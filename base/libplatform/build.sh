#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_platform from libplatform-359.1.2 (docs/base/libsystem.md). The
# published tarball has no Xcode project; the structure is its xcconfigs':
# eight sub-archives (libplatform.xcconfig PLATFORM_LIBRARIES), each built
# from its generic/ sources plus the arm64 ones (perarch.xcconfig), -all_load
# into the dylib. src/os uses os.xcconfig (hidden symbols, out-of-line
# spinlocks); atomics uses atomics.xcconfig.
#   build.sh OUT LIBPLATFORM_SRC SYSROOT DEPROOT...    (DEPROOT: libsystem_kernel)
# OUT receives usr/lib/system/libsystem_platform.dylib and
# usr/local/lib/dyld/libplatform.a (the dyld variant).
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; P="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
ND="$(cd "$(dirname "$0")" && pwd)"
cd "$P"   # the source groups below are globs relative to the tree

group_simple=(src/simple/*.c)
group_atomics=(src/atomics/init.c src/atomics/common/*.c src/atomics/arm64/*.c)
group_cachecontrol=(src/cachecontrol/generic/*.c src/cachecontrol/arm64/*.s)
group_os=(src/os/*.c)
group_setjmp=(src/setjmp/generic/*.c src/setjmp/arm64/*.s)
# NeoDarwin's src/nd_bitops.c stands in for the unpublished src/string/arm64
# (ffs, ffsl, fls, flsl).
group_string=(src/string/generic/*.c "$ND/src/nd_bitops.c")
group_ucontext=(src/ucontext/generic/*.c src/ucontext/arm64/*.c src/ucontext/arm64/*.s)
group_timingsafe=(src/timingsafe/arm64/*.c)
group_platform=(src/init.c src/force_libplatform_to_build.c)

# libplatform.xcconfig
common=("${TARGET_FLAGS[@]}" -Os -fno-stack-protector -fdollars-in-identifiers -fno-common -fverbose-asm
	-momit-leaf-frame-pointer -D_FORTIFY_SOURCE=0 -I"$P/private" -I"$P/include" -I"$P/internal"
	$(sysroot_flags "$SYSROOT"))
atomic_default=(-DOSATOMIC_USE_INLINED=0 -DOSATOMIC_DEPRECATED=0 -DOSSPINLOCK_USE_INLINED=1 -DOS_UNFAIR_LOCK_INLINE=0)
atomic_os=(-DOSATOMIC_USE_INLINED=0 -DOSATOMIC_DEPRECATED=0 -DOSSPINLOCK_USE_INLINED=0 -DOSSPINLOCK_DEPRECATED=0
	-fvisibility=hidden -I"$P/src/os/resolver")
atomic_atomics=(-DOSATOMIC_USE_INLINED=0 -DOSATOMIC_DEPRECATED=0)
variant_normal=()
variant_dyld=(-DVARIANT_DYLD=1 -DVARIANT_NO_RESOLVERS=1 -DVARIANT_STATIC=1)

build_variant() {  # build_variant NAME DEFINES...: all groups, one archive
	local v="$1"; shift
	local g
	for g in simple atomics cachecontrol os setjmp string ucontext timingsafe platform; do
		local -a extra
		case "$g" in os) extra=("${atomic_os[@]}") ;; atomics) extra=("${atomic_atomics[@]}") ;; *) extra=("${atomic_default[@]}") ;; esac
		write_rsp "$B/$v-$g.rsp" "${common[@]}" "${extra[@]}" "$@"
		local -a srcs; eval "srcs=(\"\${group_$g[@]}\")"
		compile "$B/$v/$g" "$B/$v-$g.rsp" "${srcs[@]}"
	done
	xcrun libtool -static -o "$B/libplatform_$v.a" $(find "$B/$v" -name '*.o') 2>/dev/null
}
build_variant normal ${variant_normal[@]+"${variant_normal[@]}"}
build_variant dyld "${variant_dyld[@]}"

mkdir -p "$OUT/usr/lib/system" "$OUT/usr/local/lib/dyld"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_platform.dylib \
	-current_version 359.1.2 -compatibility_version 1 -Wl,-umbrella,System \
	-Wl,-all_load "$B/libplatform_normal.a" $(dep_libdirs "${DEPS[@]}") -lsystem_kernel \
	-Wl,-alias_list,"$P/xcodeconfig/libplatform.aliases" -o "$OUT/usr/lib/system/libsystem_platform.dylib"
cp "$B/libplatform_dyld.a" "$OUT/usr/local/lib/dyld/libplatform.a"
