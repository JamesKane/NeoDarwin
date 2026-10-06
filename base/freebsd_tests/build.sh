#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The FreeBSD test suite's /usr/tests for the base's programs (P4-21
# checkpoint 7, docs/architecture/freebsd-parity.md §2.1), from the source
# drop at 050683bb8e13, installed as FreeBSD's bsd.test.mk installs it:
#   build.sh OUT FREEBSD_SRC SYSROOT DEPROOT...
#     (DEPROOT: //base:root, //base:kyua_commands for libatf-c.a)
# One line per tests/Makefile, in the words of share/mk's atf.test.mk,
# tap.test.mk and netbsd-tests.test.mk:
#   atf_sh DIR NAME [SRC [SED-EXPR...]]  ATF_TESTS_SH: "#! /usr/libexec/atf-sh"
#                                        then SRC (NAME.sh), through sed's
#                                        ATF_TESTS_SH_SED_NAME if given;
#   netbsd_sh DIR NAME [SED-EXPR...]     NETBSD_ATF_TESTS_SH: atf_sh from
#                                        contrib/netbsd-tests/DIR/t_NAME.sh
#                                        (NAME less _test);
#   tap_sh DIR NAME                      TAP_TESTS_SH: NAME.sh, executable;
#   atf_c DIR NAME [SRC...]              ATF_TESTS_C: NAME.c with libatf-c,
#                                        compiled with TARGET_FLAGS;
#   script DIR NAME SRC                  SCRIPTS (a helper, not a test);
#   files DIR SRCDIR FILE...             ${PACKAGE}FILES (0444);
#   meta DIR NAME 'KEY="VALUE"'          TEST_METADATA.NAME.
# DIR is the program's directory (bin/cat); its tests install in
# /usr/tests/DIR, with the Kyuafile suite.test.mk generates (KYUAFILE=auto),
# and tests/Kyuafile (auto-discovery) in /usr/tests and each top directory,
# as bin/tests, usr.bin/tests and usr.sbin/tests install it (KYUAFILE=yes).
# The ratchet that holds these tests is base/freebsd_tests/expected.tsv
# (//kernel:sbsa_freebsd_tests_test).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
ATF=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/local/lib/libatf-c.a" ] && ATF="$d/usr/local"; done
[ -n "$ATF" ] || { echo "freebsd_tests: no DEPROOT holds usr/local/lib/libatf-c.a (pass //base:kyua_commands)" >&2; exit 1; }
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ (none) applied
cd "$F"
T="$OUT/usr/tests"
# Each directory's tests, in order, as "interface name" lines; metadata per test.
reg() { mkdir -p "$B/kf/$1" "$T/$1"; printf '%s %s\n' "$2" "$3" >> "$B/kf/$1/tests"; }

