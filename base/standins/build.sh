#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# NeoDarwin's stand-ins for closed Apple libraries that the open ones link
# (docs/base/libsystem.md §1): each subdirectory is one dylib with Apple's
# install name and only the interfaces NeoDarwin's libraries use.
# libxpc/ is built with launchd-842's client library, by
# base/launchd/build_libxpc.sh (//base:libxpc), not here.
#   build.sh OUT STANDINS_DIR SYSROOT DEPROOT...   (DEPROOT: kernel, platform, c, ...)
# OUT receives usr/lib/system/<library>.dylib for each.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; SRC="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
mkdir -p "$OUT/usr/lib/system"

standin() {  # standin LIBRARY LINK-FLAGS...
	local lib="$1"; shift
	write_rsp "$B/$lib.rsp" "${TARGET_FLAGS[@]}" -Os -std=c17 -Wall -Wextra -Werror $(sysroot_flags "$SYSROOT")
	compile "$B/$lib" "$B/$lib.rsp" "$SRC/$lib"/*.c
	xcrun clang "${TARGET_FLAGS[@]}" -dynamiclib -nostdlib -install_name "/usr/lib/system/$lib.dylib" \
		-current_version 1 -compatibility_version 1 -Wl,-umbrella,System "$B/$lib"/*.o \
		$(dep_libdirs "${DEPS[@]}") "$@" -o "$OUT/usr/lib/system/$lib.dylib"
}
standin libcorecrypto -lsystem_kernel
standin libsystem_trace -lsystem_kernel -lsystem_malloc -lsystem_c
standin libsystem_sandbox
standin libsystem_sanitizers
standin libsystem_configuration
standin libsystem_featureflags
