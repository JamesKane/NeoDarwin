#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libzstd and zstd from Zstandard 1.5.7 (P4-21 checkpoint 2,
# docs/architecture/freebsd-parity.md §2.1), as FreeBSD's lib/libzstd and
# usr.bin/zstd Makefiles build them from sys/contrib/zstd (1.5.7 at
# freebsd-src 050683bb8e13).
#   build.sh OUT ZSTD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libzstd.1.dylib with upstream's libzstd.dylib and
# libzstd.1.5.7.dylib links, usr/bin/zstd with usr.bin/zstd's LINKS
# (unzstd, zstdcat, zstdmt), and, build-only, zstd.h, zdict.h and
# zstd_errors.h in usr/local/include.
# Why upstream: macOS ships no zstd. The release tarball is the source
# FreeBSD's pin carries, in one archive. FreeBSD builds libzstd as a private
# library for its own programs; here it is /usr/lib/libzstd.1.dylib, with
# lib/Makefile's install name and versions for Darwin (compatibility 1,
# current 1.5.7).
# lib/libzstd's SRCS and CFLAGS: XXH_NAMESPACE=ZSTD_, ZSTD_MULTITHREAD,
# -fvisibility=hidden (ZSTDLIB_API's symbols are exported), no legacy
# formats, and the C Huffman decoder (the assembly one is x86-64's).
# usr.bin/zstd's: the programs/ sources with HAVE_THREAD and
# ZSTD_MULTITHREAD, linking libzstd; no zlib, lzma or lz4 formats. The
# library's thread pool and xxhash are hidden in it, so zstd compiles its
# own copies, as programs/Makefile's zstd-dll target does
# (ZSTDLIB_LOCAL_SRC: xxhash.c, pool.c, threading.c).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$Z"

common=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -fno-common -DXXH_NAMESPACE=ZSTD_ -DZSTD_MULTITHREAD=1
	-Ilib -Ilib/common $(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$Z/"=zstd/)
write_rsp "$B/lib.rsp" "${common[@]}" -fvisibility=hidden -DZSTD_DISABLE_ASM
compile "$B/obj/lib" "$B/lib.rsp" lib/common/entropy_common.c lib/common/error_private.c \
	lib/common/fse_decompress.c lib/common/pool.c lib/common/threading.c lib/common/xxhash.c \
	lib/common/zstd_common.c lib/common/debug.c lib/compress/fse_compress.c lib/compress/huf_compress.c \
	lib/compress/zstd_compress.c lib/compress/zstd_compress_literals.c lib/compress/zstd_compress_sequences.c \
	lib/compress/zstd_compress_superblock.c lib/compress/zstd_preSplit.c lib/compress/zstdmt_compress.c \
	lib/compress/zstd_fast.c lib/compress/zstd_lazy.c lib/compress/zstd_ldm.c lib/compress/zstd_opt.c \
	lib/compress/zstd_double_fast.c lib/compress/hist.c lib/decompress/huf_decompress.c \
	lib/decompress/zstd_ddict.c lib/decompress/zstd_decompress.c lib/decompress/zstd_decompress_block.c \
	lib/deprecated/zbuff_common.c lib/deprecated/zbuff_compress.c lib/deprecated/zbuff_decompress.c \
	lib/dictBuilder/cover.c lib/dictBuilder/divsufsort.c lib/dictBuilder/zdict.c lib/dictBuilder/fastcover.c
mkdir -p "$OUT/usr/lib" "$OUT/usr/bin" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/libzstd.1.dylib -current_version 1.5.7 -compatibility_version 1 \
	-syslibroot "$ROOT" "$B"/obj/lib/*.o -lSystem -o "$OUT/usr/lib/libzstd.1.dylib"
ln -sf libzstd.1.dylib "$OUT/usr/lib/libzstd.dylib"; ln -sf libzstd.1.dylib "$OUT/usr/lib/libzstd.1.5.7.dylib"
install -m 0444 lib/zstd.h lib/zdict.h lib/zstd_errors.h "$OUT/usr/local/include/"

write_rsp "$B/cmd.rsp" "${common[@]}" -Iprograms -Ilib/compress -Ilib/dictBuilder -DHAVE_THREAD=1
tool "$B" "$ROOT" "$OUT/usr/bin/zstd" "$B/cmd.rsp" programs/benchfn.c programs/benchzstd.c programs/datagen.c \
	programs/dibio.c programs/fileio.c programs/fileio_asyncio.c programs/lorem.c programs/timefn.c \
	programs/util.c programs/zstdcli.c programs/zstdcli_trace.c lib/common/xxhash.c lib/common/pool.c \
	lib/common/threading.c -- -L"$OUT/usr/lib" -lzstd
for l in unzstd zstdcat zstdmt; do cp "$OUT/usr/bin/zstd" "$OUT/usr/bin/$l"; done
