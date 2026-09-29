#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_collections from Libc-1725.0.11 (docs/base/libsystem.md): replays
# Libc.xcodeproj's libsystem_collections target with collections.xcconfig. The
# target's source list is in sources/, taken from the project; its files carry
# no per-file COMPILER_FLAGS.
#   build.sh OUT LIBC_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, malloc, libc, blocks, llvm_runtimes)
# OUT receives usr/lib/system/libsystem_collections.dylib.
# libdyld isn't built yet; until it is, it links through the host SDK's .tbd
# stub, which carries Apple's install name.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; L="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
L="$(stage_src "$L" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$L"
D="$B/derived"; mkdir -p "$D"

# collections.xcconfig: GCC_OPTIMIZATION_LEVEL, GCC_C_LANGUAGE_STANDARD,
# OTHER_CFLAGS, GCC_SYMBOLS_PRIVATE_EXTERN (-fvisibility=hidden),
# GCC_NO_COMMON_BLOCKS, GCC_ENABLE_CPP_EXCEPTIONS, and HEADER_SEARCH_PATHS
# (System.framework's PrivateHeaders, then the project; the sysroot's usr/local/include
# holds the target's own os/collections_*.h). GCC_TREAT_WARNINGS_AS_ERRORS is
# left out: diagnostics change no interface.
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fverbose-asm -fvisibility=hidden -fno-common -fno-exceptions
	-I"$L" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
# VERSIONING_SYSTEM = apple-generic, PRODUCT_NAME = collections,
# VERSION_INFO_PREFIX = __; hidden, as the rest of the target.
write_vers "$D/collections_vers.c" collections Libc 1725.0.11 __
srcs=(); while IFS= read -r line; do srcs+=("${line%%	*}"); done < <(grep -v '^#' "$PROJ/sources/libsystem_collections.txt")
compile "$B/obj" "$B/cflags" "${srcs[@]}" "$D/collections_vers.c"

# collections.xcconfig's OTHER_LDFLAGS (LINK_WITH_STANDARD_LIBRARIES = NO).
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_collections.dylib \
	-current_version 1725.0.11 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_malloc -lsystem_c -lsystem_blocks \
	-L"$SDK/usr/lib/system" -ldyld -o "$OUT/usr/lib/system/libsystem_collections.dylib"
