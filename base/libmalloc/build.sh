#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_malloc from libmalloc-792.1.1 (docs/base/libsystem.md): replays
# libmalloc.xcodeproj's libsystem_malloc target with libmalloc.xcconfig and
# libmalloc_common.xcconfig. The resolved variants (libmalloc_mp, _alt) are
# for iOS, tvOS and watchOS only; macOS builds this one. libmalloc has no dyld
# variant.
#   build.sh OUT LIBMALLOC_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, pthread)
# OUT receives usr/lib/system/libsystem_malloc.dylib.
# libdyld, libcompiler_rt, and the upward links (libsystem_c, libsystem_blocks,
# libcorecrypto, libsystem_featureflags) aren't built yet; until they are, the
# dylib links them through the host SDK's .tbd stubs, which carry Apple's
# install names, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
M="$(stage_src "$M" "$B/src" "$(cd "$(dirname "$0")" && pwd)/patches")"   # patches/ applied
cd "$M"

# libsystem_malloc's sources phase (magmallocProvider.d becomes a header below).
srcs=(src/instrumentation.c src/bitarray.c src/xzone/xzone_segment.c src/malloc_common.c src/purgeable_malloc.c
	src/sanitizer_malloc.c src/magazine_large.c src/magazine_malloc.c src/magazine_small.c src/stack_trace.c
	src/xzone/xzone_metapool.c src/magazine_rack.c src/legacy_malloc.c src/vm.c src/malloc_type.c
	src/msl_lite_support.c src/early_malloc.c src/xzone/xzone_introspect.c src/has_section.c src/pgm_malloc.c
	src/nanov2_malloc.c src/wrapper_zones.c resolver/resolver.c src/nano_malloc_common.c src/xzone/xzone_malloc.c
	src/malloc.c src/magazine_medium.c src/frozen_malloc.c src/magazine_tiny.c src/malloc_printf.c)
mkdir -p "$B/dtrace"; xcrun dtrace -h -s src/magmallocProvider.d -o "$B/dtrace/magmallocProvider.h"

# libmalloc.xcconfig and libmalloc_common.xcconfig.
flags=("${TARGET_FLAGS[@]}" -Os -fno-common -fno-typed-memory-operations -momit-leaf-frame-pointer
	-D_FORTIFY_SOURCE=0 -DOSATOMIC_USE_INLINED=1 -DOS_UNFAIR_LOCK_INLINE=1 -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1
	-DNDEBUG -Werror=implicit-function-declaration
	-I"$B/dtrace" -I"$M/include" -I"$M/private" -I"$M/resolver" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
compile "$B/obj" "$B/cflags" "${srcs[@]}"

mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_malloc.dylib \
	-current_version 792.1.1 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_platform -lsystem_pthread -L"$SDK/usr/lib/system" \
	-lcompiler_rt -ldyld -Wl,-upward-lsystem_featureflags -Wl,-upward-lsystem_c -Wl,-upward-lsystem_blocks \
	-Wl,-upward-lcorecrypto -Wl,-interposable_list,"$M/xcodeconfig/interposable.list" \
	-o "$OUT/usr/lib/system/libsystem_malloc.dylib"
