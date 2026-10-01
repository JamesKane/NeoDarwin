#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# PAM modules from pam_modules-217.0.1 (docs/base/session.md, "PAM"):
# replays pam_modules.xcodeproj's targets for the modules that need only
# libSystem and libpam: env, group, launchd, nologin, rootok, sacl, self and
# uwtmp (PRODUCT_NAME, EXECUTABLE_PREFIX pam_, EXECUTABLE_EXTENSION so.2,
# INSTALL_PATH /usr/lib/pam; each links libpam.2), with the project's
# settings (-Ddarwin, Logging.h from common/: openpam_log, as
# PAM_USE_OS_LOG is off).
#   build.sh OUT PAM_MODULES_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libpam_dylib)
# OUT receives usr/lib/pam/pam_NAME.so.2.
# Not built, for their closed dependencies: opendirectory, krb5, ntlm, mount
# (OpenDirectory, CoreFoundation, Heimdal, GSS, NetFS), smartcard
# (CryptoTokenKit's libraries, Security), localauthentication, tid and aks
# (LocalAuthentication's coreauthd client, Security, AppleKeyStore), and
# basesystem (CoreFoundation; Objective-C).
# Patch 0001: pam_group checks membership in the group database when
# mbr_check_membership(3) can't answer (Libinfo without Open Directory).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PAM=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libpam.2.dylib" ] && PAM="$d"; done
[ -n "$PAM" ] || { echo "pam_modules: no DEPROOT holds usr/lib/libpam.2.dylib (pass //base:libpam_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
M="$(stage_src "$M" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$M"

# Warning flags (-Wall, GCC_TREAT_WARNINGS_AS_ERRORS) are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -Ddarwin -I"$M/common" -I"$PAM/usr/local/include" \
	$(cmd_sysroot_flags "$SYSROOT")
for m in env group launchd nologin rootok sacl self uwtmp; do
	compile "$B/obj/$m" "$B/cflags" "modules/pam_$m/pam_$m.c"
	mkdir -p "$OUT/usr/lib/pam"
	xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
		-install_name "/usr/lib/pam/pam_$m.so.2" -syslibroot "$ROOT" "$B/obj/$m"/*.o \
		-L"$PAM/usr/lib" -lpam -lSystem -o "$OUT/usr/lib/pam/pam_$m.so.2"
done
