<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndcrypto: the kernel crypto provider (P1-13)

XNU does no cryptography of its own beyond early boot. Everything else goes through two hooks, and a provider must fill both:
- **The crypto table.** Kmem slot randomisation, FileVault, IPsec, code signing and HFS+ checksums all call through one table of function pointers, `struct crypto_functions` (`libkern/libkern/crypto/register_crypto.h`). The provider fills it by calling `register_crypto_functions()`.
- **The kernel PRNG.** `read_random()` calls through a PRNG the provider installs with `register_and_init_prng()` (`osfmk/prng/prng_random.c`). Until then only xnu's early DRBG exists. On Apple systems the provider is the corecrypto kext. Its source is published only under an evaluation licence ([github.com/apple/corecrypto](https://github.com/apple/corecrypto), 90 days of internal use, no redistribution), so NeoDarwin cannot use it (`docs/repository.md` §3.1). Today the SBSA kernel stops with a data abort in `kmem_crypto_init`, the first consumer.

ndcrypto is NeoDarwin's provider. It implements the **corecrypto interface Apple publishes as APSL** (`EXTERNAL_HEADERS/corecrypto/*.h` in xnu) from three sources:
1. **xnu's own corecrypto subset**, which is Apple open source and already compiled into every kernel.
2. **FreeBSD's kernel crypto**, for the algorithms xnu doesn't carry. This is also the maintained upstream of the DES, SHA-2 and KAME code Apple itself shipped in xnu up to 1699.
3. **Small new NeoDarwin glue**: the corecrypto mode factories and the `crypto_*` multiplexers.

## 1. Where it lives and when it runs

