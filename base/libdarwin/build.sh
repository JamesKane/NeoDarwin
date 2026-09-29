#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_darwin from Libc-1725.0.11 (docs/base/libsystem.md): replays
# Libc.xcodeproj's libsystem_darwin.dylib target (libdarwin) with libc.xcconfig.
# The target's source list is in sources/, taken from the project; its files
# carry no per-file COMPILER_FLAGS. The Copy AppleFooVariant.plists phase
# installs data, not code, and is left out.
#   build.sh OUT LIBC_SRC SYSROOT DEPROOT...
#   (DEPROOT: kernel, platform, pthread, malloc, libc, blocks, libdispatch, libmacho, libsystem_m, llvm_runtimes,
#   standin_libs)
# OUT receives usr/lib/system/libsystem_darwin.dylib.
# libxpc and libsystem_trace are closed and link NeoDarwin's stand-ins (DEPROOT
# standin_libs). libdyld isn't built yet and links through the host SDK's .tbd
# stub, which carries Apple's install name.
# Apple's apfs/apfs_fsctl.h, os/transaction_private.h and the declaration of
# _os_xbs_chrooted are unpublished: patches/ 0001-0003.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$L"
D="$B/derived"; mkdir -p "$D/dtrace"

# libc.xcconfig (BASE_PREPROCESSOR_MACROS, OTHER_CFLAGS, SRCROOT_SEARCH_PATHS,
# GCC_TREAT_IMPLICIT_FUNCTION_DECLARATIONS_AS_ERRORS) and the target's own
# GCC_PREPROCESSOR_DEFINITIONS and HEADER_SEARCH_PATHS (libdarwin, libdarwin/h,
# then SDKROOT/usr/local/include, which sysroot_flags supplies).
# WARNING_CFLAGS' -Werror is left out: diagnostics change no interface.
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fdollars-in-identifiers -fno-common -fverbose-asm
	-Werror=implicit-function-declaration -D__LIBC__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1
	-D__DARWIN_NON_CANCELABLE=1 -D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0
	-D_LIBC_NO_FEATURE_VERIFICATION=1 -DDARWIN_BUILDING_LIBSYSTEM_DARWIN=1 -DOS_CRASH_ENABLE_EXPERIMENTAL_LIBTRACE
	-I"$D/dtrace" -I"$L" -I"$L/include" -I"$L/gen" -I"$L/locale" -I"$L/locale/FreeBSD" -I"$L/stdtime/FreeBSD"
	-I"$L/darwin" -I"$L/libdarwin" -I"$L/libdarwin/h" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
# VERSIONING_SYSTEM = apple-generic, PRODUCT_NAME = darwin, and libc.xcconfig's
# VERSION_INFO_PREFIX, which keeps the version symbols hidden.
write_vers "$D/darwin_vers.c" darwin Libc 1725.0.11 '__attribute__((visibility("hidden"))) '
srcs=(); while IFS= read -r line; do srcs+=("${line%%	*}"); done < <(grep -v '^#' "$PROJ/sources/libsystem_darwin.dylib.txt")
compile "$B/obj" "$B/cflags" "${srcs[@]}" "$D/darwin_vers.c"

# libc.xcconfig's LIBSYSTEM_DARWIN_LDFLAGS (its -all_load has no archives to act on).
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_darwin.dylib \
	-current_version 1725.0.11 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_m -lsystem_malloc -lsystem_blocks \
	-lsystem_platform -lsystem_pthread -lsystem_c -ldispatch -lmacho \
	-lxpc -Wl,-upward-lsystem_trace -L"$SDK/usr/lib/system" -ldyld \
	-o "$OUT/usr/lib/system/libsystem_darwin.dylib"
