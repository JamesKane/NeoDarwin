#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libmd from FreeBSD's lib/libmd (docs/base/libsystem.md): md5(1)'s digests.
#   build.sh OUT LIBMD_ROOT SYSROOT DEPROOT...   (DEPROOT: //base:root)
# LIBMD_ROOT holds the freebsd-src files freebsd.lock pins, laid out as in
# freebsd-src. OUT receives usr/lib/libmd.dylib and, build-only, its headers
# in usr/local/include (md5.h, sha.h, sha224.h, sha256.h, sha384.h,
# sha512.h, sys/md5.h, and compat/libmd_cdefs.h, which a program including
# them force-includes: FreeBSD's <sys/cdefs.h> names they use).
# Why FreeBSD: macOS ships /usr/lib/libmd.dylib (text_cmds' md5 target links
# -lmd) but Apple publishes no source for it, and md5.c is FreeBSD's, written
# against this API. What: the algorithms md5.c builds on Apple platforms
# (MD5, SHA-1, SHA-224, SHA-256, SHA-384, SHA-512), from lib/libmd/Makefile:
# the sys/crypto block functions and the End/File/FileChunk/Fd/Data helpers
# that Makefile generates from mdXhl.c with sed. The C versions: the arm64
# assembly and ARMv8 crypto-extension variants are left out
# (USE_ASM_SOURCES=0, as when bootstrapping). MD4, RIPEMD-160, SHA-0,
# SHA-512/t and Skein, which md5.c leaves out on Apple, are left out too.
# ndcrypto's prelude (kernel/neodarwin/crypto/compat) supplies
# <sys/endian.h> and explicit_bzero. Install name and versions are Apple's
# (/usr/lib/libmd.dylib 1.0, linking libSystem alone).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
NDCOMPAT="$(cd "$PROJ/../../kernel/neodarwin/crypto/compat" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$B/derived"; mkdir -p "$D/sys"
cp "$M/sys/sys/md5.h" "$D/sys/md5.h"   # the Makefile's sys/md5.h link

# The Makefile's generated helpers: mdXhl.c with the algorithm's names.
hl() {   # hl OUT LENGTH SED-EXPR...
	local out="$1" len="$2"; shift 2
	{ echo "#define LENGTH $len"; sed "$@" "$M/lib/libmd/mdXhl.c"; } > "$D/$out"
}
hl md5hl.c 16 -e 's/mdX/md5/g' -e 's/MDX/MD5/g'
hl sha1hl.c 20 -e 's/mdX/sha/g' -e 's/MDX/SHA1_/g' -e 's/SHA1__/SHA1_/g'
hl sha224hl.c 28 -e 's/mdX/sha224/g' -e 's/MDX/SHA224_/g' -e 's/SHA224__/SHA224_/g'
hl sha256hl.c 32 -e 's/mdX/sha256/g' -e 's/MDX/SHA256_/g' -e 's/SHA256__/SHA256_/g'
hl sha384hl.c 48 -e 's/mdX/sha384/g' -e 's/MDX/SHA384_/g' -e 's/SHA384__/SHA384_/g'
hl sha512hl.c 64 -e 's/mdX/sha512/g' -e 's/MDX/SHA512_/g' -e 's/SHA512__/SHA512_/g'

# CFLAGS: -I. (sys/md5.h), -I${.CURDIR}, sys/crypto/sha2. WARNS=0. The
# headers rename the functions _libmd_*; WEAK_REFS gives them their public
# names with ELF's __weak_reference, which Mach-O lacks: the link aliases
# them instead (ld -alias), so both names are exported, as on FreeBSD.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common -w -include nd_freebsd.h -I"$NDCOMPAT" \
	-I"$D" -I"$M/lib/libmd" -I"$M/sys/crypto/sha2" '-D__weak_reference(sym,alias)=' $(cmd_sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" "$M/sys/crypto/md5c.c" "$M/lib/libmd/sha1c.c" "$M/sys/crypto/sha2/sha256c.c" \
	"$M/sys/crypto/sha2/sha512c.c" "$D"/md5hl.c "$D"/sha1hl.c "$D"/sha224hl.c "$D"/sha256hl.c "$D"/sha384hl.c \
	"$D"/sha512hl.c

mkdir -p "$OUT/usr/lib" "$OUT/usr/local/include/sys"
nm -gUj "$B"/obj/*.o | sed -n 's/^__libmd_\(.*\)/__libmd_\1 _\1/p' | sort -u > "$B/aliases"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libmd.dylib -current_version 1.0 -compatibility_version 1.0 \
	-syslibroot "$ROOT" -alias_list "$B/aliases" "$B"/obj/*.o -lSystem -o "$OUT/usr/lib/libmd.dylib"
# INCS: build-only here, as the base's other headers are.
cp "$M/lib/libmd/md5.h" "$M/lib/libmd/sha.h" "$M"/sys/crypto/sha2/sha{224,256,384,512}.h "$OUT/usr/local/include/"
cp "$M/sys/sys/md5.h" "$OUT/usr/local/include/sys/"
cp "$PROJ/compat/libmd_cdefs.h" "$OUT/usr/local/include/"
