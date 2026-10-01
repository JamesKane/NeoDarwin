#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# getty, login and sysctl from system_cmds-1039 (docs/base/libsystem.md):
# replays system_cmds.xcodeproj's getty, login and sysctl targets with the
# project's base.xcconfig (gnu99, GCC_SYMBOLS_PRIVATE_EXTERN, its OTHER_CFLAGS).
#   build.sh OUT SYSTEM_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libpam_dylib)
# OUT receives usr/libexec/getty, usr/bin/login and its PAM policies
# private/etc/pam.d/{login,login.term}, usr/sbin/sysctl (the
# target's one source, no frameworks; the SMP boot test reads hw.ncpu with
# it, P1-06) and getty's launchd job,
# System/Library/LaunchDaemons/com.apple.getty.plist (getty std.9600 on the
# console). generate_plist.sh adds Disabled = true on macOS, where
# loginwindow owns the console; NeoDarwin installs it as the embedded
# platforms do, enabled.
# On macOS, login authenticates through PAM and records BSM audit and
# EndpointSecurity events (USE_PAM, USE_BSM_AUDIT; -lpam -lbsm, weak
# libEndpointSecuritySystem). NeoDarwin builds it with USE_PAM against
# OpenPAM (//base:libpam_dylib; docs/base/session.md, "PAM"), without
# libbsm: login_audit.c compiles to nothing without USE_BSM_AUDIT. Patch
# 0001 keeps the root-terminal check on the PAM path; 0002 makes the target's
# pam.d policies use pam_unix in place of pam_opendirectory (closed). Apple
# installs login setuid root (INSTALL_MODE_FLAG u+s); the image rule sets
# modes.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PAM=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libpam.2.dylib" ] && PAM="$d"; done
[ -n "$PAM" ] || { echo "system_cmds: no DEPROOT holds usr/lib/libpam.2.dylib (pass //base:libpam_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"

# base.xcconfig: GCC_C_LANGUAGE_STANDARD, GCC_SYMBOLS_PRIVATE_EXTERN,
# OTHER_CFLAGS (XPC_BUILD_OTHER_CFLAGS for the current major release),
# HEADER_SEARCH_PATHS (the project directory; the rest don't exist here).
# Warning flags change no interface and are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -fvisibility=hidden \
	-DHAVE_KDEBUG_TRACE=1 -DCONFIG_EMULATE_XNU_INITPROC_SELECTION=0 -DHAVE_GALARCH_AVAILABILITY=1 \
	-D__XPC_PROJECT_BUILD__=1 -I"$S" $(sysroot_flags "$SYSROOT")

tool "$B" "$ROOT" "$OUT/usr/libexec/getty" "$B/cflags" getty/chat.c getty/init.c getty/main.c getty/subr.c
# login: its Release settings' USE_PAM and -lpam, for the macosx SDK.
{ cat "$B/cflags"; printf '%s\n' -DUSE_PAM -I"$PAM/usr/local/include"; } > "$B/login.rsp"
tool "$B" "$ROOT" "$OUT/usr/bin/login" "$B/login.rsp" login/login.c login/login_audit.c -- -L"$PAM/usr/lib" -lpam
tool "$B" "$ROOT" "$OUT/usr/sbin/sysctl" "$B/cflags" sysctl/sysctl.c
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 getty/com.apple.getty.plist "$OUT/System/Library/LaunchDaemons/"
# The login target's copy phase: pam.d/login and login.term in /etc/pam.d.
mkdir -p "$OUT/private/etc/pam.d"
install -m 0644 login/pam.d/login login/pam.d/login.term "$OUT/private/etc/pam.d/"
