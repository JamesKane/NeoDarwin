#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_kernel from xnu's libsyscall/ (docs/base/libsystem.md): replays
# Libsyscall.xcodeproj's Syscalls, MIG headers, Libsyscall_static and
# Libsyscall_dynamic targets with Libsyscall.xcconfig's settings.
#   build.sh OUT XNU_SRC SYSROOT
# OUT receives usr/lib/system/libsystem_kernel.dylib and, for dyld,
# usr/local/lib/dyld/libsystem_kernel.a.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; XNU="$(abspath "$2")"; SYSROOT="$(abspath "$3")"
LIST="$(cd "$(dirname "$0")" && pwd)"
L="$XNU/libsyscall"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT

# Syscalls: stubs from bsd/kern/syscalls.master.
mkdir -p "$B/sys"
ARCHS=arm64 perl "$L/xcodescripts/create-syscalls.pl" "$XNU/bsd/kern/syscalls.master" "$L/custom" "$L/Platforms" MacOSX "$B/sys" > /dev/null
# MIG headers, internal ones included, by Apple's script.
(export SRCROOT="$L" OBJROOT="$B/migobj" BUILT_PRODUCTS_DIR="$B" SDKROOT="$SDK" ARCHS=arm64 PLATFORM_NAME=macosx DSTROOT="$B/dst"
 mkdir -p "$OBJROOT"; cd "$OBJROOT"; bash "$L/xcodescripts/mach_install_mig.sh") > /dev/null

# Libsyscall.xcconfig: OTHER_CFLAGS, GCC_PREPROCESSOR_DEFINITIONS, HEADER_SEARCH_PATHS, and
# CLANG_WARN_INT_CONVERSION = NO (an error by default in current clang).
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -Wno-int-conversion -fdollars-in-identifiers -fno-common -fno-stack-protector -fno-stack-check
	-fno-builtin-calloc -momit-leaf-frame-pointer -DLIBSYSCALL_INTERFACE -D__DARWIN_VERS_1050=1 -DNO_SYSCALL_LEGACY
	-DCF_OPEN_SOURCE -DCF_EXCLUDE_CSTD_HEADERS -DDEBUG -D_FORTIFY_SOURCE=0
	-I"$L/mach" -I"$L/os" -I"$L/wrappers" -I"$L/wrappers/string" -I"$L/wrappers/libproc" -I"$L/wrappers/libproc/spawn"
	-I"$B/internal_hdr/include" -I"$B/mig_hdr/local/include" -I"$B/mig_hdr/include" -I"$B/mig" -I"$L/wrappers/spawn"
	$(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"

# MIG user stubs (and exc's server), as Xcode's .defs rule runs them with
# OTHER_MIGFLAGS: the sysroot's header directories, in search order.
mig_includes=(-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
mkdir -p "$B/mig"; srcs=()
while read -r src attrs; do
	case "$src" in ''|'#'*) continue ;; esac
	if [ "${src%.defs}" != "$src" ]; then
		n="$(basename "${src%.defs}")"; server=/dev/null; [ -n "$attrs" ] && server="$B/mig/${n}Server.c"
		(cd "$B/mig" && xcrun mig -novouchers "${mig_includes[@]}" \
			-DKOBJECT_SERVER -arch arm64 -header "$n.h" -user "${n}User.c" -server "$server" "$L/$src") > /dev/null
		srcs+=("$B/mig/${n}User.c"); [ -n "$attrs" ] && srcs+=("$server")
	else
		srcs+=("$L/$src")
	fi
done < "$LIST/sources.static"
while read -r s; do srcs+=("$B/sys/$s"); done < <(sed 's|.*/||' "$B/sys/stubs.list")
compile "$B/static" "$B/cflags" "${srcs[@]}"
xcrun libtool -static -o "$B/libsystem_kernel.a" "$B"/static/*.o 2>/dev/null

# Libsyscall_dynamic: -all_load of the static library, its own sources and the version symbols.
dsrcs=(); while read -r s; do case "$s" in ''|'#'*) ;; *) dsrcs+=("$L/$s") ;; esac; done < "$LIST/sources.dynamic"
write_vers "$B/kernel_vers.c" kernel Libsyscall 12377.1.9 ___; dsrcs+=("$B/kernel_vers.c")
compile "$B/dynamic" "$B/cflags" "${dsrcs[@]}"
mkdir -p "$OUT/usr/lib/system" "$OUT/usr/local/lib/dyld"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_kernel.dylib \
	-current_version 12377.1.9 -compatibility_version 1 -Wl,-umbrella,System -Wl,-all_load "$B/libsystem_kernel.a" \
	"$B"/dynamic/*.o -o "$OUT/usr/lib/system/libsystem_kernel.dylib"
cp "$B/libsystem_kernel.a" "$OUT/usr/local/lib/dyld/libsystem_kernel.a"
