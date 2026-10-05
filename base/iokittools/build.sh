#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ioreg from IOKitTools-125 (P4-21 checkpoint 6b, docs/base/corefoundation.md):
# /usr/sbin/ioreg, the I/O Registry browser, against the base's
# CoreFoundation and IOKit frameworks; ncurses for its terminal width
# (tputs). Its page, ioreg.8, is installed by pages.sh.
#   build.sh OUT IOKITTOOLS_SRC SYSROOT DEPROOT...
#   (DEPROOTs: //base:root, //base:corefoundation_framework,
#    //base:iokit_framework, //base:libncurses_dylib)
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CFT=""; IOK=""; NC=""
for d in "${DEPS[@]}"; do
	[ -d "$d/usr/local/frameworks/CoreFoundation.framework" ] && CFT="$d"
	[ -d "$d/usr/local/frameworks/IOKit.framework" ] && IOK="$d"
	[ -f "$d/usr/lib/libncurses.5.4.dylib" ] && NC="$d"
done
[ -n "$CFT" ] && [ -n "$IOK" ] && [ -n "$NC" ] || { echo "iokittools/build.sh: pass the CoreFoundation, IOKit and ncurses trees" >&2; exit 1; }
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$T"
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu11 -iframework "$IOK/usr/local/frameworks" \
	-iframework "$CFT/usr/local/frameworks" $(cmd_sysroot_flags "$SYSROOT") -isystem "$NC/usr/local/include" \
	-ffile-prefix-map="$T/"=IOKitTools/
tool "$B" "$ROOT" "$OUT/usr/sbin/ioreg" "$B/cflags" ioreg.tproj/ioreg.c -- \
	"$CFT/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"$IOK/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit" -L"$NC/usr/lib" -lncurses
