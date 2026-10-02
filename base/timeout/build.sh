#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# timeout from FreeBSD's bin/timeout (the ZFS test suite's;
# docs/architecture/filesystems.md §7). macOS 26's release set has no
# timeout(1), so by the reuse order (docs/repository.md §3.1) FreeBSD's is
# next. Its Makefile: PROG timeout, BINDIR /bin, no libraries (the /usr/bin
# symlink isn't installed). Patch 0001 replaces procctl(2)'s reaper with a
# process group.
#   build.sh OUT TIMEOUT_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives bin/timeout.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; T="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
T="$(stage_src "$T/bin/timeout" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$T"

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -fno-common -D__FBSDID=__RCSID $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/bin/timeout" "$B/cflags" timeout.c
