#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# su from shell_cmds-326 (docs/base/session.md, "PAM"): replays
# shell_cmds.xcodeproj's su target (project settings as in build.sh;
# INSTALL_PATH /usr/bin; libpam) and install-files.sh's su.pam, installed as
# /etc/pam.d/su. A target of its own, apart from build.sh's commands,
# because it links libpam.
#   su.sh OUT SHELL_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libpam_dylib)
# OUT receives usr/bin/su and private/etc/pam.d/su. Apple installs su setuid
# root (INSTALL_MODE_FLAG u+s); the image rule sets modes.
# su.c soft-links libEndpointSecuritySystem (closed) through the private
# <SoftLinking/SoftLinking.h> and asks libsystem_sandbox's private
# <rootless.h> about the installer's environment: compat/ has both (no
# EndpointSecurity library is found; never the installer). Without
# USE_BSM_AUDIT, as Apple builds it, su records no audit events but still
# gives a login shell (su -) its own audit session, as macOS does.
# su.pam authenticates through pam_opendirectory (closed); patch 0001
# replaces it with pam_unix, and pam_launchd with pam_permit.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PAM=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libpam.2.dylib" ] && PAM="$d"; done
[ -n "$PAM" ] || { echo "su.sh: no DEPROOT holds usr/lib/libpam.2.dylib (pass //base:libpam_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common -D__FBSDID=__RCSID \
	-I"$PROJ/compat" -I"$PAM/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
write_vers "$D/su_vers.c" su shell_cmds 326
tool "$B" "$ROOT" "$OUT/usr/bin/su" "$B/cflags" su/su.c "$D/su_vers.c" -- -L"$PAM/usr/lib" -lpam
mkdir -p "$OUT/private/etc/pam.d"
install -m 0644 su/su.pam "$OUT/private/etc/pam.d/su"
