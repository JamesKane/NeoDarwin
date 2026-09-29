#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libsystem_notify from Libnotify-344.0.1 (docs/base/libsystem.md): replays
# Libnotify.xcodeproj's libnotify target with libnotify.xcconfig and
# base.xcconfig. The client library only; notifyd and notifyutil are later work.
#   build.sh OUT LIBNOTIFY_SRC SYSROOT DEPROOT...   (DEPROOT: kernel, platform, pthread, malloc, libc, blocks)
# OUT receives usr/lib/system/libsystem_notify.dylib.
# libdyld, libcompiler_rt, libdispatch, libxpc, libsystem_darwin and
# libsystem_collections aren't built yet; until they are, the dylib links them
# through the host SDK's .tbd stubs, which carry Apple's install names, and is
# relinked when NeoDarwin's exist.
# libxpc's xpc/private.h and launchd's bootstrap_priv.h come from base/sdk.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; N="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
N="$(stage_src "$N" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$N"
D="$B/derived"; mkdir -p "$D"

# The .d rule: notify_probes.d becomes a header.
xcrun dtrace -h -s notify_probes.d -o "$D/notify_probes.h"

# The .defs rule, ATTRIBUTES = Client: user stubs only, with OTHER_MIGFLAGS
# (none for this target) and the sysroot's header directories, in search order.
mig_includes=(-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
for n in notify_ipc notify_old_ipc; do
	(cd "$D" && xcrun mig "${mig_includes[@]}" -arch arm64 -header "$n.h" -user "${n}User.c" \
		-server /dev/null -sheader /dev/null "$N/$n.defs") > /dev/null
done

# base.xcconfig and libnotify.xcconfig: GCC_PREPROCESSOR_DEFINITIONS, OTHER_CFLAGS'
# -fno-exceptions, GCC_SYMBOLS_PRIVATE_EXTERN (-fvisibility=hidden),
# GCC_NO_COMMON_BLOCKS, GCC_TREAT_IMPLICIT_FUNCTION_DECLARATIONS_AS_ERRORS, and
# HEADER_SEARCH_PATHS (System.framework's PrivateHeaders, then the project).
# OTHER_CFLAGS' -Weverything with GCC_TREAT_WARNINGS_AS_ERRORS and LLVM_LTO are
# left out: diagnostics and link-time optimization change no interface.
flags=("${TARGET_FLAGS[@]}" -Os -std=gnu11 -fno-exceptions -fvisibility=hidden -fno-common
	-Werror=implicit-function-declaration -D__DARWIN_NON_CANCELABLE=1
	-I"$D" -I"$N" $(sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${flags[@]}"
# VERSIONING_SYSTEM = apple-generic, VERSION_INFO_PREFIX = __.
write_vers "$D/libsystem_notify_vers.c" libsystem_notify Libnotify 344.0.1 __
compile "$B/obj" "$B/cflags" libnotify.c notify_client.c table.c \
	"$D/notify_ipcUser.c" "$D/notify_old_ipcUser.c" "$D/libsystem_notify_vers.c"

# libnotify.xcconfig OTHER_LDFLAGS. The Frameworks phase's libCrashReporterClient.a
# (internal SDK) is left out: on arm64 os/crashlog_private.h records crash
# messages without it.
mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libsystem_notify.dylib \
	-current_version 344.0.1 -compatibility_version 1 -Wl,-umbrella,System "$B"/obj/*.o \
	$(dep_libdirs "${DEPS[@]}") -lsystem_kernel -lsystem_platform -lsystem_pthread -lsystem_malloc -lsystem_c \
	-lsystem_blocks -L"$SDK/usr/lib/system" -ldyld -lcompiler_rt -ldispatch -lxpc -lsystem_darwin \
	-lsystem_collections -o "$OUT/usr/lib/system/libsystem_notify.dylib"
