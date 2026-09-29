#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libremovefile from removefile-84 (docs/base/libsystem.md): replays
# removefile.xcodeproj's removefile target with xcodescripts/removefile.xcconfig.
#   build.sh OUT REMOVEFILE_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, malloc, libc, llvm_runtimes)
# OUT receives usr/lib/system/libremovefile.dylib.
# REMOVEFILE_CLEAR_PURGEABLE uses APFS's private fsctl header (closed); patch
# 0001 lets the build leave it out, as the simulator build does
# (REMOVEFILE_NO_APFS).
# libdyld isn't built yet; until it is, the dylib links it through the host
# SDK's .tbd stub, which carries Apple's install name, and is relinked when
# NeoDarwin's exists.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; RF="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
RF="$(stage_src "$RF" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$RF"
D="$B/derived"; mkdir -p "$D"

# The target's GCC_PREPROCESSOR_DEFINITIONS. WARNING_CFLAGS (-Wall) with
# GCC_TREAT_WARNINGS_AS_ERRORS are left out: diagnostics change no interface.
flags=("${TARGET_FLAGS[@]}" -Os -fno-common -D__DARWIN_NON_CANCELABLE=1 -DREMOVEFILE_NO_APFS
	-I"$RF" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
# VERSIONING_SYSTEM = apple-generic (BSD.xcconfig) with the target's
# VERSION_INFO_PREFIX, which hides the version symbols.
write_vers "$D/removefile_vers.c" removefile removefile 84 '__attribute__((visibility("hidden"))) '
compile "$B/obj" "$B/cflags" removefile_random.c removefile_rename_unlink.c removefile_sunlink.c \
	removefile_tree_walker.c removefile.c "$D/removefile_vers.c"

# The target's OTHER_LDFLAGS; DYLIB_CURRENT_VERSION is the project version.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libremovefile.dylib \
	-current_version 84 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_platform -lsystem_malloc -lsystem_c \
	-L"$SDK/usr/lib/system" -ldyld -o "$OUT/usr/lib/system/libremovefile.dylib"
