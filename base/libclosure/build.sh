#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_blocks from libclosure-96 (docs/base/libsystem.md): replays
# Blocks.xcodeproj's Blocks-dynamic target with Blocks.xcconfig's macOS
# settings (HAVE_OBJC=1 HAVE_UNWIND=1, data.m's Objective-C block classes,
# -upward-lobjc, -lunwind).
#   build.sh OUT LIBCLOSURE_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, malloc, ...)
# OUT receives usr/lib/system/libsystem_blocks.dylib.
# libobjc, libunwind, libdyld and libcompiler_rt aren't built yet; until they
# are, the dylib links them through the host SDK's .tbd stubs, which carry
# Apple's install names, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; C="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$C"

# libc++ must precede the C headers for runtime.cpp, as clang orders them
# itself when no sysroot headers are added ahead of the SDK's.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fexceptions -DHAVE_OBJC=1 -DHAVE_UNWIND=1 -I"$C" \
	-isystem "$SDK/usr/include/c++/v1" $(sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" runtime.cpp data.c data.m generic_helpers.c

mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_blocks.dylib \
	-current_version 96 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_platform -lsystem_malloc -L"$SDK/usr/lib/system" -L"$SDK/usr/lib" \
	-Wl,-upward-lobjc -Wl,-alias_list,"$C/Blocks.alias" -Wl,-upward-lsystem_c -ldyld -lunwind -lcompiler_rt \
	-o "$OUT/usr/lib/system/libsystem_blocks.dylib"
