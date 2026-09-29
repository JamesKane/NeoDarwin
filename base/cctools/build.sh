#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libmacho from cctools-1035.1.102 (docs/base/libsystem.md): replays
# cctools.xcodeproj's "macho dynamic" target with xcode/macho_dynamic.xcconfig.
# cctools ships with the toolchain, not in the macOS release set; macOS 26's
# libmacho is version 1040, and 1035.1.102 is the newest Apple published.
#   build.sh OUT CCTOOLS_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, malloc, c)
# OUT receives usr/lib/system/libmacho.dylib.
# libdyld and libcompiler_rt aren't built yet; until they are, the dylib links
# them through the host SDK's .tbd stubs, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; C="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
cd "$C"

# The target's headers (include/mach-o) come first, as in the project.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fapplication-extension -I"$C/include" $(sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/cflags" libmacho/arch.c libmacho/get_end.c libmacho/getsecbyname.c libmacho/getsegbyname.c \
	libmacho/i386_swap.c libmacho/slot_name.c libmacho/swap.c

mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nodefaultlibs -install_name /usr/lib/system/libmacho.dylib \
	-current_version 1035.1.102 -compatibility_version 1 -Wl,-umbrella,System -Wl,-application_extension "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -Wl,-upward-lsystem_malloc -Wl,-upward-lsystem_c -Wl,-upward-lsystem_kernel \
	-L"$SDK/usr/lib/system" -Wl,-upward-lcompiler_rt -ldyld -o "$OUT/usr/lib/system/libmacho.dylib"
