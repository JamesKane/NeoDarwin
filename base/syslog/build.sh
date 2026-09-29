#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_asl from syslog-404 (docs/base/libsystem.md): replays
# syslog.xcodeproj's libsystem_asl target with libasl.xcconfig and the
# project-level settings (GCC_C_LANGUAGE_STANDARD = gnu99, WARNING_CFLAGS =
# -Werror=format, HEADER_SEARCH_PATHS: the MIG output directory,
# libsystem_asl.tproj/include, aslcommon; the target adds System.framework's
# PrivateHeaders, which the sysroot flags already carry). No file has per-file
# COMPILER_FLAGS. asl_ipc.defs is in the sources phase with no attributes, so
# Xcode generates its client side only. The daemons and tools (syslogd,
# aslmanager, syslog, newsyslog) and the aslcommon archive are not built.
#   build.sh OUT SYSLOG_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, pthread, malloc, libc, blocks)
# OUT receives usr/lib/system/libsystem_asl.dylib.
# libasl.xcconfig's link line: -lCrashReporterClient is left out (nothing in
# the library uses it, and it is an internal-SDK archive). libdyld,
# libcompiler_rt, libunwind, libdispatch, libxpc, libsystem_trace,
# libsystem_notify, libsystem_darwin and the upward libsystem_info aren't built
# yet; until they are, the dylib links them through the host SDK's .tbd stubs,
# which carry Apple's install names, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$(cd "$(dirname "$0")" && pwd)/patches")"   # patches/ applied
cd "$S"

# The sources phase, in the project's order.
srcs=(asl_mt_shim.c asl_client.c asl.c asl_core.c asl_string.c asl_fd.c asl_msg_list.c asl_file.c asl_legacy1.c
	asl_msg.c asl_store.c asl_util.c asl_object.c syslog.c)
srcs=("${srcs[@]/#/libsystem_asl.tproj/src/}")

# asl_ipc.defs, client side, as Xcode's .defs rule runs it: the sysroot's
# header directories in search order.
mig_includes=(-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
mkdir -p "$B/mig"
(cd "$B/mig" && xcrun mig "${mig_includes[@]}" -arch arm64 -header asl_ipc.h -user asl_ipcUser.c \
	-server /dev/null -sheader /dev/null "$S/aslcommon/asl_ipc.defs") > /dev/null
srcs+=("$B/mig/asl_ipcUser.c")

flags=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -Werror=format -I"$B/mig" -I"$S/libsystem_asl.tproj/include"
	-I"$S/aslcommon" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
compile "$B/obj" "$B/cflags" "${srcs[@]}"

mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_asl.dylib \
	-current_version 404 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_platform -lsystem_pthread -lsystem_malloc -lsystem_c \
	-lsystem_blocks -L"$SDK/usr/lib/system" -lcompiler_rt -ldyld -lunwind -ldispatch -lxpc -lsystem_trace \
	-lsystem_notify -lsystem_darwin -Wl,-upward-lsystem_info \
	-o "$OUT/usr/lib/system/libsystem_asl.dylib"
