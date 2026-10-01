#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# launchd from launchd-842.92.1 (docs/base/libsystem.md): replays
# launchd.xcodeproj's launchd target with its xcconfigs (common.xcconfig,
# launchd.xcconfig): the daemon's sources and MIG subsystems, -fvisibility=hidden
# (GCC_SYMBOLS_PRIVATE_EXTERN), -D__MigTypeCheck=1 -Dmig_external=__private_extern__
# -D_DARWIN_USE_64_BIT_INODE=1 -DXPC_BUILDING_LAUNCHD=1, the project's src/
# and liblaunch/ ahead of the SDK's System.framework PrivateHeaders.
#   build.sh OUT LAUNCHD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives sbin/launchd.
# Left out, as NeoDarwin has no implementation of them (patches/ gate each):
#  - libxpc's launchd interface (xpc/launchd.h, xpc/domain.defs, xpc/init.defs):
#    XPC domains, events and process calls, whose only clients (xpcproxy,
#    xpcd, UserEventAgent) are closed. launchd serves its MIG subsystems from
#    a plain mach_msg_server_once() loop instead of xpc_pipe_try_receive();
#  - libauditd (audit_quick_stop at shutdown) and libCrashReporterClient.a
#    (base/sdk's CrashReporterClient.h defines the annotations);
#  - libbsm, linked for audit_token_to_au32() only: src/nd_audit_token.c
#    reads the token as libbsm does;
#  - quarantine, responsibility, sandbox and systemstats: config.h would
#    enable sandbox from the public SDK's <sandbox.h>, which lacks
#    sandbox_check(); every switch is set off here (-DHAVE_...=0);
#  - per-user launchds (patch 0005, HAVE_PER_USER_LAUNCHD=0): PID 1 serves
#    every user, as embedded launchd does. Its exchanges with a per-user
#    launchd move port arrays, which xnu-12377 refuses from a platform
#    binary (docs/base/session.md, "PAM");
#  - importance-watch ports (patch 0006, HAVE_IMPORTANCE_WATCH_PORTS=0): a
#    job's child doesn't ask PID 1 for its MachServices' send rights, which
#    PID 1 would send as such an array (docs/base/session.md, "BSM audit").
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"

# MIG, as Xcode's .defs rule runs it with launchd.xcconfig's OTHER_MIGFLAGS:
# Client gives NAME.h and NAMEUser.c, Server NAMEServer.h and NAMEServer.c.
M="$B/mig"; mkdir -p "$M"
migflags=(-arch arm64 -DXPC_BUILDING_LAUNCHD=1 -I"$S/src" -I"$S/liblaunch"
	-I"$SYSROOT/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders"
	-I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
migsrcs=()
mig() {  # mig DEFS client|server|both
	local defs="$1" n; n="$(basename "${1%.defs}")"
	local user=/dev/null header=/dev/null server=/dev/null sheader=/dev/null
	case "$2" in client|both) user="$n"User.c; header="$n.h"; migsrcs+=("$M/${n}User.c") ;; esac
	case "$2" in server|both) server="$n"Server.c; sheader="$n"Server.h; migsrcs+=("$M/${n}Server.c") ;; esac
	(cd "$M" && xcrun mig "${migflags[@]}" -user "$user" -header "$header" -server "$server" -sheader "$sheader" "$defs") > /dev/null
}
mig "$S/src/helper.defs" both
mig "$S/src/internal.defs" both
mig "$S/src/job.defs" both
mig "$S/src/job_reply.defs" client
mig "$S/src/job_forward.defs" client
mig "$SYSROOT/usr/include/mach/mach_exc.defs" server
mig "$SYSROOT/usr/include/mach/notify.defs" server

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fvisibility=hidden -fno-common \
	-D__MigTypeCheck=1 -Dmig_external=__private_extern__ -D_DARWIN_USE_64_BIT_INODE=1 -DXPC_BUILDING_LAUNCHD=1 \
	-DHAVE_XPC_LAUNCHD=0 -DHAVE_LIBAUDITD=0 -DHAVE_QUARANTINE=0 -DHAVE_RESPONSIBILITY=0 -DHAVE_SANDBOX=0 -DHAVE_SYSTEMSTATS=0 \
	-DHAVE_PER_USER_LAUNCHD=0 -DHAVE_IMPORTANCE_WATCH_PORTS=0 \
	-I"$PROJ/include" -I"$S/src" -I"$S/liblaunch" -I"$M" $(sysroot_flags "$SYSROOT")
write_vers "$B/launchd_vers.c" launchd launchd 842.92.1
tool "$B" "$ROOT" "$OUT/sbin/launchd" "$B/cflags" \
	"$S"/src/{launchd,runtime,core,ipc,kill2,ktrace,log}.c "${migsrcs[@]}" "$PROJ/src/nd_audit_token.c" "$B/launchd_vers.c"
