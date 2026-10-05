#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# liblzma and the xz commands from XZ Utils 5.4.7 (P4-21 checkpoint 2,
# docs/architecture/freebsd-parity.md §2.1).
#   build.sh OUT XZ_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/liblzma.5.dylib with its liblzma.dylib link,
# usr/bin/xz with FreeBSD's usr.bin/xz LINKS (unxz, lzma, unlzma, xzcat,
# lzcat), usr/bin/xzdec with usr.bin/xzdec's lzdec, usr/bin/lzmainfo, and,
# build-only, lzma.h and lzma/ in usr/local/include.
# Why upstream: macOS 26 ships /usr/lib/liblzma.5.dylib, but the release
# set has no xz project. Its exports (liblzma.tbd: lzma_lzip_decoder,
# lzma_microlzma_*, lzma_str_*, lzma_file_info_decoder, no 5.6 additions)
# are XZ Utils 5.4's, so this is the last 5.4 release, built as Apple's
# (install name, current version 6.3, compatibility 6). FreeBSD's rows
# (usr.bin/xz, xzdec, lzmainfo) build the same programs from contrib/xz
# (5.8.4 at 050683bb8e13).
# What: the sources src/liblzma/Makefile.am, src/xz, src/xzdec and
# src/lzmainfo build with configure's defaults (every filter, encoder and
# decoder, the lzip decoder, POSIX threads, no NLS) for macOS; config.h is
# that configure run's (base/xz/config.h). -fvisibility=hidden, so only
# LZMA_API symbols are exported, as libtool's build does.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; X="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$X/src"

common=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O2 -std=gnu99 -fno-common -pthread -DHAVE_CONFIG_H -I"$PROJ"
	-I"$X/src/common" -I"$X/src/liblzma/api" $(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$X/"=xz/)
L=liblzma
write_rsp "$B/lib.rsp" "${common[@]}" -fvisibility=hidden -DTUKLIB_SYMBOL_PREFIX=lzma_ -I"$L/common" \
	-I"$L/check" -I"$L/lz" -I"$L/rangecoder" -I"$L/lzma" -I"$L/delta" -I"$L/simple"
compile "$B/obj/lib" "$B/lib.rsp" common/tuklib_cpucores.c common/tuklib_physmem.c \
	$L/check/check.c $L/check/crc32_fast.c $L/check/crc32_table.c $L/check/crc64_fast.c $L/check/crc64_table.c \
	$L/check/sha256.c $(for s in alone_decoder alone_encoder auto_decoder block_buffer_decoder \
		block_buffer_encoder block_decoder block_encoder block_header_decoder block_header_encoder block_util \
		common easy_buffer_encoder easy_decoder_memusage easy_encoder_memusage easy_encoder easy_preset \
		file_info filter_buffer_decoder filter_buffer_encoder filter_common filter_decoder filter_encoder \
		filter_flags_decoder filter_flags_encoder hardware_cputhreads hardware_physmem index_decoder \
		index_encoder index_hash index lzip_decoder microlzma_decoder microlzma_encoder outqueue \
		stream_buffer_decoder stream_buffer_encoder stream_decoder_mt stream_decoder stream_encoder_mt \
		stream_encoder stream_flags_common stream_flags_decoder stream_flags_encoder string_conversion \
		vli_decoder vli_encoder vli_size; do echo $L/common/$s.c; done) \
	$L/delta/delta_common.c $L/delta/delta_decoder.c $L/delta/delta_encoder.c $L/lz/lz_decoder.c \
	$L/lz/lz_encoder_mf.c $L/lz/lz_encoder.c $L/lzma/fastpos_table.c $L/lzma/lzma_decoder.c \
	$L/lzma/lzma_encoder_optimum_fast.c $L/lzma/lzma_encoder_optimum_normal.c $L/lzma/lzma_encoder_presets.c \
	$L/lzma/lzma_encoder.c $L/lzma/lzma2_decoder.c $L/lzma/lzma2_encoder.c $L/rangecoder/price_table.c \
	$L/simple/arm.c $L/simple/arm64.c $L/simple/armthumb.c $L/simple/ia64.c $L/simple/powerpc.c \
	$L/simple/simple_coder.c $L/simple/simple_decoder.c $L/simple/simple_encoder.c $L/simple/sparc.c $L/simple/x86.c
mkdir -p "$OUT/usr/lib" "$OUT/usr/bin" "$OUT/usr/local/include"
xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
	-install_name /usr/lib/liblzma.5.dylib -current_version 6.3 -compatibility_version 6 \
	-syslibroot "$ROOT" "$B"/obj/lib/*.o -lSystem -o "$OUT/usr/lib/liblzma.5.dylib"
ln -sf liblzma.5.dylib "$OUT/usr/lib/liblzma.dylib"
cp -R "$L/api/lzma.h" "$L/api/lzma" "$OUT/usr/local/include/"

# The commands (src/xz, src/xzdec, src/lzmainfo), linking liblzma.
write_rsp "$B/cmd.rsp" "${common[@]}" '-DLOCALEDIR=\"/usr/share/locale\"'
lz=(-L"$OUT/usr/lib" -llzma)
tool "$B" "$ROOT" "$OUT/usr/bin/xz" "$B/cmd.rsp" common/tuklib_exit.c common/tuklib_mbstr_fw.c \
	common/tuklib_mbstr_width.c common/tuklib_open_stdxxx.c common/tuklib_progname.c xz/args.c xz/coder.c \
	xz/file_io.c xz/hardware.c xz/list.c xz/main.c xz/message.c xz/mytime.c xz/options.c xz/signals.c \
	xz/suffix.c xz/util.c -- "${lz[@]}"
for l in unxz lzma unlzma xzcat lzcat; do cp "$OUT/usr/bin/xz" "$OUT/usr/bin/$l"; done
write_rsp "$B/xzdec.rsp" "${common[@]}" -DTUKLIB_GETTEXT=0
tool "$B" "$ROOT" "$OUT/usr/bin/xzdec" "$B/xzdec.rsp" xzdec/xzdec.c common/tuklib_progname.c \
	common/tuklib_exit.c -- "${lz[@]}"
cp "$OUT/usr/bin/xzdec" "$OUT/usr/bin/lzdec"
tool "$B" "$ROOT" "$OUT/usr/bin/lzmainfo" "$B/cmd.rsp" lzmainfo/lzmainfo.c common/tuklib_progname.c \
	common/tuklib_exit.c -- "${lz[@]}"
