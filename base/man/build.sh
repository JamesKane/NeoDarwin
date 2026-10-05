#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# man, manpath, apropos and whatis from man-62 (P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): FreeBSD's man.sh with Apple's
# changes, installed as man/Makefile's install target does. It formats pages
# with /usr/bin/mandoc (base/mandoc) and pages them through /usr/bin/less
# (base/less).
#   build.sh OUT MAN_SRC SYSROOT
# OUT receives usr/bin/man with its manpath, apropos and whatis links,
# private/etc/man.conf and the pages in usr/share/man. The tests target
# (Apple's internal test material) isn't installed.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; M="$(abspath "$2")/man"
mkdir -p "$OUT/usr/bin" "$OUT/private/etc" "$OUT/usr/share/man/man1" "$OUT/usr/share/man/man5"
install -m 0644 "$M/man.conf" "$OUT/private/etc/"
install -m 0755 "$M/man.sh" "$OUT/usr/bin/man"
for l in manpath apropos whatis; do ln -f "$OUT/usr/bin/man" "$OUT/usr/bin/$l"; done
install -m 0444 "$M/apropos.1" "$M/man.1" "$M/manpath.1" "$M/whatis.1" "$OUT/usr/share/man/man1/"
install -m 0444 "$M/man.conf.5" "$OUT/usr/share/man/man5/"
