#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# top from top-144 (the macOS 26.0 release; P4-21 checkpoint 6,
# docs/architecture/freebsd-parity.md §2.1): /usr/bin/top and its page
# top.1. FreeBSD's systat row is its equivalent.
#   build.sh OUT TOP_SRC SYSROOT DEPROOT...
#   (DEPROOTs: //base:root, //base:corefoundation_framework,
#    //base:iokit_framework, //base:libncurses_dylib, //base:libutil_dylib)
# Replays top.xcodeproj's libtop target (libtop.c, a static library) and
# top target: its sources, GCC_OPTIMIZATION_LEVEL s,
# TOP_ANONYMOUS_MEMORY (GCC_PREPROCESSOR_DEFINITIONS for macosx), and its
# Frameworks phase's CoreFoundation, IOKit (the base's, docs/base/
# corefoundation.md; libtop reads disk statistics from each
# IOBlockStorageDriver), libncurses, libpanel and libutil. Apple installs it
# setuid root (INSTALL_MODE_FLAG 04555), to read other tasks' ports;
# images/BUILD.bazel sets the mode. The entitlements (task_for_pid) are
# code-signing policy NeoDarwin doesn't enforce. Patch 0001 gives libtop's
# in_shared_region() the arm64 case the published source lacks (it aborts
# on CPU_TYPE_ARM64 tasks).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
CFT=""; IOK=""; NC=""; UTIL=""
for d in "${DEPS[@]}"; do
	[ -d "$d/usr/local/frameworks/CoreFoundation.framework" ] && CFT="$d"
	[ -d "$d/usr/local/frameworks/IOKit.framework" ] && IOK="$d"
	[ -f "$d/usr/lib/libpanel.5.4.dylib" ] && NC="$d"
	[ -f "$d/usr/lib/libutil.dylib" ] && UTIL="$d"
done
[ -n "$CFT" ] && [ -n "$IOK" ] && [ -n "$NC" ] && [ -n "$UTIL" ] ||
	{ echo "top/build.sh: pass the CoreFoundation, IOKit, ncurses (with libpanel) and libutil trees" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$(stage_src "$T" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$T"
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -DTOP_ANONYMOUS_MEMORY -iframework "$IOK/usr/local/frameworks" \
	-iframework "$CFT/usr/local/frameworks" $(cmd_sysroot_flags "$SYSROOT") -isystem "$NC/usr/local/include" \
	-isystem "$UTIL/usr/local/include" -ffile-prefix-map="$T/"=top/
tool "$B" "$ROOT" "$OUT/usr/bin/top" "$B/cflags" libtop.c command.c cpu.c csw.c faults.c generic.c globalstats.c \
	layout.c log.c main.c memstats.c messages.c options.c pgrp.c pid.c ports.c ppid.c preferences.c pstate.c \
	statistic.c syscalls.c threads.c timestat.c top.c uid.c uinteger.c user.c userinput.c userinput_mode.c \
	userinput_order.c userinput_sleep.c userinput_user.c workqueue.c userinput_signal.c logging.c \
	userinput_secondary_order.c sig.c userinput_help.c power.c -- \
	"$CFT/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation" \
	"$IOK/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit" -L"$NC/usr/lib" -lncurses -lpanel \
	-L"$UTIL/usr/lib" -lutil