- **Sources.** NeoDarwin-owned files under `kernel/neodarwin/crypto/` (repository policy §3 rule 3). The FreeBSD sources (70 files, including BearSSL and libsodium, with their licences) are fetched unmodified by commit and per-file SHA-256 from `freebsd.lock` (`rules/pinned_files.bzl`; `tools/pinned/lock.sh` refreshes the pin). The kernel build overlays both at `libkern/ndcrypto/` (`xnu_kernel`'s `overlays`), and patch 0008 adds them to libkern's file list. They compile in their userland branches with `compat/nd_freebsd.h` forced in; `compat/kernel/` maps the few libc headers they name onto the kernel's.
- **Registration.** `STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST, ndcrypto_register)` in `nd_kernel.c`: first the table, then the kernel PRNG (whose `entropy_init` hashes through the table), ahead of `kmem_crypto_init` at `STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE)` (`osfmk/vm/vm_kern.c:2872`). No kext can start that early, and the kernel collection carries no kexts until P5, so ndcrypto is compiled into the kernel, like the platform expert (§2.3).
- **Swappable later.** Because ndcrypto implements Apple's published interface, a corecrypto kext could replace it later without touching any consumer, if Apple ever relicenses corecrypto.

## 2. Mapping

Legend for **Glue**: *none* means used as is; *adapter* means a few lines that fit a FreeBSD entry point to a corecrypto struct; *new* means NeoDarwin code.

### 2.1 Framework (xnu, Apple open source, already built)

| corecrypto piece | xnu source (`osfmk/corecrypto/`) | Used for |
|---|---|---|
| `ccdigest_init` / `ccdigest_update` / `ccdigest_final_64be` | `ccdigest_init.c`, `ccdigest_update.c`, `ccdigest_final_64be.c` | every digest below is a `struct ccdigest_info` driven by these |
| `ccsha256_di` (SHA-256) | `ccsha256_di.c`, `ccsha256_ltc_*.c`, plus `osfmk/arm64/corecrypto/sha256_compress_arm64.s` | table entry `ccsha256_di` directly |
| `cchmac_*` | `cchmac*.c` | `cchmac_*_fn`, and `crypto_hmac_*` for every digest |
| `ccdrbg_nisthmac` (NIST SP 800-90A HMAC-DRBG) | `ccdrbg.c`, `ccdrbg_nisthmac.c` | the `random_*` entries (§2.4) |
| `cchkdf`, `cccbc_*`, `ccgcm_*` front ends, GCM field multiply | `cchkdf.c`, `cccbc.c`, `ccgcm.c`, `ccmode_gcm_gf_mult.c`, `ccmode_gcm_mult_h.c` | GHASH inside the GCM factory (§2.3) |

### 2.2 Primitives (FreeBSD)

The adapters drive FreeBSD's public entry points on whole blocks, so no FreeBSD file is edited. For example, a digest `compress(state, nblocks, data)` loads `state` into a FreeBSD context with an empty buffer, calls `*_Update` on `nblocks` full blocks, and stores the state back.

| `crypto_functions` entry | FreeBSD source | Licence | Glue |
|---|---|---|---|
| `ccmd5_di` | `sys/crypto/md5c.c` (`MD5Update`) | BSD-2-Clause | adapter; a little-endian final written here (MD5 stores its length little-endian) |
| `ccsha1_di` | `sys/crypto/sha1.c` (KAME `sha1_init`/`sha1_loop`) | BSD-3-Clause | adapter |
| `ccsha384_di`, `ccsha512_di` | `sys/crypto/sha2/sha512c.c` (`SHA512_Update`) | BSD-2-Clause (Colin Percival) | adapter; both share the SHA-512 block function and a 128-byte-block final written here |
| `ccaes_ecb_encrypt` / `_decrypt` | `sys/crypto/rijndael/rijndael-alg-fst.c` (`rijndaelKeySetupEnc`/`Dec`, `rijndaelEncrypt`/`Decrypt`) | public domain | adapter: a `struct ccmode_ecb` holding one key schedule. libkern stores AES-CBC contexts in 280 bytes (`AES_CBC_CTX_MAX_SIZE`), which FreeBSD's two-schedule `rijndael_ctx` would overflow |
| `ccdes_ecb_*`, `cctdes_ecb_*` | `sys/crypto/des/des_ecb.c`, `des_enc.c`, `des_setkey.c` (`des_set_key`, `des_ecb_encrypt`, `des_ecb3_encrypt`) | SSLeay (BSD-style, Eric Young) | adapter |
| `ccdes_key_is_weak_fn`, `ccdes_key_set_odd_parity_fn` | `des_setkey.c` (`des_is_weak_key`, `des_set_odd_parity`) | same | adapter (argument order and length check) |
| `ccchacha20poly1305_fns` | `sys/crypto/chacha20/chacha.c` (D. J. Bernstein) and Poly1305 from `sys/contrib/libsodium` (poly1305-donna, as FreeBSD's `xform_poly1305.c` uses) | public domain, ISC | adapter: corecrypto's streaming `init`/`setnonce`/`aad`/`encrypt`/`finalize` over RFC 8439. `nd_chachapoly.c` includes the unmodified donna source. The 64-bit donna state (89 bytes) fits corecrypto's `ccpoly1305_ctx`; the 32-bit one does not on LP64. corecrypto documents no `incnonce`, so ndcrypto defines it as incrementing the 96-bit nonce, little-endian |
| `ccrsa_make_pub_fn`, `ccrsa_verify_pkcs1v15_fn` | BearSSL `contrib/bearssl/src/rsa/rsa_i31_pub.c`, `rsa_i31_pkcs1_vrfy.c`, which FreeBSD's `lib/libsecureboot` uses for signature checks | MIT | adapter: corecrypto's `ccn` limb arrays to BearSSL big-endian keys; verify only, no private-key operations |

### 2.3 Modes (new, generic over any `struct ccmode_ecb`)

The corecrypto *factories* (`ccmode_factory_cbc_encrypt` and friends, `EXTERNAL_HEADERS/corecrypto/ccmode_factory.h`) are closed, and FreeBSD's `opencrypto` xforms are welded to its rijndael context. So ndcrypto writes the factories once over the ECB interface. They then serve AES and DES alike, and an accelerated ECB drops in later.

| Entries | Construction | Reference |
|---|---|---|
| `ccaes_cbc_*`, `ccdes_cbc_*`, `cctdes_cbc_*` | CBC | FreeBSD `opencrypto/xform_aes_cbc.c` |
| `ccaes_ctr_crypt` | CTR with a 128-bit big-endian counter | `opencrypto/xform_aes_icm.c` |
| `ccaes_xts_encrypt` / `_decrypt` | XTS (IEEE 1619) over whole blocks, which is all the interface offers | `opencrypto/xform_aes_xts.c` |
| `ccaes_gcm_*`, `ccgcm_init_with_iv_fn`, `ccgcm_inc_iv_fn` | GCM, built on corecrypto's generic key layout, which xnu publishes (`struct _ccmode_gcm_key`, `osfmk/corecrypto/ccmode_gcm_internal.h`). xnu's own helpers (`update_pad`, `aad_finalize`, `mult_h`) and front ends (`ccgcm_init_with_iv`, `ccgcm_inc_iv`) work on it unchanged | NIST SP 800-38D |
| `ccpad_cts3_encrypt_fn` / `_decrypt_fn` | CBC ciphertext stealing, variant CS3 | NIST SP 800-38A addendum |

### 2.4 Randomness and the `crypto_*` multiplexers (new, over xnu)

| Entries | Implementation |
|---|---|
| `random_kmem_ctx_size_fn`, `random_kmem_init_fn`, `random_generate_fn`, `random_uniform_fn` | a ChaCha20 generator (FreeBSD) with fast key erasure: each 64-byte block rekeys with its first half and yields the second. It is about 100 bytes, within `CRYPTO_RANDOM_MAX_CTX_SIZE` (256). It is seeded from `read_frandom()` (xnu's early DRBG, itself seeded from the loader's `/chosen/random-seed`); `random_uniform` uses rejection sampling |
| kernel PRNG (`register_and_init_prng`) | xnu's HMAC-DRBG (SHA-256) is the central pool. It is seeded at boot and reseeded from the kernel's entropy source every 256 refreshes. Each CPU has its own ChaCha20 generator, rekeyed from the pool whenever the pool's epoch advances, so `generate` takes no lock |
| `ccrng_fn` | a `struct ccrng_state` whose `generate` calls `read_random()` |
| `ccdigest_*_fn`, `digest_*_fn` | `ccdigest_*` from xnu, with `crypto_digest_alg_t` mapped to the descriptors in §2.1 and §2.2 |
| `cchmac_*_fn`, `hmac_*_fn` | `cchmac_*` from xnu, with verification by `cc_cmp_safe` (constant time) |

## 3. Later: Armv8 acceleration

FreeBSD's `sys/crypto/armv8` (AES and GHASH with the Crypto Extensions) and `sys/crypto/sha2/sha*_arm64.c` are BSD-licensed. They slot in behind the same ECB and digest adapters once the portable path passes its tests. None is needed to boot.

## 4. Verification

- **Host tests.** `//kernel/neodarwin/crypto:ndcrypto_kat_test`: ndcrypto builds as a host library as well as into the kernel, and the test reaches every entry through the table, as libkern does. It checks 110 results against published known-answer vectors:
  - NIST CAVP for AES (ECB, CBC, CTR, XTS, GCM), SHA-1, SHA-2 and HMAC;
  - RFC 1321 for MD5;
  - FIPS 46-3 for DES and 3DES;
  - RFC 8439 for ChaCha20-Poly1305;
  - RFC 3962 for CBC-CS3;
  - an OpenSSL-generated signature for RSA PKCS#1 v1.5 verify.

  It also checks the negative cases (altered tags, signatures and digests) and each context against libkern's fixed buffer sizes. The DRBG is xnu's own code; the tests check that the generators built on it rekey and diverge.
- **Boot.** `//kernel:sbsa_boot_test` extends past `kmem_crypto_init`.
- **Fail loudly.** An entry ndcrypto can't yet provide calls a stub that panics with the entry's name, never a NULL.
