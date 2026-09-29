#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# /usr/lib/system/libxpc.dylib: launchd-842.92.1's client library with
# NeoDarwin's libxpc stand-in (docs/base/libsystem.md §1).
#   build_libxpc.sh OUT LAUNCHD_SRC SYSROOT DEPROOT...
# OUT receives usr/lib/system/libxpc.dylib.
# Apple moved liblaunch (launch_*, vproc_*, bootstrap_*) into libxpc in OS X
# 10.8, so it is libxpc that libSystem reexports and that the open libraries
# import bootstrap_look_up2() and vproc_swap_integer() from. NeoDarwin's
# libxpc is launchd-842's liblaunch target (liblaunch.xcconfig: its three
# sources, job.defs and helper.defs, -fvisibility=hidden with the headers'
# visibility pushes choosing the exports) linked with the XPC object
# stand-in, base/standins/libxpc/xpc.c, built as the other stand-ins are.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
PROJ="$(cd "$(dirname "$0")" && pwd)"
STANDIN="$(cd "$(dirname "$0")/../standins/libxpc" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"

# MIG, as for launchd (build.sh): job.defs's client (vproc_mig_*), and
# helper.defs's client and server (launch_wait() serves helper_recv_wait).
M="$B/mig"; mkdir -p "$M"
migflags=(-arch arm64 -DXPC_BUILDING_LAUNCHD=1 -I"$S/src" -I"$S/liblaunch"
	-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
(cd "$M" && xcrun mig "${migflags[@]}" -user jobUser.c -header job.h -server /dev/null -sheader /dev/null "$S/src/job.defs") > /dev/null
(cd "$M" && xcrun mig "${migflags[@]}" -user helperUser.c -header helper.h -server helperServer.c -sheader helperServer.h \
	"$S/src/helper.defs") > /dev/null

# liblaunch.xcconfig (normal variant) with common.xcconfig's OTHER_CFLAGS.
write_rsp "$B/launch.rsp" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fvisibility=hidden -fno-common \
	-D__MigTypeCheck=1 -Dmig_external=__private_extern__ -DXPC_BUILDING_LAUNCHD=1 -D__DARWIN_NON_CANCELABLE=1 \
	-D_DARWIN_USE_64_BIT_INODE=1 -DHAVE_QUARANTINE=0 -DHAVE_RESPONSIBILITY=0 -DHAVE_SANDBOX=0 -DHAVE_SYSTEMSTATS=0 \
	-DHAVE_LIBAUDITD=0 -DHAVE_XPC_LAUNCHD=0 -I"$S/src" -I"$S/liblaunch" -I"$M" $(sysroot_flags "$SYSROOT")
compile "$B/launch" "$B/launch.rsp" "$S"/liblaunch/{liblaunch,libvproc,libbootstrap}.c "$M"/{jobUser,helperUser,helperServer}.c
# The stand-in, with base/standins/build.sh's flags.
write_rsp "$B/standin.rsp" "${TARGET_FLAGS[@]}" -Os -std=c17 -Wall -Wextra -Werror $(sysroot_flags "$SYSROOT")
compile "$B/standin" "$B/standin.rsp" "$STANDIN"/*.c

mkdir -p "$OUT/usr/lib/system"
xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name /usr/lib/system/libxpc.dylib \
	-current_version 1 -compatibility_version 1 -Wl,-umbrella,System "$B"/launch/*.o "$B"/standin/*.o \
	$(dep_libdirs "${DEPS[@]}") -lcompiler_rt -lsystem_kernel -lsystem_platform -lsystem_pthread \
	-lsystem_malloc -lsystem_c -lsystem_blocks -ldispatch -o "$OUT/usr/lib/system/libxpc.dylib"
