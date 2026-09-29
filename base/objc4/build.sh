#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libobjc.A from objc4-950 (docs/base/libsystem.md): replays objc.xcodeproj's
# objc target (Release) and objc-trampolines target with the project-level
# settings (the target's objc.xcconfig only routes the macOS-public extra
# headers). Plain arm64 macOS, which Apple doesn't build: patch 0001 gives it
# the 52-bit isa class field. The sysroot carries the other projects' private
# headers it includes (libsystem_darwin's os/*.h, dyld's objc-shared-cache.h,
# <System/pthread_machdep.h>) and base/sdk's CrashReporterClient.h and
# sandbox/private.h.
#   build.sh OUT OBJC4_SRC SYSROOT DEPROOT...   (DEPROOT: libSystem, once built)
# OUT receives usr/lib/libobjc.A.dylib, its libobjc.dylib link, and
# usr/lib/libobjc-trampolines.dylib, which the runtime dlopens for
# imp_implementationWithBlock.
#
# libobjc links libSystem.B as a whole, as Apple's does (it sits outside the
# umbrella). libSystem, libc++abi and libc++ aren't built yet; until they are,
# the dylibs link them through the host SDK's .tbd stubs, which carry Apple's
# install names, and are relinked when NeoDarwin's exist (a DEPROOT holding
# usr/lib/libSystem.dylib is searched first). What libobjc takes from libSystem
# resolves at run time in the libraries under it, the libsystem_sandbox
# stand-in included (sandbox_check).
#
# Left out of the objc target: libCrashReporterClient.a (base/sdk's
# CrashReporterClient.h is header-only), libRosetta and the Rosetta hooks
# (OBJC_USE_ROSETTA=0, patch 0002), libobjc-env.dylib (a weak link for the test
# harness that nothing in the library uses), link-time optimization and the
# warning set (neither changes an interface), and -delay-lswiftCore (see the link).
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; O="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
O="$(stage_src "$O" "$B/src" "$(cd "$(dirname "$0")" && pwd)/patches")"   # patches/ applied
cd "$O"

# The objc target's sources phase, arm64 macOS: the other architectures'
# messengers and dummy-library-mac-i386.c are excluded by architecture.
# objc-lockdebug.mm's per-file -Os is the Release default already.
mm=(runtime/hashtable2.mm runtime/maptable.mm runtime/objc-cache.mm runtime/objc-class.mm runtime/objc-errors.mm
	runtime/objc-file.mm runtime/objc-initialize.mm runtime/objc-layout.mm runtime/objc-load.mm runtime/objc-loadmethod.mm
	runtime/objc-lockdebug.mm runtime/objc-runtime-new.mm runtime/objc-runtime.mm runtime/objc-sel.mm runtime/objc-sync.mm
	runtime/objc-typeencoding.mm runtime/Object.mm runtime/Protocol.mm runtime/objc-accessors.mm runtime/objc-references.mm
	runtime/objc-os.mm runtime/objc-block-trampolines.mm runtime/objc-weak.mm runtime/NSObject.mm runtime/objc-opt.mm
	runtime/objc-zalloc.mm)
mm_exc=(runtime/objc-auto.mm runtime/objc-exception.mm)   # COMPILER_FLAGS = -fexceptions
m=(runtime/objc-magicsel.m)
s=(runtime/retain-release-helpers-arm64.s runtime/objc-sel-table.s runtime/Messengers.subproj/objc-msg-arm64.s)

# The DTrace provider header (objc-probes.d is in the sources phase).
G="$B/gen"; mkdir -p "$G"; xcrun dtrace -h -s runtime/objc-probes.d -o "$G/objc-probes.h"
# The headers phase's Public and Private headers and the macOS-public extra
# headers, as Xcode installs them into the build products the sources search
# (<objc/...>): through unifdef -k -DBUILD_FOR_OSX.
H="$B/hdr/objc"; mkdir -p "$H"
for h in message.h objc-api.h objc-auto.h objc-exception.h objc-sync.h objc.h runtime.h NSObjCRuntime.h NSObject.h \
	maptable.h objc-abi.h objc-gdb.h objc-internal.h NSObject-internal.h objc-block-trampolines.h \
	OldClasses.subproj/List.h Object.h Protocol.h hashtable.h hashtable2.h objc-class.h objc-load.h objc-runtime.h; do
	unifdef -k -DBUILD_FOR_OSX -o "$H/$(basename "$h")" "runtime/$h" || [ $? -eq 1 ]; done

