#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# unzip from Apple's zip-29 (Info-ZIP UnZip 6.0; P4-21 checkpoint 2,
# docs/architecture/freebsd-parity.md §2.1): replays unzip/Makefile, which
# runs unzip60/unix/Makefile's macosx target ("make unzips" with CFLAGS
# -O3 -Wall -DBSD -DLARGE_FILE_SUPPORT -DUNICODE_SUPPORT, plus the
# Makefile's CF_NOOPT -I. -DUNIX) and its install target. zip itself
# (zip30) isn't built: FreeBSD's base has no zip.
#   build.sh OUT ZIP_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/{unzip,funzip,unzipsfx} (UNZIPS), zipinfo (install's
# hard link to unzip: a copy here) and the zipgrep script. Warning flags
# change no interface and are left out. Patch 0001 (ND_NO_QUARANTINE) drops
# libquarantine.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; Z="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
Z="$(stage_src "$Z" "$B/src" "$PROJ/patches")/unzip/unzip60"   # patches/ applied
cd "$Z"

cf=("${TARGET_FLAGS[@]}" -mcpu=cortex-a76 -O3 -DBSD -DLARGE_FILE_SUPPORT -DUNICODE_SUPPORT -I. -DUNIX -DND_NO_QUARANTINE
	$(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$Z/"=unzip60/)
write_rsp "$B/cflags" "${cf[@]}"
write_rsp "$B/sfx.rsp" "${cf[@]}" -DSFX         # OBJX's "_" objects
write_rsp "$B/funzip.rsp" "${cf[@]}" -DFUNZIP   # OBJF's "f" objects
mkdir -p "$OUT/usr/bin"
# unzip: OBJS (CRCA_O is empty: no assembly CRC); $M is unix/unix.c.
tool "$B" "$ROOT" "$OUT/usr/bin/unzip" "$B/cflags" unzip.c crc32.c crypt.c envargs.c explode.c extract.c \
	fileio.c globals.c inflate.c list.c match.c process.c ttyio.c ubz2err.c unreduce.c unshrink.c zipinfo.c \
	unix/unix.c
# funzip: OBJF, with funzip.c and crc32.c as they are.
compile "$B/obj/funzip_plain" "$B/cflags" funzip.c crc32.c
compile "$B/obj/funzip_f" "$B/funzip.rsp" crypt.c globals.c inflate.c ttyio.c
link_tool "$ROOT" "$OUT/usr/bin/funzip" "$B"/obj/funzip_plain/*.o "$B"/obj/funzip_f/*.o
# unzipsfx: OBJX (unzipsfx.o is unzip.c with -DSFX), crc32.c as it is.
compile "$B/obj/sfx_plain" "$B/cflags" crc32.c
compile "$B/obj/sfx" "$B/sfx.rsp" unzip.c crypt.c extract.c fileio.c globals.c inflate.c match.c process.c \
	ttyio.c ubz2err.c unix/unix.c
link_tool "$ROOT" "$OUT/usr/bin/unzipsfx" "$B"/obj/sfx_plain/*.o "$B"/obj/sfx/*.o
cp "$OUT/usr/bin/unzip" "$OUT/usr/bin/zipinfo"
install -m 0755 unix/zipgrep "$OUT/usr/bin/zipgrep"
