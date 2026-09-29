#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_pthread from libpthread-539 (docs/base/libsystem.md): replays
# libpthread.xcodeproj's libsystem_pthread target and its libpthread dyld
# variant, with pthread.xcconfig's settings.
#   build.sh OUT LIBPTHREAD_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform)
# OUT receives usr/lib/system/libsystem_pthread.dylib and
# usr/local/lib/dyld/libpthread.a.
# libdyld and libmacho aren't built yet (checkpoint 3); until they are, the
# dylib links them through the host SDK's .tbd stubs, which carry Apple's
# install names, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; P="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
P="$(stage_src "$P" "$B/src" "$(cd "$(dirname "$0")" && pwd)/patches")"   # patches/ applied
cd "$P"

# libsystem_pthread's and libpthread dyld's sources phases.
normal=(src/resolver/resolver.c src/pthread.c src/pthread_cancelable.c src/pthread_cond.c src/pthread_mutex.c src/qos.c
	src/pthread_rwlock.c src/pthread_cwd.c src/pthread_tsd.c src/variants/pthread_cancelable_cancel.c src/pthread_atfork.c
	src/pthread_dependency.c src/pthread_asm.s)
dyld=(src/resolver/resolver.c src/qos.c src/pthread.c src/pthread_cancelable.c src/pthread_cond.c src/pthread_mutex.c
	src/pthread_dependency.c src/pthread_rwlock.c src/pthread_tsd.c src/pthread_cwd.c src/pthread_atfork.c)

# The DTrace provider header the sources include.
mkdir -p "$B/gen"; xcrun dtrace -h -s src/plockstat.d -o "$B/gen/plockstat.h"

# pthread.xcconfig: BASE_PREPROCESSOR_MACROS, OTHER_CFLAGS, DISABLED_WARNING_CFLAGS, SRCROOT_SEARCH_PATHS.
base=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-common -fno-stack-protector -fno-stack-check -fno-builtin
	-Wno-int-conversion -Wno-sign-compare -Wno-sign-conversion -Wno-unused-parameter
	-D__LIBC__ -D__POSIX_LIB__ -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 -D__DARWIN_NON_CANCELABLE=1
	-D__DARWIN_VERS_1050=1 -D_FORTIFY_SOURCE=0 -D__PTHREAD_BUILDING_PTHREAD__=1 -D__PTHREAD_EXPOSE_INTERNALS__
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1
	-I"$B/gen" -I"$P/src/resolver" -I"$P/private" -I"$P/include" -I"$P" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/normal.rsp" "${base[@]}" -momit-leaf-frame-pointer
write_rsp "$B/dyld.rsp" "${base[@]}" -DVARIANT_STATIC=1 -DVARIANT_DYLD=1
compile "$B/normal" "$B/normal.rsp" "${normal[@]}"
compile "$B/dyld" "$B/dyld.rsp" "${dyld[@]}"

mkdir -p "$OUT/usr/lib/system" "$OUT/usr/local/lib/dyld"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_pthread.dylib \
	-current_version 539 -compatibility_version 1 -Wl,-umbrella,System -Wl,-alias_list,"$P/xcodescripts/pthread.aliases" \
	"$B"/normal/*.o $(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_platform \
	-L"$SDK/usr/lib/system" -ldyld -Wl,-upward-lmacho -o "$OUT/usr/lib/system/libsystem_pthread.dylib"
xcrun libtool -static -o "$OUT/usr/local/lib/dyld/libpthread.a" "$B"/dyld/*.o 2>/dev/null
