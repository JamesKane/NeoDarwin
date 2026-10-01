#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# libpam and OpenPAM's modules from OpenPAM-35 (OpenPAM 20071221 with Apple's
# changes; docs/base/session.md, "PAM"): replays openpam.xcodeproj's "Build
# Library", "Build shim", pam_deny and pam_permit targets and its "Install
# pam.d" phase, with the project's settings.
#   build.sh OUT OPENPAM_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/lib/libpam.2.dylib (+ the libpam.dylib link the target's
# script phase makes), usr/lib/libpam.1.dylib (the shim for binaries built
# before 10.6, which nothing may link: -allowable_client !),
# usr/lib/pam/pam_{deny,permit,unix}.so.2, private/etc/pam.d/other (deny
# everything) and, build-only, the public headers in
# usr/local/include/security (Apple's PUBLIC_HEADERS_FOLDER_PATH
# /usr/include/security; NeoDarwin's root has no /usr/include).
# pam_unix: OpenPAM ships it in openpam/modules but Apple doesn't build it;
# macOS authenticates through pam_opendirectory (OpenDirectory and
# CoreFoundation, both closed). NeoDarwin builds it as Apple builds pam_deny
# and pam_permit: it checks crypt(3) of the password against the password
# database's pw_passwd (Libinfo's file module: /etc/master.passwd). Patch
# 0001 adds FreeBSD's nullok option, for root's empty password.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; P="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
P="$(stage_src "$P" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$P"

# The project's GCC_PREPROCESSOR_DEFINITIONS (LIB_MAJ, the module search
# path, quoted for clang's response file, _GNU_SOURCE, __APPLE_MDM_SUPPORT__)
# and GCC_NO_COMMON_BLOCKS. The targets' sources reach the public headers
# through the SDK's /usr/include/security, here openpam/include. Warning
# flags are left out.
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -DLIB_MAJ=2 \
	"'-DOPENPAM_MODULES_DIR=\"/usr/lib/pam/;/usr/local/lib/pam/\"'" -D_GNU_SOURCE -D__APPLE_MDM_SUPPORT__ \
	-I"$P/openpam/include" -I"$P/openpam/lib" $(cmd_sysroot_flags "$SYSROOT")

# dylib OUT INSTALL_NAME OBJDIR LDFLAG...: an MH_DYLIB linked against ROOT's
# libSystem, signed ad hoc (CODE_SIGN_IDENTITY -).
dylib() {
	local out="$1" name="$2" obj="$3"; shift 3
	mkdir -p "$(dirname "$out")"
	xcrun ld -arch arm64 -platform_version macos 26.0 26.0 -dylib -dead_strip -adhoc_codesign \
		-install_name "$name" -syslibroot "$ROOT" "$obj"/*.o "$@" -lSystem -o "$out"
}

# Build Library: libpam.2.dylib (PRODUCT_NAME pam.2, EXECUTABLE_PREFIX lib,
# DYLIB_CURRENT/COMPATIBILITY_VERSION 3.0.0), and its libpam.dylib link.
LIB=(openpam_apple_chains openpam_borrow_cred openpam_configure openpam_dispatch openpam_dynamic openpam_findenv
	openpam_free_data openpam_free_envlist openpam_get_option openpam_load openpam_log openpam_nullconv
	openpam_readline openpam_restore_cred openpam_set_option openpam_static openpam_ttyconv pam_acct_mgmt
	pam_authenticate pam_chauthtok pam_close_session pam_end pam_error pam_get_authtok pam_get_data pam_get_item
	pam_get_user pam_getenv pam_getenvlist pam_info pam_open_session pam_prompt pam_putenv pam_set_data
	pam_set_item pam_setcred pam_setenv pam_start pam_strerror pam_verror pam_vinfo pam_vprompt pam_unsetenv)
compile_c() { local d="$1"; shift; compile "$d" "$B/cflags" "$@"; }
compile_c "$B/obj/libpam" $(printf 'openpam/lib/%s.c\n' "${LIB[@]}")
dylib "$OUT/usr/lib/libpam.2.dylib" /usr/lib/libpam.2.dylib "$B/obj/libpam" \
	-current_version 3.0.0 -compatibility_version 3.0.0
ln -s libpam.2.dylib "$OUT/usr/lib/libpam.dylib"

# Build shim: libpam.1.dylib (PRODUCT_NAME pam.1; OTHER_LDFLAGS
# -allowable_client !), which translates the pre-OpenPAM constants and calls
# libpam.2 through dlopen.
compile_c "$B/obj/shim" compat/pam_shim.c
dylib "$OUT/usr/lib/libpam.1.dylib" /usr/lib/libpam.1.dylib "$B/obj/shim" -allowable_client '!'

# The modules: MH_DYLIBs named pam_NAME.so.2 (EXECUTABLE_EXTENSION so.2) in
# /usr/lib/pam, linked against libpam.2. OpenPAM's loader tries
# /usr/lib/pam/NAME.so.2 before NAME.so.
for m in pam_deny pam_permit pam_unix; do
	compile_c "$B/obj/$m" "openpam/modules/$m/$m.c"
	dylib "$OUT/usr/lib/pam/$m.so.2" "/usr/lib/pam/$m.so.2" "$B/obj/$m" -L"$OUT/usr/lib" -lpam
done

# Install pam.d: private/etc/pam.d/other, 0444 (the "Cleanup EVERYTHING" phase).
mkdir -p "$OUT/private/etc/pam.d"
install -m 0444 pam.d/other "$OUT/private/etc/pam.d/other"
# PBXHeadersBuildPhase: Public headers.
mkdir -p "$OUT/usr/local/include/security"
cp openpam/include/security/*.h "$OUT/usr/local/include/security/"
