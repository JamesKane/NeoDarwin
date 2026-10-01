#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# pfctl from OpenBSD 4.3's sbin/pfctl (docs/base/pf-ntp.md): the program,
# the passive OS fingerprints, macOS's default ruleset and the launchd job
# that loads it.
#   build.sh OUT PFCTL_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives sbin/pfctl, private/etc/{pf.conf,pf.os,pf.anchors/com.apple}
# and System/Library/LaunchDaemons/com.apple.pfctl.plist (Disabled, as on
# macOS).
#
# Apple publishes no pfctl (no network_cmds tag has it, nor any other
# apple-oss-distributions project), and xnu's pf is OpenBSD's of early 2008
# (pfvar.h 1.259, pf.c 1.567), so pfctl is OpenBSD's as released in 4.3, the
# same months, compiled against xnu's own <net/pfvar.h> (System.framework's
# PrivateHeaders) so that every structure it passes is the kernel's. Its
# Makefile: SRCS (pfctl.c parse.y pfctl_parser.c pf_print_state.c
# pfctl_altq.c pfctl_osfp.c pfctl_radix.c pfctl_table.c pfctl_qstats.c
# pfctl_optimize.c, and sys/net/pf_ruleset.c, the userland half of the
# anchor code), -lm; -fcommon, as OpenBSD's compiler of 2008 defaulted to:
# pfctl.c and pf_ruleset.c both define pf_anchors and pf_main_anchor
# tentatively. parse.y goes through the toolchain's yacc (bison 2.3), as
# bash's does. xnu has no ALTQ: it answers ALTQ's requests ENODEV, and pfctl
# turns queueing off at start ("No ALTQ support in kernel"), as on macOS. So
# pfctl_altq.c and pfctl_qstats.c (queueing) aren't built, nor OpenBSD's
# sys/altq headers (some carry the four-clause BSD licence):
# src/nd_pfctl_noaltq.c stands in for their functions (a queueing rule is
# an error) and compat/altq the constants the parser names. parse.y hashes
# with MD5 (<md5.h>): FreeBSD's md5c.c from ndcrypto's pinned files,
# compiled as libresolv's copy is.
# compat/nd_pfctl_compat.h, included first in every source: OpenBSD's
# SIMPLEQ (Darwin's STAILQ), TAILQ_END, xnu's pf_addr member names, the
# ruleset prototypes xnu's pfvar.h keeps for the kernel. patches/: 0001
# xnu's structure layouts and Darwin's libc; 0002 xnu's enable references
# (-E, -X, -s References), scrub and dummynet anchors.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; D="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
# find_tree FILE: the repository next to PFCTL_SRC that holds FILE (as libresolv's build.sh).
find_tree() {
	local d; for d in "$(dirname "$D")"/*/; do [ -f "$d$1" ] && { printf '%s' "${d%/}"; return; }; done
	echo "pfctl: no repository next to $D holds $1" >&2; exit 1
}
FREEBSD="$(find_tree sys/crypto/md5c.c)"
NDCRYPTO="$(cd "$PROJ/../../kernel/neodarwin/crypto" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
D="$(stage_src "$D" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$D/sbin/pfctl"
cp ../../sys/net/pf_ruleset.c .
xcrun yacc -d -o parse.c parse.y 2> "$B/yacc.log" || { cat "$B/yacc.log" >&2; exit 1; }

write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -std=gnu99 -fcommon -include "$PROJ/compat/nd_pfctl_compat.h" \
	-I. -I"$PROJ/compat" -idirafter "$FREEBSD/sys/sys" $(sysroot_flags "$SYSROOT")
write_rsp "$B/md5.rsp" "${TARGET_FLAGS[@]}" -Os -std=c17 -w -include nd_freebsd.h -I"$NDCRYPTO/compat" \
	-I"$FREEBSD/sys" $(sysroot_flags "$SYSROOT")
compile "$B/obj/md5" "$B/md5.rsp" "$FREEBSD/sys/crypto/md5c.c"
tool "$B" "$ROOT" "$OUT/sbin/pfctl" "$B/cflags" pfctl.c parse.c pfctl_parser.c pf_print_state.c pfctl_osfp.c \
	pfctl_radix.c pfctl_table.c pfctl_optimize.c pf_ruleset.c "$PROJ/src/nd_pfctl_noaltq.c" -- "$B"/obj/md5/*.o

mkdir -p "$OUT/private/etc/pf.anchors" "$OUT/System/Library/LaunchDaemons"
install -m 0644 "$D/etc/pf.os" "$OUT/private/etc/pf.os"
install -m 0644 "$PROJ/pf.conf" "$OUT/private/etc/pf.conf"
install -m 0644 "$PROJ/com.apple.anchors" "$OUT/private/etc/pf.anchors/com.apple"
install -m 0644 "$PROJ/com.apple.pfctl.plist" "$OUT/System/Library/LaunchDaemons/com.apple.pfctl.plist"
