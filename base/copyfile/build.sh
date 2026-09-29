#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libcopyfile from copyfile-230.0.1.0.1 (docs/base/libsystem.md): replays
# copyfile.xcodeproj's copyfile target with xcodescripts/copyfile.xcconfig.
#   build.sh OUT COPYFILE_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, malloc, libc, blocks, asl, info,
#                                                   libdispatch, llvm_runtimes)
# OUT receives usr/lib/system/libcopyfile.dylib.
# The project links libquarantine (closed); patch 0001 lets it build without
# it, with the no-op quarantine calls of the iOS build (COPYFILE_NO_QUARANTINE).
# libxpc (xattr_flags.c calls _xpc_runtime_is_app_sandboxed) is the stand-in
# (DEPROOT standin_libs). libdyld isn't built yet and links through the host
# SDK's .tbd stub, which carries Apple's install name; the dylib is relinked
# when NeoDarwin's exists.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; C="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
C="$(stage_src "$C" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$C"
D="$B/derived"; mkdir -p "$D/frameworks"

# copyfile.c includes <Kernel/sys/decmpfs.h>, a private header of xnu's
# Kernel.framework. Only that framework is put on a framework path of this
# build's own; the sysroot's framework directory holds System.framework alone.
ln -s "$SYSROOT/System/Library/Frameworks/Kernel.framework" "$D/frameworks/Kernel.framework"

# The target's GCC_PREPROCESSOR_DEFINITIONS (upstream spells it
# __DARWIN_NOW_CANCELABLE, which no header reads), OTHER_CFLAGS and
# GCC_NO_COMMON_BLOCKS; SDKROOT = macosx.internal is the sysroot. Warning
# flags are left out: they change no interface.
flags=("${TARGET_FLAGS[@]}" -Os -fno-common -ftrivial-auto-var-init=uninitialized -D__DARWIN_NOW_CANCELABLE=1
	-DCOPYFILE_NO_QUARANTINE -I"$C" $(sysroot_flags "$SYSROOT") -iframework "$D/frameworks")
write_rsp "$B/cflags" "${flags[@]}"
# VERSIONING_SYSTEM = apple-generic (BSD.xcconfig) with copyfile.xcconfig's
# VERSION_INFO_PREFIX, which hides the version symbols.
write_vers "$D/copyfile_vers.c" copyfile copyfile 230.0.1.0.1 '__attribute__((visibility("hidden"))) '
compile "$B/obj" "$B/cflags" copyfile.c xattr_flags.c "$D/copyfile_vers.c"

# The target's OTHER_LDFLAGS, without -lquarantine.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libcopyfile.dylib \
	-current_version 230.0.1.0.1 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_malloc -lsystem_c \
	-lsystem_blocks -lsystem_asl -lsystem_info -ldispatch -lxpc -L"$SDK/usr/lib/system" -ldyld \
	-o "$OUT/usr/lib/system/libcopyfile.dylib"
