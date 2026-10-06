#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Apple's ld64 from source (P0-06; toolchains/ld64/README.md): replays
# ld64.xcodeproj's "ld" target (product ld-classic, Release) as a host tool.
#   build.sh OUT LD64_SRC VERSION
# OUT receives bin/ld and VERSION.txt (`ld -v`'s first line).
# The target's script phases are replayed: src/create_configure writes
# configure.h, and compile_stubs becomes compile_stubs.h. Apple-generic
# versioning gives ld_classicVersionString, which -v prints.
# Built without libtapi, libLTO, bitcode bundles and libswiftDemangle:
# nd_stubs.cpp replaces lto_file.cpp, textstub_dylib_file.cpp and
# bitcode_bundle.cpp, and compat/ stands in for the internal-SDK headers.
# Phase 1 compiles with the host Xcode's clang against the public macOS SDK;
# P0-02 switches to the pinned toolchain.
set -euo pipefail
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
OUT="$(abspath "$1")"; S="$(abspath "$2")"; VERSION="$3"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK="$(xcrun --show-sdk-path)"
JOBS="$(/usr/sbin/sysctl -n hw.ncpu)"
# __DATE__/__TIME__ (-v's BUILD line) from a fixed epoch, so the binary is reproducible.
export SOURCE_DATE_EPOCH=0
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
mkdir -p "$B/derived" "$B/obj" "$OUT/bin"

# make configure.h: Apple's defaults (every architecture, no riscv32 as in
# Xcode's toolchain); the default macOS deployment target is NeoDarwin's.
DERIVED_FILE_DIR="$B/derived" RC_ProjectSourceVersion="$VERSION" MACOSX_DEPLOYMENT_TARGET=26.0 \
	TOOLCHAIN_INSTALL_DIR=XcodeDefault.xctoolchain TOOLCHAIN_DIR="$B/no-toolchain" \
	RC_SUPPORTED_ARCHS= IPHONEOS_DEPLOYMENT_TARGET= /bin/bash "$S/src/create_configure"
# make compile_stub string (the target's shell phase, verbatim)
{
	echo "static const char *compile_stubs = "
	sed 's/"/\\"/g' "$S/compile_stubs" | sed 's/^/"/' | sed 's/$/\\n"/'
	echo ";"
} > "$B/derived/compile_stubs.h"
# VERSIONING_SYSTEM = apple-generic, PRODUCT_NAME = ld-classic
cat > "$B/derived/ld_classic_vers.c" <<VERS
extern const unsigned char ld_classicVersionString[];
extern const double ld_classicVersionNumber;
const unsigned char ld_classicVersionString[] __attribute__ ((used)) = "@(#)PROGRAM:ld  PROJECT:ld64-${VERSION}" "\\n";
const double ld_classicVersionNumber __attribute__ ((used)) = (double)${VERSION};
VERS

# The ld target's Sources phase, minus the three nd_stubs.cpp replaces.
SRCS=(
	src/ld/passes/code_dedup.cpp src/ld/passes/thread_starts.cpp src/ld/Options.cpp
	src/ld/ResponseFiles.cpp src/ld/libcodedirectory.c src/ld/PlatformSupport.cpp src/ld/ld.cpp
	src/ld/parsers/macho_relocatable_file.cpp src/ld/parsers/archive_file.cpp
	src/ld/parsers/macho_dylib_file.cpp src/ld/debugline.c src/ld/InputFiles.cpp src/ld/SymbolTable.cpp
	src/ld/Resolver.cpp src/ld/OutputFile.cpp src/ld/passes/stubs/stubs.cpp
	src/ld/parsers/opaque_section_file.cpp src/ld/passes/dtrace_dof.cpp src/ld/passes/compact_unwind.cpp
	src/ld/passes/got.cpp src/mach_o/ExportsTrie.cpp src/ld/passes/huge.cpp src/ld/passes/inits.cpp
	src/ld/parsers/generic_dylib_file.cpp src/ld/passes/order.cpp src/ld/passes/branch_island.cpp
	src/ld/passes/objc.cpp src/ld/passes/dylibs.cpp src/ld/passes/objc_constants.cpp src/ld/passes/tlvp.cpp
	src/ld/passes/branch_shim.cpp src/ld/Snapshot.cpp src/ld/Mangling.cpp src/mach_o/Error.cpp
	src/ld/FatFile.cpp src/ld/passes/objc_stubs.cpp src/ld/code-sign-blobs/blob.cpp
)
# Xcode's header map makes every project header visible by name; compat/
# comes after them and before the SDK.
COMMON=(-arch "$(uname -m)" -isysroot "$SDK" -mmacosx-version-min=14.0 -O2 -g0 -DNDEBUG -DBUILDING_LD=1
	"-DLD_VERS=\\\"ld64-${VERSION}\\\"" -I"$B/derived" -I"$S/src/ld" -I"$S/src/ld/parsers" -I"$S/src/ld/passes"
	-I"$S/src/ld/passes/stubs" -I"$S/src/ld/code-sign-blobs" -I"$S/src/abstraction" -I"$S/src/mach_o"
	-I"$S/src/llvm" -I"$HERE/compat" -fno-common -w)   # -w: upstream's warnings are upstream's to fix
export B; export COMMON_RSP="$B/common.rsp"
printf '%s\n' "${COMMON[@]}" > "$COMMON_RSP"
cd "$S"
{ printf '%s\n' "${SRCS[@]}"; printf '%s\n' "$HERE/nd_stubs.cpp" "$B/derived/ld_classic_vers.c"; } |
	xargs -P "$JOBS" -I{} /bin/bash -c '
		set -euo pipefail; src="$1"; obj="$B/obj/$(printf "%s" "$src" | tr "/" "_").o"
		case "$src" in
		*.c) xcrun clang -std=gnu11 @"$COMMON_RSP" -c "$src" -o "$obj" ;;
		*) xcrun clang++ -std=c++20 -stdlib=libc++ @"$COMMON_RSP" -c "$src" -o "$obj" ;;
		esac' _ {}
# OTHER_LDFLAGS, minus -lxar, -ltapi and linkExtras (libswiftDemangle).
xcrun clang++ -arch "$(uname -m)" -isysroot "$SDK" -mmacosx-version-min=14.0 -stdlib=libc++ \
	-Wl,-exported_symbol,__mh_execute_header -Wl,-stack_size,0x02000000 -Wl,-client_name,ld -Wl,-dead_strip \
	"$B"/obj/*.o -o "$OUT/bin/ld"
"$OUT/bin/ld" -v 2>&1 | head -1 > "$OUT/VERSION.txt"
grep -q "PROJECT:ld64-${VERSION}\$" "$OUT/VERSION.txt" || { echo "ld64 build: -v reports '$(cat "$OUT/VERSION.txt")'" >&2; exit 1; }
