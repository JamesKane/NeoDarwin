#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# make installhdrs for XNU into OUT_DIR.
#   headers.sh OUT_DIR XNU_SRC SDK_DIR DARWIN_VERSION
source "$(dirname "$0")/common.sh"
OUT="$(abspath "$1")"; SRC="$(abspath "$2")"; SDK="$(abspath "$3")"; VER="$4"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
stage_tree "$SRC" "$WORK/src"
assemble_sdk "$SDK" "$WORK/MacOSX.sdk"; SDK="$WORK/MacOSX.sdk"
SDKVARS=(); while IFS= read -r v; do SDKVARS+=("$v"); done < <(xnu_sdk_vars "$SDK")
make -C "$WORK/src" -j"$(sysctl -n hw.ncpu)" installhdrs "${SDKVARS[@]}" \
	ARCH_CONFIGS=ARM64 RC_DARWIN_KERNEL_VERSION="$VER" \
	OBJROOT="$WORK/obj" SYMROOT="$WORK/sym" DSTROOT="$OUT" > "$WORK/log" 2>&1 \
	|| { tail -40 "$WORK/log" >&2; exit 1; }
