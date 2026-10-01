#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# BSM audit from OpenBSM-21 (OpenBSM 1.1 with Apple's changes;
# docs/base/session.md, "BSM audit"): replays OpenBSM.xcodeproj's bsm.0,
# auditd.0, auditd, audit, auditreduce and praudit targets and its
# etc_security and LaunchDaemons phases, with the project's settings
# (GCC_C_LANGUAGE_STANDARD c99, GCC_STRICT_ALIASING NO, HEADER_SEARCH_PATHS
# openbsm, whose committed config/config.h Apple's build uses: configure
# doesn't run).
#   build.sh OUT OPENBSM_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives:
#   usr/lib/libbsm.0.dylib and libauditd.0.dylib, with the libbsm.dylib and
#     libauditd.dylib links the targets' script phases make;
#   usr/sbin/auditd, audit, auditreduce and praudit;
#   private/etc/security/audit_{class,control,event,user,warn};
#   System/Library/LaunchDaemons/com.apple.auditd.plist;
#   build-only, the public headers in usr/local/include/bsm (libbsm.h,
#     audit_uevents.h, audit_filter.h, auditd_lib.h; Apple's
#     usr/include/bsm and usr/local/include/bsm). bsm/audit.h and the other
#     kernel headers are xnu's, in the sysroot, as in Apple's build.
# OpenBSM-21 is the last OpenBSM Apple published (Mac OS X 10.6.8); later
# releases ship libbsm and auditd without source. Its auditd needs only open
# pieces: launchd check-in (liblaunch, in libxpc), ASL, notify and MIG.
# Patch 0001 teaches libbsm the token types xnu-12377 writes that OpenBSM 1.1
# predates (the identity token the kernel adds to every user record, and the
# Kerberos principal and certificate hash tokens), so praudit and
# auditreduce can read today's trails.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; P="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
P="$(stage_src "$P" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$P/openbsm"

# The project's settings. Warning flags change no interface and are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=c99 -fno-strict-aliasing -I"$P/openbsm" \
	$(cmd_sysroot_flags "$SYSROOT")

# dylib OUT INSTALL_NAME OBJDIR LDFLAG...: an MH_DYLIB linked against ROOT's
# libSystem, signed ad hoc.
dylib() {
	local out="$1" name="$2" obj="$3"; shift 3
	mkdir -p "$(dirname "$out")"
	xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
		-install_name "$name" -syslibroot "$ROOT" "$obj"/*.o "$@" -lSystem -o "$out"
}

# bsm.0: libbsm.0.dylib (EXECUTABLE_PREFIX lib, INSTALL_PATH /usr/lib), and
# the libbsm.dylib link its script phase makes.
LIBBSM=(bsm_audit bsm_class bsm_control bsm_domain bsm_event bsm_flags bsm_io bsm_mask bsm_notify
	bsm_socket_type bsm_token bsm_user bsm_wrappers bsm_errno bsm_fcntl)
compile "$B/obj/libbsm" "$B/cflags" $(printf 'libbsm/%s.c\n' "${LIBBSM[@]}")
dylib "$OUT/usr/lib/libbsm.0.dylib" /usr/lib/libbsm.0.dylib "$B/obj/libbsm"
ln -s libbsm.0.dylib "$OUT/usr/lib/libbsm.dylib"

# auditd.0: libauditd.0.dylib, linked against libbsm; and its libauditd.dylib link.
compile "$B/obj/libauditd" "$B/cflags" libauditd/auditd_lib.c
dylib "$OUT/usr/lib/libauditd.0.dylib" /usr/lib/libauditd.0.dylib "$B/obj/libauditd" -L"$OUT/usr/lib" -lbsm
ln -s libauditd.0.dylib "$OUT/usr/lib/libauditd.dylib"

# MIG, as Xcode's .defs rule runs it: a Server attribute gives NAMEServer.h
# and NAMEServer.c, Client NAME.h and NAMEUser.c.
M="$B/mig"; mkdir -p "$M/server" "$M/client"
migflags=(-arch arm64 -I"$SYSROOT/usr/local/include" -I"$SYSROOT/usr/include")
(cd "$M/server" && for d in audit_triggers auditd_control; do
	xcrun mig "${migflags[@]}" -user /dev/null -header /dev/null -server "${d}Server.c" -sheader "${d}Server.h" \
		"$P/openbsm/bin/auditd/$d.defs"
done) > /dev/null
(cd "$M/client" && xcrun mig "${migflags[@]}" -user auditd_controlUser.c -header auditd_control.h \
	-server /dev/null -sheader /dev/null "$P/openbsm/bin/auditd/auditd_control.defs") > /dev/null

# auditd: auditd.c, the Darwin back end (launchd check-in, the Mach trigger
# and control ports, ASL) and audit_warn.c; libbsm and libauditd.
{ cat "$B/cflags"; printf '%s\n' -I"$M/server"; } > "$B/auditd.rsp"
tool "$B" "$ROOT" "$OUT/usr/sbin/auditd" "$B/auditd.rsp" bin/auditd/auditd.c bin/auditd/auditd_darwin.c \
	bin/auditd/audit_warn.c "$M/server/audit_triggersServer.c" "$M/server/auditd_controlServer.c" \
	-- -L"$OUT/usr/lib" -lbsm -lauditd
# audit: sends auditd its triggers over the auditd_control interface.
{ cat "$B/cflags"; printf '%s\n' -I"$M/client"; } > "$B/audit.rsp"
tool "$B" "$ROOT" "$OUT/usr/sbin/audit" "$B/audit.rsp" bin/audit/audit.c "$M/client/auditd_controlUser.c" \
	-- -L"$OUT/usr/lib" -lbsm
tool "$B" "$ROOT" "$OUT/usr/sbin/auditreduce" "$B/cflags" bin/auditreduce/auditreduce.c -- -L"$OUT/usr/lib" -lbsm
tool "$B" "$ROOT" "$OUT/usr/sbin/praudit" "$B/cflags" bin/praudit/praudit.c -- -L"$OUT/usr/lib" -lbsm

# etc_security: private/etc/security, with its script phase's modes
# (0444; audit_control and audit_user 0400; audit_warn 0555).
E="$OUT/private/etc/security"; mkdir -p "$E"
install -m 0444 etc/audit_class etc/audit_event "$E/"
install -m 0400 etc/audit_control etc/audit_user "$E/"
install -m 0555 etc/audit_warn "$E/"
# LaunchDaemons: com.apple.auditd.plist, 0644.
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 "$P/com.apple.auditd.plist" "$OUT/System/Library/LaunchDaemons/"
# Headers phases: the public headers, build-only.
mkdir -p "$OUT/usr/local/include/bsm"
cp bsm/libbsm.h bsm/audit_uevents.h bsm/audit_filter.h bsm/auditd_lib.h "$OUT/usr/local/include/bsm/"
