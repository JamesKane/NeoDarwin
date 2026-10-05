#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# mandoc and soelim from FreeBSD at 050683bb8e13 (P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): mandoc as usr.bin/mandoc's
# Makefile builds contrib/mandoc (its SRCS, linking libz for gzipped pages),
# and usr.bin/soelim. macOS ships mandoc without publishing it, and man-62's
# man(1) runs /usr/bin/mandoc.
#   build.sh OUT FREEBSD_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libz_dylib)
# OUT receives usr/bin/{mandoc,makewhatis,soelim} and the pages in
# usr/share/man. apropos and whatis stay man-62's (links to man.sh), as on
# macOS; FreeBSD's MK_MAN_UTILS links them to mandoc instead.
# config.h is committed (base/mandoc/config.h, a configure run); without
# reallocarray, recallocarray and ohash in Libc, mandoc's compat_ohash.c,
# compat_reallocarray.c and compat_recallocarray.c are compiled in, as its
# configure's MANDOC_COBJS lists, and soelim gets compat_reallocarray's.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
Z=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libz.1.dylib" ] && Z="$d"; done
[ -n "$Z" ] || { echo "mandoc: no DEPROOT holds usr/lib/libz.1.dylib (pass //base:libz_dylib)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
M="$F/contrib/mandoc"
cp "$PROJ/config.h" "$M/config.h"
cd "$M"

base=("${TARGET_FLAGS[@]}" -Os -DHAVE_CONFIG_H -I"$M" $(cmd_sysroot_flags "$SYSROOT"))
write_rsp "$B/mandoc.rsp" "${base[@]}" -isystem "$Z/usr/local/include"
# usr.bin/mandoc/Makefile's SRCS, in its groups.
LIBMAN=(man.c man_macro.c man_validate.c)
LIBMDOC=(arch.c att.c lib.c mdoc.c mdoc_argv.c mdoc_macro.c mdoc_markdown.c mdoc_state.c mdoc_validate.c st.c)
LIBROFF=(eqn.c roff.c roff_escape.c roff_html.c roff_term.c roff_validate.c tbl.c tbl_data.c tbl_layout.c tbl_opts.c)
LIB=("${LIBMAN[@]}" "${LIBMDOC[@]}" "${LIBROFF[@]}" chars.c mandoc.c mandoc_aux.c mandoc_dbg.c mandoc_msg.c
	mandoc_ohash.c mandoc_xr.c msec.c preconv.c read.c)
HTML=(eqn_html.c html.c man_html.c mdoc_html.c tbl_html.c)
TERM=(eqn_term.c man_term.c mdoc_term.c term.c term_ascii.c term_ps.c term_tab.c term_tag.c tbl_term.c)
DBM=(dbm.c dbm_map.c mansearch.c)
DBA=(dba.c dba_array.c dba_read.c dba_write.c mandocdb.c)
COMPAT=(compat_ohash.c compat_reallocarray.c compat_recallocarray.c)
tool "$B" "$ROOT" "$OUT/usr/bin/mandoc" "$B/mandoc.rsp" "${LIB[@]}" "${HTML[@]}" mdoc_man.c "${TERM[@]}" \
	"${DBM[@]}" "${DBA[@]}" main.c manpath.c out.c tag.c tree.c "${COMPAT[@]}" -- -L"$Z/usr/lib" -lz
ln -f "$OUT/usr/bin/mandoc" "$OUT/usr/bin/makewhatis"

# soelim: usr.bin/soelim, FreeBSD's own (not contrib/mandoc's soelim.c).
write_rsp "$B/soelim.rsp" "${base[@]}" -include "$M/config.h"
tool "$B" "$ROOT" "$OUT/usr/bin/soelim" "$B/soelim.rsp" "$F/usr.bin/soelim/soelim.c" "$M/compat_reallocarray.c"

mkdir -p "$OUT/usr/share/man/man1" "$OUT/usr/share/man/man5" "$OUT/usr/share/man/man7" "$OUT/usr/share/man/man8"
install -m 0444 mandoc.1 "$OUT/usr/share/man/man1/"
install -m 0444 "$F/usr.bin/soelim/soelim.1" "$OUT/usr/share/man/man1/"
install -m 0444 mandoc.db.5 "$OUT/usr/share/man/man5/"
install -m 0444 eqn.7 mandoc_char.7 tbl.7 man.7 mdoc.7 roff.7 "$OUT/usr/share/man/man7/"
install -m 0444 makewhatis.8 "$OUT/usr/share/man/man8/"
