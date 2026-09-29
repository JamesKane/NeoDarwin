#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The LLVM runtimes from swiftlang/llvm-project swift-6.2-RELEASE (LLVM 19.1.5,
# the branch Xcode 26 builds from; docs/base/libsystem.md):
#   libcompiler_rt  compiler-rt's builtins, as the dylib macOS has;
#   libunwind       libunwind;
#   libc++abi       libcxxabi;
#   libc++          libcxx, re-exporting libc++abi's interface.
# The compile and link flags are those the runtimes' CMake produces for arm64
# Darwin with libcxx/cmake/caches/Apple.cmake (Apple's own configuration of
# libc++ and libc++abi), replayed here without CMake; sources/*.txt list each
# target's files. Link dependencies follow Apple's images (dyld_info on macOS).
#   build.sh OUT LLVM_PROJECT_SRC SYSROOT DEPROOT...
#   (DEPROOT: kernel, platform, pthread, malloc, c)
# OUT receives usr/lib/system/libcompiler_rt.dylib, usr/lib/system/libunwind.dylib,
# usr/lib/libc++abi.dylib, usr/lib/libc++.1.dylib and the libc++.dylib link.
# Headers go through the sysroot instead (install_headers.sh).
# libdyld, libsystem_m and libSystem.B aren't built yet; until they are, the
# dylibs link them through the host SDK's .tbd stubs, which carry Apple's
# install names, and are relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ME="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$ME/patches")"   # patches/ applied
cd "$L"
srcs() { grep -v '^#' "$ME/sources/$1.txt"; }

