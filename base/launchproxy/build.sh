#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# launchproxy from launchd-842.92.1 (docs/base/session.md, "Loopback and
# sshd"): replays launchd.xcodeproj's launchproxy target with
# common.xcconfig (-D__MigTypeCheck=1 -Dmig_external=__private_extern__
# -D_DARWIN_USE_64_BIT_INODE=1, -fvisibility=hidden, the project's src/ and
# liblaunch/ ahead of System.framework's PrivateHeaders).
#   build.sh OUT LAUNCHD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/libexec/launchproxy. launchd-842 runs it for a job with
# inetdCompatibility (core.c: job_start_child's file2exec): it checks in,
# accepts each connection on the job's sockets and runs the job's program
# with the connection as its standard input and output (and error, unless
# the job names a StandardErrorPath), as inetd does. The launchd project's
# patches are applied, as for the daemon: 0001 lets the build set config.h's
# HAVE_* switches, all off as base/launchd/build.sh sets them.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/../launchd/patches")"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fvisibility=hidden -fno-common \
	-D__MigTypeCheck=1 -Dmig_external=__private_extern__ -D_DARWIN_USE_64_BIT_INODE=1 \
	-DHAVE_XPC_LAUNCHD=0 -DHAVE_LIBAUDITD=0 -DHAVE_QUARANTINE=0 -DHAVE_RESPONSIBILITY=0 -DHAVE_SANDBOX=0 -DHAVE_SYSTEMSTATS=0 \
	-I"$S/src" -I"$S/liblaunch" $(sysroot_flags "$SYSROOT")
write_vers "$B/launchproxy_vers.c" launchproxy launchd 842.92.1
tool "$B" "$ROOT" "$OUT/usr/libexec/launchproxy" "$B/cflags" "$S/support/launchproxy.c" "$B/launchproxy_vers.c"
