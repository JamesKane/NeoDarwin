<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndcrypto provenance

ndcrypto's own files (`nd_*.c`, `*.h`, `compat/`, `test/`) are NeoDarwin code, BSD-2-Clause. It builds on two upstreams, used unmodified. Design and per-entry mapping: `docs/kernel/crypto-provider.md`.

## Apple open source (APSL 2.0)

xnu-12377.1.9 (the kernel's own pin, `kernel/upstream.lock`):
- `EXTERNAL_HEADERS/corecrypto/*.h`, the interface ndcrypto implements;
- `libkern/libkern/crypto/{register_crypto,rand,crypto}.h`;
- `osfmk/corecrypto/*.c`, the digest framework, SHA-256, HMAC, HMAC-DRBG and the GCM helpers.

In the kernel these are xnu's own objects. The host test builds them from the same archive (`@apple_xnu_corecrypto`).

## FreeBSD

`freebsd.lock` pins every file by commit (`github.com/freebsd/freebsd-src`) and SHA-256; `tools/pinned/lock.sh` refreshes it. Licences, as the files state:

| Path | Origin | Licence |
|---|---|---|
| `sys/crypto/md5c.c`, `sys/sys/md5.h` | FreeBSD (R. Clausecker) | BSD-2-Clause |
| `sys/crypto/sha1.c`, `sha1.h` | KAME | BSD-3-Clause |
| `sys/crypto/sha2/sha512c.c` and headers | C. Percival | BSD-2-Clause |
| `sys/crypto/rijndael/*` | V. Rijmen, A. Bosselaers, P. Barreto | public domain |
| `sys/crypto/des/*` | E. Young (SSLeay) | SSLeay licence (BSD-style) |
| `sys/crypto/chacha20/*` | D. J. Bernstein | public domain |
| `sys/contrib/libsodium/.../poly1305/donna/*` and sodium headers | libsodium (F. Denis), poly1305-donna (A. Moon) | ISC (`sys/contrib/libsodium/LICENSE`) |
| `contrib/bearssl/*` | T. Pornin | MIT (`contrib/bearssl/LICENSE.txt`) |

The FreeBSD DES and SHA-1 function names collide with xnu's libkern KPI, so they are compiled under an `nd_fb_` prefix (`compat/nd_freebsd.h`); the source is unchanged.