atf_sh() {
	local dir="$1" name="$2" src="${3:-$1/tests/$2.sh}"; shift 2; [ $# -gt 0 ] && shift
	reg "$dir" atf "$name"
	{ echo '#! /usr/libexec/atf-sh'
	  if [ $# -gt 0 ]; then sed "$@" < "$src"; else cat "$src"; fi; } > "$T/$dir/$name"
	chmod 0555 "$T/$dir/$name"
}
netbsd_sh() {
	local dir="$1" name="$2"; shift 2
	local src="contrib/netbsd-tests/$dir/t_${name%_test}.sh"
	atf_sh "$dir" "$name" "$src" "$@"
}
tap_sh() { reg "$1" tap "$2"; install -m 0555 "$1/tests/$2.sh" "$T/$1/$2"; }
script() { mkdir -p "$T/$1"; install -m 0555 "$3" "$T/$1/$2"; }
files() {
	local dir="$1" from="$2" f; shift 2; mkdir -p "$T/$dir"
	for f in "$@"; do install -m 0444 "$from/$f" "$T/$dir/$f"; done
}
meta() { printf '%s\n' "$3" >> "$B/kf/$1/meta.$2"; }
# C tests: base/freebsd_cmds' compat layer (nd_freebsd.h force-included,
# nd_freebsd.c linked and dead stripped), as its programs are built.
C="$PROJ/../freebsd_cmds/compat"
write_rsp "$B/c.rsp" "${TARGET_FLAGS[@]}" -O2 -std=gnu11 -w -D__FBSDID=__RCSID -include "$C/nd_freebsd.h" -I"$C" \
	-isystem "$ATF/include" $(cmd_sysroot_flags "$SYSROOT")
atf_c() {
	local dir="$1" name="$2"; shift 2
	[ $# -gt 0 ] || set -- "$dir/tests/$name.c"
	reg "$dir" atf "$name"
	tool "$B" "$ROOT" "$T/$dir/$name" "$B/c.rsp" "$@" "$C/nd_freebsd.c" -- "$ATF/lib/libatf-c.a"
}

# --- bin ---
atf_sh bin/cat cat_extra_test
netbsd_sh bin/cat cat_test
files bin/cat contrib/netbsd-tests/bin/cat d_align.in d_align.out d_b_output.in d_b_output.out \
	d_se_output.in d_se_output.out d_s_output.in d_s_output.out d_vt_output.in d_vt_output.out stdin_test.out
netbsd_sh bin/expr expr_test -e 's/eval expr/eval expr --/g' \
	-e 's/"expr: integer overflow or underflow occurred for operation.*"/"expr: overflow"/g'
atf_sh bin/ls ls_tests
meta bin/ls ls_tests 'required_user="unprivileged"'
meta bin/ls ls_tests 'required_files="/usr/bin/awk /usr/bin/nc /usr/bin/sort"'
atf_c bin/pwait pwait_reap
atf_sh bin/pwait pwait_test
atf_sh bin/timeout timeout_test

# --- usr.bin ---
atf_sh usr.bin/asa asa_test
atf_sh usr.bin/cut cut2_test
netbsd_sh usr.bin/cut cut_test
files usr.bin/cut contrib/netbsd-tests/usr.bin/cut d_basic.out d_cut.in d_dflag.out d_dsflag.out d_latin1.in \
	d_sflag.out d_utf8.in
atf_sh usr.bin/head head_test
atf_sh usr.bin/ident ident_test
files usr.bin/ident usr.bin/ident/tests test.in test.out testnoid
atf_sh usr.bin/m4 m4_test
files usr.bin/m4 usr.bin/m4/tests $(cd usr.bin/m4/tests && ls | grep -v '^Makefile\|^m4_test.sh$')
atf_sh usr.bin/soelim soelim_test
files usr.bin/soelim usr.bin/soelim/tests nonexisting.in basic.in basic basic.out basic-with-space.in \
	basic-with-space.out
atf_sh usr.bin/tail tail_test
tap_sh usr.bin/tr legacy_test
files usr.bin/tr usr.bin/tr/tests regress.0{0,1,2,3,4,5,6,7,8,9,a,b,c,d}.out regress.in regress.sh regress2.in
atf_sh usr.bin/uniq uniq_test
atf_sh usr.bin/wc wc_test
# xo: contrib/libxo's tests/xo, xo_01 a SCRIPT, its saved outputs FILES.
atf_sh usr.bin/xo functional_test
script usr.bin/xo xo_01 contrib/libxo/tests/xo/xo_01.sh
files usr.bin/xo contrib/libxo/tests/xo/saved $(cd contrib/libxo/tests/xo/saved && ls xo_01.*)

# --- usr.sbin ---
atf_sh usr.sbin/daemon daemon_test
# nmtree: contrib/netbsd-tests/usr.sbin/mtree; two outputs through the
# Makefile's sed (FreeBSD's mtree spells the keyword sha256digest).
atf_sh usr.sbin/nmtree nmtree_test contrib/netbsd-tests/usr.sbin/mtree/t_mtree.sh
for f in mtree_d_create.out netbsd6_d_create.out; do
	sed -e 's/sha256/sha256digest/g' < "contrib/netbsd-tests/usr.sbin/mtree/$f" > "$T/usr.sbin/nmtree/$f"
	chmod 0444 "$T/usr.sbin/nmtree/$f"
done
files usr.sbin/nmtree contrib/netbsd-tests/usr.sbin/mtree d_convert.in d_convert_C.out d_convert_C_S.out \
	d_convert_D.out d_convert_D_S.out d_merge.in d_merge_C_M.out d_merge_C_M_S.out

# The Kyuafiles: suite.test.mk's for each directory (its tests sorted, as
# _TESTS:O; TEST_METADATA after the name), tests/Kyuafile for the rest.
for kf in "$B"/kf/*/*; do
	dir="${kf#"$B/kf/"}"
	{ echo '-- Automatically generated by bsd.test.mk.'; echo; echo 'syntax(2)'; echo
	  echo 'test_suite("FreeBSD")'; echo
	  LC_ALL=C sort -k2 "$kf/tests" | while read -r iface name; do
		md=""; [ -f "$kf/meta.$name" ] && md="$(awk '{ printf ", %s", $0 }' "$kf/meta.$name")"
		printf '%s_test_program{name="%s"%s}\n' "$iface" "$name" "$md"
	  done; } > "$T/$dir/Kyuafile"
	chmod 0444 "$T/$dir/Kyuafile"
done
for d in . bin usr.bin usr.sbin; do install -m 0444 tests/Kyuafile "$T/$d/Kyuafile"; done
# usr.bin/tests' FILES: regress.m4, the TAP driver of the legacy tests
# (usr.bin/tr's legacy_test runs ../regress.m4).
install -m 0444 usr.bin/tests/regress.m4 "$T/usr.bin/"