# The project's Release settings: GCC_SYMBOLS_PRIVATE_EXTERN,
# GCC_INLINES_ARE_PRIVATE_EXTERN, GCC_NO_COMMON_BLOCKS, ENABLE_STRICT_OBJC_MSGSEND,
# GCC_PREPROCESSOR_DEFINITIONS, OTHER_CFLAGS (project and target),
# OTHER_CPLUSPLUSFLAGS, C/C++ standards, no C++ exceptions or RTTI.
# NeoDarwin: OBJC_USE_ROSETTA=0 (patch 0002).
common=("${TARGET_FLAGS[@]}" -Os -fvisibility=hidden -fno-common -momit-leaf-frame-pointer -fdollars-in-identifiers
	-fno-objc-convert-messages-to-runtime-calls -fno-objc-msgsend-selector-stubs -DOBJC_OLD_DISPATCH_PROTOTYPES=0
	-DOS_OBJECT_USE_OBJC=0 -DNDEBUG=1 -DOBJC_USE_ROSETTA=0 -I"$O/runtime" -I"$G" -I"$B/hdr")
cxx=(-std=gnu++20 -fno-exceptions -fno-rtti -fvisibility-inlines-hidden -D_LIBCPP_VISIBLE= -fno-typed-cxx-new-delete
	-nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1")   # libc++ ahead of the C headers, as clang orders them itself
write_rsp "$B/mm.rsp" "${common[@]}" "${cxx[@]}" $(sysroot_flags "$SYSROOT")
write_rsp "$B/mm_exc.rsp" "${common[@]}" "${cxx[@]}" -fexceptions $(sysroot_flags "$SYSROOT")
write_rsp "$B/c.rsp" "${common[@]}" -std=gnu17 $(sysroot_flags "$SYSROOT")
compile "$B/obj" "$B/mm.rsp" "${mm[@]}"
compile "$B/obj" "$B/mm_exc.rsp" "${mm_exc[@]}"
compile "$B/obj" "$B/c.rsp" "${m[@]}" "${s[@]}"
# objc-trampolines' sources phase (its OTHER_CFLAGS: -fdollars-in-identifiers).
write_rsp "$B/tramp.rsp" "${TARGET_FLAGS[@]}" -Os -fdollars-in-identifiers -I"$O/runtime" -I"$B/hdr" $(sysroot_flags "$SYSROOT")
compile "$B/tramp" "$B/tramp.rsp" runtime/objc-blocktramps-arm64.s

# The objc target's link: OTHER_LDFLAGS (all SDKs and macOS), UNEXPORTED_SYMBOLS_FILE,
# CLANG_CXX_LIBRARY = libc++. Swift: Apple delay-links libswiftCore
# (-delay-lswiftCore) for swift_retain/swift_release, which the runtime calls
# only for objects of Swift classes. NeoDarwin has no Swift userland, so
# libswiftCore is weak-linked instead: its two symbols bind to NULL while the
# dylib is absent, and nothing calls them without Swift objects.
mkdir -p "$OUT/usr/lib"
libdirs=($(dep_libdirs ${DEPS[@]+"${DEPS[@]}"}) -L"$SDK/usr/lib")
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/libobjc.A.dylib \
	-current_version 228 -compatibility_version 1 "$B"/obj/*.o "${libdirs[@]}" -lSystem -lc++abi -lc++ \
	-L"$SDK/usr/lib/swift" -Wl,-weak-lswiftCore -Wl,-unexported_symbols_list,"$O/unexported_symbols" \
	-Wl,-init_offsets -Wl,-sectalign,__DATA,__objc_data,0x1000 -o "$B/libobjc.A.dylib"
# Run Script (markgc): renames the initializer sections so dyld leaves them to
# libobjc's own _objc_init, and fails a Release build that has any. A host tool;
# the rewrite invalidates the linker's ad-hoc signature, which is redone.
xcrun clang++ -std=c++11 -Os "$O/markgc.cpp" -o "$B/markgc"
"$B/markgc" "$B/libobjc.A.dylib" Release NO > /dev/null
codesign -f -s - "$B/libobjc.A.dylib" 2>/dev/null
cp "$B/libobjc.A.dylib" "$OUT/usr/lib/"
ln -sfh libobjc.A.dylib "$OUT/usr/lib/libobjc.dylib"   # Run Script (symlink)

# objc-trampolines' link (OTHER_LDFLAGS).
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/libobjc-trampolines.dylib \
	-current_version 228 -compatibility_version 1 "$B"/tramp/*.o "${libdirs[@]}" -lSystem \
	-Wl,-not_for_dyld_shared_cache -o "$OUT/usr/lib/libobjc-trampolines.dylib"
