<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndcrypto: the kernel crypto provider (P1-13)

XNU does no cryptography of its own beyond early boot. Everything else, from kmem slot randomisation to FileVault, IPsec, code signing and HFS+ checksums, calls through one table of function pointers, `struct crypto_functions` (`libkern/libkern/crypto/register_crypto.h`). A provider fills that table by calling `register_crypto_functions()`. On Apple systems the provider is the corecrypto kext. Its source is published only under an evaluation licence ([github.com/apple/corecrypto](https://github.com/apple/corecrypto), 90 days of internal use, no redistribution), so NeoDarwin cannot use it (`docs/repository.md` §3.1). Today the SBSA kernel stops with a data abort in `kmem_crypto_init`, the first consumer.

ndcrypto is NeoDarwin's provider. It implements the **corecrypto interface Apple publishes as APSL** (`EXTERNAL_HEADERS/corecrypto/*.h` in xnu) from three sources:
1. **xnu's own corecrypto subset**, which is Apple open source and already compiled into every kernel.
2. **FreeBSD's kernel crypto**, for the algorithms xnu doesn't carry. This is also the maintained upstream of the DES, SHA-2 and KAME code Apple itself shipped in xnu up to 1699.
3. **Small new NeoDarwin glue**: the corecrypto mode factories and the `crypto_*` multiplexers.

## 1. Where it lives and when it runs

- **Sources.** NeoDarwin-owned files under `kernel/neodarwin/crypto/` (repository policy §3 rule 3). FreeBSD sources come unmodified from a pinned `freebsd-src` mirror commit, with `PROVENANCE.md` naming each file and revision. One XNU patch adds the directory to the kernel's file list.
- **Registration.** `STARTUP(EARLY_BOOT, STARTUP_RANK_FIRST, ndcrypto_register)`, ahead of `kmem_crypto_init` at `STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE)` (`osfmk/vm/vm_kern.c:2872`). No kext can start that early, and the kernel collection carries no kexts until P5, so ndcrypto is compiled into the kernel, like the platform expert (§2.3).
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
| `ccmd5_di` | `sys/crypto/md5c.c` (`MD5Init`/`MD5Update`) | BSD-2-Clause | adapter; new `ccdigest_final_64le` (MD5 stores its length little-endian) |
| `ccsha1_di` | `sys/crypto/sha1.c` (KAME `sha1_init`/`sha1_loop`) | BSD-3-Clause | adapter |
| `ccsha384_di`, `ccsha512_di` | `sys/crypto/sha2/sha512c.c` (`SHA384_Init`, `SHA512_Init`, `SHA512_Update`) | BSD-2-Clause (Colin Percival) | adapter; both share the SHA-512 block function |
| `ccaes_ecb_encrypt` / `_decrypt` | `sys/crypto/rijndael/rijndael-alg-fst.c`, `rijndael-api.c` (`rijndael_set_key`, `rijndael_encrypt`/`_decrypt`) | public domain | adapter: a `struct ccmode_ecb` |
| `ccdes_ecb_*`, `cctdes_ecb_*` | `sys/crypto/des/des_ecb.c`, `des_enc.c`, `des_setkey.c` (`des_set_key`, `des_ecb_encrypt`, `des_ecb3_encrypt`) | SSLeay (BSD-style, Eric Young) | adapter |
| `ccdes_key_is_weak_fn`, `ccdes_key_set_odd_parity_fn` | `des_setkey.c` (`des_is_weak_key`, `des_set_odd_parity`) | same | adapter (argument order and length check) |
| `ccchacha20poly1305_fns` | `sys/crypto/chacha20/chacha.c` (D. J. Bernstein) and Poly1305 from `sys/contrib/libsodium` (poly1305-donna, as FreeBSD's `xform_poly1305.c` uses) | public domain, ISC | adapter: corecrypto's streaming `init`/`setnonce`/`aad`/`encrypt`/`finalize` over RFC 8439 |
| `ccrsa_make_pub_fn`, `ccrsa_verify_pkcs1v15_fn` | BearSSL `contrib/bearssl/src/rsa/rsa_i31_pub.c`, `rsa_i31_pkcs1_vrfy.c`, which FreeBSD's `lib/libsecureboot` uses for signature checks | MIT | adapter: corecrypto's `ccn` limb arrays to BearSSL big-endian keys; verify only, no private-key operations |

### 2.3 Modes (new, generic over any `struct ccmode_ecb`)

The corecrypto *factories* (`ccmode_factory_cbc_encrypt` and friends, `EXTERNAL_HEADERS/corecrypto/ccmode_factory.h`) are closed, and FreeBSD's `opencrypto` xforms are welded to its rijndael context. So ndcrypto writes the factories once over the ECB interface. They then serve AES and DES alike, and an accelerated ECB drops in later.

| Entries | Construction | Reference |
|---|---|---|
| `ccaes_cbc_*`, `ccdes_cbc_*`, `cctdes_cbc_*` | CBC | FreeBSD `opencrypto/xform_aes_cbc.c` |
| `ccaes_ctr_crypt` | CTR with a 128-bit big-endian counter | `opencrypto/xform_aes_icm.c` |
| `ccaes_xts_encrypt` / `_decrypt` | XTS (IEEE 1619), including ciphertext stealing | `opencrypto/xform_aes_xts.c` |
| `ccaes_gcm_*`, `ccgcm_init_with_iv_fn`, `ccgcm_inc_iv_fn` | GCM: CTR plus GHASH, using xnu's GCM field multiply (§2.1) | NIST SP 800-38D |
| `ccpad_cts3_encrypt_fn` / `_decrypt_fn` | CBC ciphertext stealing, variant CS3 | NIST SP 800-38A addendum |

### 2.4 Randomness and the `crypto_*` multiplexers (new, over xnu)

| Entries | Implementation |
|---|---|
| `random_kmem_ctx_size_fn`, `random_kmem_init_fn`, `random_generate_fn`, `random_uniform_fn` | a per-context xnu HMAC-DRBG (SHA-256), seeded from the kernel's entropy (`read_random`, itself fed by `early_random` and the loader's `/chosen/random-seed`); `random_uniform` by rejection sampling |
| `ccrng_fn` | a `struct ccrng_state` whose `generate` calls the same DRBG under a lock |
| `ccdigest_*_fn`, `digest_*_fn` | `ccdigest_*` from xnu, with `crypto_digest_alg_t` mapped to the descriptors in §2.1 and §2.2 |
| `cchmac_*_fn`, `hmac_*_fn` | `cchmac_*` from xnu, with verification by `cc_cmp_safe` (constant time) |

## 3. Later: Armv8 acceleration

FreeBSD's `sys/crypto/armv8` (AES and GHASH with the Crypto Extensions) and `sys/crypto/sha2/sha*_arm64.c` are BSD-licensed. They slot in behind the same ECB and digest adapters once the portable path passes its tests. None is needed to boot.

## 4. Verification

- **Host tests.** ndcrypto builds as a host library as well as into the kernel, and a Bazel test runs published known-answer vectors:
  - NIST CAVP for AES (ECB, CBC, CTR, XTS, GCM), SHA-1, SHA-2 and HMAC;
  - RFC 1321 for MD5;
  - FIPS 46-3 for DES and 3DES;
  - RFC 8439 for ChaCha20-Poly1305;
  - NIST CAVP (FIPS 186) for RSA PKCS#1 v1.5 verify;
  - SP 800-90A for the DRBG.
- **Boot.** `//kernel:sbsa_boot_test` extends past `kmem_crypto_init`.
- **Fail loudly.** An entry ndcrypto can't yet provide calls a stub that panics with the entry's name, never a NULL.
