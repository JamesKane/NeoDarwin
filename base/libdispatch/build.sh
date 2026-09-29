#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libdispatch from libdispatch-1542.0.4 (docs/base/libsystem.md): replays
# libdispatch.xcodeproj's "libdispatch" target (the macOS dylib; the
# "resolved" variants are iOS-only and "mp static" is a separate archive),
# with its Objective-C parts (object.m, data.m, client_callout.mm).
# Apple publishes libdispatch.xcconfig without its settings since
# libdispatch-1271; the flags below are the last published full one's
# (libdispatch-1173.100.2), which this project's layout still matches.
#   build.sh OUT LIBDISPATCH_SRC SYSROOT DEPROOT...
#     (DEPROOT: kernel, platform, pthread, malloc, libc, blocks)
# OUT receives usr/lib/system/libdispatch.dylib.
# libdyld, libcompiler_rt and libobjc (upward) aren't built yet; until they
# are, the dylib links them through the host SDK's .tbd stubs, which carry
# Apple's install names, and is relinked when NeoDarwin's exist.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$(stage_src "$D" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D"
G="$B/derived"; mkdir -p "$G"

# The target's sources phase; the .d and .defs files become generated sources below.
srcs=(resolver/resolver.c src/init.c src/object.c src/object.m src/block.cpp src/shims/lock.c src/semaphore.c
	src/once.c src/eventlink.c src/queue.c src/apply.c src/source.c src/workgroup.c src/shims/yield.c
	src/client_callout.mm src/mach.c src/event/event.c src/event/event_kevent.c src/event/event_epoll.c
	src/voucher.c src/firehose/firehose_buffer.c src/io.c src/data.c src/data.m src/transform.c src/time.c
	src/allocator.c src/benchmark.c)
# NeoDarwin's: the work interval instance interface, until libsystem_kernel has it.
srcs+=("$PROJ/src/nd_work_interval_instance.c")

# provider.d: Xcode's DTrace rule makes the provider header.
xcrun dtrace -h -s src/provider.d -o "$G/provider.h"

# .defs, as Xcode's MIG rule runs them with the attributes of the sources
# phase (protocol: Client and Server; firehose: Client; firehose_reply: Server)
# and OTHER_MIGFLAGS: -novouchers and the sysroot's header directories, in
# search order, then the project's HEADER_SEARCH_PATHS (mig-headers.sh).
mig_includes=(-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include" -I"$D" -I"$D/private" -I"$D/src")
mig() { (cd "$G" && xcrun mig -novouchers "${mig_includes[@]}" -arch arm64 "$@") > /dev/null; }
mig -header protocol.h -user protocolUser.c -sheader protocolServer.h -server protocolServer.c "$D/src/protocol.defs"
mig -header firehose.h -user firehoseUser.c -server /dev/null "$D/src/firehose/firehose.defs"
mig -header firehose_reply.h -user /dev/null -sheader firehose_replyServer.h -server firehose_replyServer.c \
	"$D/src/firehose/firehose_reply.defs"
srcs+=("$G/protocolUser.c" "$G/protocolServer.c" "$G/firehoseUser.c" "$G/firehose_replyServer.c")

# libdispatch.xcconfig (1173.100.2): GCC_PREPROCESSOR_DEFINITIONS, OTHER_CFLAGS
# (and OTHER_CFLAGS_normal), GCC_SYMBOLS_PRIVATE_EXTERN, GCC_NO_COMMON_BLOCKS,
# GCC_OPTIMIZATION_LEVEL, ENABLE_STRICT_OBJC_MSGSEND, HEADER_SEARCH_PATHS with
# the sysroot as SYSTEM_HEADER_SEARCH_PATHS. C++ is gnu++11 without
# exceptions (GCC_ENABLE_CPP_EXCEPTIONS = NO); libc++ precedes the C headers.
# The target's OTHER_CPLUSPLUSFLAGS add -reorder-cxx-includes-hack, which only
# Apple's internal clang has; the include order below does the same job.
# Three settings Apple's build takes from places not published:
#  - OS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1, as libmalloc's and libpthread's
#    xcconfigs set it: libplatform's os/atomic_private.h defines the dependency
#    orderings inline_internal.h uses only with it;
#  - DISPATCH_SEND_ACTIVITY_IN_MSGV=1: the published internal.h has an #error
#    in place of the OS version check, whose macOS branch is "13.0 or later on
#    arm64", i.e. 1;
#  - __PTHREAD_EXPOSE_INTERNALS__: shims/priority.h uses the pthread_priority_t
#    helpers of xnu's pthread/priority_private.h, which it exposes only then.
# sdk/ completes xnu's sys/work_interval.h (see there).
common=("${TARGET_FLAGS[@]}" -Os -fno-common -fvisibility=hidden -fstrict-aliasing -fverbose-asm
	-momit-leaf-frame-pointer -D__DARWIN_NON_CANCELABLE=1 -DOBJC_OLD_DISPATCH_PROTOTYPES=0
	-DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 -DDISPATCH_SEND_ACTIVITY_IN_MSGV=1 -D__PTHREAD_EXPOSE_INTERNALS__=1
	-I"$G" -I"$D" -I"$D/private" -I"$D/src" -I"$PROJ/sdk")
write_rsp "$B/c.rsp" "${common[@]}" -std=gnu11 $(sysroot_flags "$SYSROOT")
write_rsp "$B/cxx.rsp" "${common[@]}" -std=gnu++11 -fno-exceptions -nostdinc++ -isystem "$SYSROOT/usr/include/c++/v1" \
	$(sysroot_flags "$SYSROOT")
csrcs=(); cxxsrcs=()
for s in "${srcs[@]}"; do case "$s" in *.cpp|*.mm) cxxsrcs+=("$s") ;; *) csrcs+=("$s") ;; esac; done
rc=0
compile "$B/obj" "$B/c.rsp" "${csrcs[@]}" || rc=1
compile "$B/obj" "$B/cxx.rsp" "${cxxsrcs[@]}" || rc=1
[ "$rc" = 0 ] || exit 1
write_vers "$B/libdispatch_vers.c" libdispatch libdispatch 1542.0.4
compile "$B/obj" "$B/c.rsp" "$B/libdispatch_vers.c"

# DYLIB_LDFLAGS, OBJC_LDFLAGS, ALIASES_LDFLAGS and the macOS ORDER_LDFLAGS;
# DYLIB_CURRENT_VERSION is the project version. UNWIND_LDFLAGS (-lunwind) and
# LIBDARWIN_LDFLAGS (-upward-lsystem_darwin) are left out: this build uses
# no symbol from either, and ld would otherwise record them as dependencies.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libdispatch.dylib \
	-current_version 1542.0.4 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_platform -lsystem_pthread -lsystem_malloc -lsystem_c \
	-lsystem_blocks -L"$SDK/usr/lib/system" -L"$SDK/usr/lib" -ldyld -lcompiler_rt -Wl,-upward-lobjc \
	-Wl,-alias_list,"$D/xcodeconfig/libdispatch.aliases" -Wl,-order_file,"$D/xcodeconfig/libdispatch.order" \
	-o "$OUT/usr/lib/system/libdispatch.dylib"
