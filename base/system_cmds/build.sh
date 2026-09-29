#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# getty and login from system_cmds-1039 (docs/base/libsystem.md): replays
# system_cmds.xcodeproj's getty and login targets with the project's
# base.xcconfig (gnu99, GCC_SYMBOLS_PRIVATE_EXTERN, its OTHER_CFLAGS).
#   build.sh OUT SYSTEM_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/libexec/getty, usr/bin/login and getty's launchd job,
# System/Library/LaunchDaemons/com.apple.getty.plist (getty std.9600 on the
# console). generate_plist.sh adds Disabled = true on macOS, where
# loginwindow owns the console; NeoDarwin installs it as the embedded
# platforms do, enabled.
# On macOS, login authenticates through PAM and records BSM audit and
# EndpointSecurity events (USE_PAM, USE_BSM_AUDIT; -lpam -lbsm, weak
# libEndpointSecuritySystem). NeoDarwin builds neither OpenPAM nor libbsm
# yet, so login takes the source's own non-PAM path: crypt(3) against the
# password database's pw_passwd, nologin and root-terminal checks, utmpx.
# login_audit.c compiles to nothing without USE_BSM_AUDIT. Apple installs
# login setuid root (INSTALL_MODE_FLAG u+s); the image rule sets modes.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
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
tool "$B" "$ROOT" "$OUT/usr/bin/login" "$B/cflags" login/login.c login/login_audit.c
mkdir -p "$OUT/System/Library/LaunchDaemons"
install -m 0644 getty/com.apple.getty.plist "$OUT/System/Library/LaunchDaemons/"