# libc++'s headers as installed (generated __config_site included). C++ sees
# them ahead of the sysroot's C headers, which they wrap with #include_next;
# libunwind's unwind.h serves the C sources that include it.
"$ME/install_headers.sh" "$B/hdr" "$L"
V1="$B/hdr/usr/include/c++/v1"
LLVM_DEFS=(-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)
SYS=($(sysroot_flags "$SYSROOT") -isystem "$B/hdr/usr/include")
LINK=("${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -compatibility_version 1)
# The runtimes' version, in Apple's numbering (LLVM major x 100).
LLVM_VERSION=1900.1.5
mkdir -p "$OUT/usr/lib/system"

# libcompiler_rt: CompilerRTDarwinUtils.cmake's builtin flags, without its
# -fvisibility=hidden -DVISIBILITY_HIDDEN (those are for the static archive;
# the dylib exports every builtin it has).
write_rsp "$B/rt.rsp" "${TARGET_FLAGS[@]}" -O3 -fPIC -Wall -fomit-frame-pointer "${SYS[@]}"
compile "$B/rt" "$B/rt.rsp" $(srcs compiler_rt)
compile "$B/rt" "$B/rt.rsp" "$ME/src/nd_chkstk_darwin.c"

# libunwind: libunwind/src/CMakeLists.txt's flags, with assertions off as in
# Apple's (its libunwind has none of the LIBUNWIND_PRINT_* tracing that
# LIBUNWIND_ENABLE_ASSERTIONS compiles in).
UNW=("${TARGET_FLAGS[@]}" "${LLVM_DEFS[@]}" -Os -DNDEBUG -funwind-tables -nostdinc++ -D_LIBUNWIND_IS_NATIVE_ONLY
	-U__STRICT_ANSI__ -fno-rtti -I"$L/libunwind/include" "${SYS[@]}")
write_rsp "$B/unw_cxx.rsp" "${UNW[@]}" -std=c++17 -fstrict-aliasing -fno-exceptions -fvisibility-inlines-hidden
write_rsp "$B/unw_c.rsp" "${UNW[@]}" -std=c99
write_rsp "$B/unw_s.rsp" "${UNW[@]}"
compile "$B/unw" "$B/unw_cxx.rsp" $(srcs libunwind | grep '\.cpp$')
compile "$B/unw" "$B/unw_c.rsp" $(srcs libunwind | grep '\.c$')
compile "$B/unw" "$B/unw_s.rsp" $(srcs libunwind | grep '\.S$')

# libc++abi and libc++: libcxxabi/src and libcxx/src CMakeLists.txt's flags
# under Apple.cmake (forgiving dynamic_cast; libc++ hidden by default and
# exported by its visibility annotations).
CXX=("${TARGET_FLAGS[@]}" "${LLVM_DEFS[@]}" -Os -DNDEBUG -std=c++23 -nostdinc++ -fvisibility-inlines-hidden
	-DLIBCXX_BUILDING_LIBCXXABI -D_LIBCPP_BUILDING_LIBRARY -D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
write_rsp "$B/abi.rsp" "${CXX[@]}" -D_LIBCXXABI_BUILDING_LIBRARY -D_LIBCXXABI_FORGIVING_DYNAMIC_CAST \
	-fstrict-aliasing -funwind-tables -I"$L/libcxx/src" -I"$V1" -I"$L/libcxxabi/include" "${SYS[@]}"
compile "$B/abi" "$B/abi.rsp" $(srcs libcxxabi)
write_rsp "$B/cxx.rsp" "${CXX[@]}" -D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES -fPIC -faligned-allocation \
	-fvisibility=hidden -fsized-deallocation -I"$L/libcxx/src" -I"$V1" -I"$L/libcxxabi/include" "${SYS[@]}"
compile "$B/cxx" "$B/cxx.rsp" $(srcs libcxx)

# libunwind and libcompiler_rt link each other upward. libunwind links first,
# against the SDK's libcompiler_rt stub, then again against NeoDarwin's.
link_unwind() {
	xcrun clang "${LINK[@]}" -install_name /usr/lib/system/libunwind.dylib -current_version "$LLVM_VERSION" \
		-Wl,-umbrella,System "$B"/unw/*.o $(dep_libdirs "${DEPS[@]}") -lsystem_malloc -lsystem_c \
		-lsystem_pthread -lsystem_platform "$@" -o "$OUT/usr/lib/system/libunwind.dylib"
}
# libdyld: host SDK stub (_dyld_find_unwind_sections, dladdr).
link_unwind -L"$SDK/usr/lib/system" -ldyld -Wl,-upward-lcompiler_rt
xcrun clang "${LINK[@]}" -install_name /usr/lib/system/libcompiler_rt.dylib -current_version 103 \
	-Wl,-umbrella,System "$B"/rt/*.o -L"$OUT/usr/lib/system" $(dep_libdirs "${DEPS[@]}") -Wl,-upward-lunwind \
	-Wl,-upward-lsystem_c -Wl,-upward-lsystem_pthread -Wl,-upward-lsystem_kernel -Wl,-upward-lsystem_platform \
	-L"$SDK/usr/lib/system" -Wl,-upward-lsystem_m -ldyld -o "$OUT/usr/lib/system/libcompiler_rt.dylib"
link_unwind -L"$OUT/usr/lib/system" -Wl,-upward-lcompiler_rt -L"$SDK/usr/lib/system" -ldyld

# libc++abi exports the symbols of libcxxabi/lib/*.exp that CMake selects on
# arm64; libc++ re-exports all but symbols-not-reexported.exp. Both link
# libSystem, as Apple's do (host SDK stub).
EXP=(cxxabiv1 fundamental-types itanium-base std-misc new-delete std-exceptions itanium-exceptions personality-v0)
xcrun clang "${LINK[@]}" -install_name /usr/lib/libc++abi.dylib -current_version "$LLVM_VERSION" "$B"/abi/*.o \
	-Wl,-exported_symbols_list,"$L/libcxxabi/lib/symbols-not-reexported.exp" \
	$(for e in "${EXP[@]}"; do printf -- '-Wl,-exported_symbols_list,%s\n' "$L/libcxxabi/lib/$e.exp"; done) \
	-lSystem -o "$OUT/usr/lib/libc++abi.dylib"
xcrun clang "${LINK[@]}" -install_name /usr/lib/libc++.1.dylib -current_version "$LLVM_VERSION" "$B"/cxx/*.o \
	-Wl,-unexported_symbols_list,"$L/libcxx/lib/libc++unexp.exp" \
	-Wl,-force_symbols_not_weak_list,"$L/libcxx/lib/notweak.exp" -Wl,-force_symbols_weak_list,"$L/libcxx/lib/weak.exp" \
	"$OUT/usr/lib/libc++abi.dylib" \
	$(for e in "${EXP[@]}"; do printf -- '-Wl,-reexported_symbols_list,%s\n' "$L/libcxxabi/lib/$e.exp"; done) \
	-lSystem -o "$OUT/usr/lib/libc++.1.dylib"
ln -sf libc++.1.dylib "$OUT/usr/lib/libc++.dylib"
