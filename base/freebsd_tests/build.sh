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
#   plain_c DIR NAME RSP SRC... [-- LIB...]  PLAIN_TESTS_C: a program whose
#                                        exit status is its result;
#   prog DIR NAME SRC...                 PROGS (a C helper, not a test);
#   script DIR NAME SRC                  SCRIPTS (a helper, not a test);
#   files DIR SRCDIR FILE...             ${PACKAGE}FILES (0444);
#   tree_files DIR [SRCDIR]              every file of SRCDIR (DIR/tests)
#                                        but Makefiles and test sources;
#   meta DIR NAME 'KEY="VALUE"'          TEST_METADATA.NAME.
# DIR is the program's directory (bin/cat), or a TESTS_SUBDIRS directory
# under it (bin/sh/builtins); its tests install in /usr/tests/DIR, with the
# Kyuafile suite.test.mk generates (KYUAFILE=auto; a TESTS_SUBDIRS parent's
# includes its subdirectories'), and tests/Kyuafile (auto-discovery) in
# /usr/tests and each top directory, as bin/tests, sbin/tests, usr.bin/tests
# and usr.sbin/tests install it (KYUAFILE=yes).
# Left out (docs/architecture/freebsd-parity.md §2.1, the known gaps):
# bin/pax's legacy_test.pl (TAP_TESTS_PERL: no perl in the base), sbin/ping's
# test_ping.py (ATF_TESTS_PYTEST: no Python) and in_cksum_test (a unit test
# of FreeBSD's sbin/ping/utils.c; the base's ping is network_cmds'), and
# usr.sbin/makefs's makefs_zfs_tests (makefs is built without zfs, as
# MK_ZFS=no).
# The ratchet that holds these tests is base/freebsd_tests/expected.tsv
# (//kernel:sbsa_freebsd_tests_test).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
ATF=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/local/lib/libatf-c.a" ] && ATF="$d/usr/local"; done
[ -n "$ATF" ] || { echo "freebsd_tests: no DEPROOT holds usr/local/lib/libatf-c.a (pass //base:kyua_commands)" >&2; exit 1; }
dep() {
	local d; for d in "${DEPS[@]}"; do [ -e "$d/usr/lib/$1" ] && { printf '%s' "$d"; return 0; }; done
	echo "freebsd_tests: no DEPROOT holds usr/lib/$1 (pass $2)" >&2; return 1
}
SBUF="$(dep libsbuf.dylib //base:libsbuf_dylib)"; UTIL="$(dep libutil.dylib //base:libutil_dylib)"
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
tap_sh() { reg "$1" tap "$2"; install -m 0555 "${3:-$1/tests/$2.sh}" "$T/$1/$2"; }
script() { mkdir -p "$T/$1"; install -m 0555 "$3" "$T/$1/$2"; }
files() {
	local dir="$1" from="$2" f; shift 2; mkdir -p "$T/$dir"
	for f in "$@"; do install -m 0444 "$from/$f" "$T/$dir/$f"; done
}
tree_files() {
	local dir="$1" from="${2:-$1/tests}" f; mkdir -p "$T/$dir"
	for f in "$from"/*; do
		[ -f "$f" ] || continue
		case "${f##*/}" in Makefile|Makefile.depend|*_test.sh|legacy_test.sh|*.c) continue ;; esac
		install -m 0444 "$f" "$T/$dir/${f##*/}"
	done
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
	local -a srcs=(); while [ $# -gt 0 ] && [ "$1" != "--" ]; do srcs+=("$1"); shift; done
	[ "${1:-}" = "--" ] && shift
	tool "$B" "$ROOT" "$T/$dir/$name" "${CRSP:-$B/c.rsp}" "${srcs[@]}" "$C/nd_freebsd.c" -- "$ATF/lib/libatf-c.a" "$@"
}
plain_c() {
	local dir="$1" name="$2" rsp="$3"; shift 3
	reg "$dir" plain "$name"
	tool "$B" "$ROOT" "$T/$dir/$name" "$rsp" "$@"
}
prog() {
	local dir="$1" name="$2"; shift 2; mkdir -p "$T/$dir"
	tool "$B" "$ROOT" "$T/$dir/$name" "$B/c.rsp" "$@" "$C/nd_freebsd.c"
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
# pwait_reap writes to its pipe's read end (FreeBSD's pipes are
# bidirectional): a socketpair here (compat/nd_pipe_socketpair.h), so its
# cases fail at once (expected.tsv) instead of hanging until kyua's timeout.
write_rsp "$B/sp.rsp" "@$B/c.rsp" -include "$PROJ/compat/nd_pipe_socketpair.h"
CRSP="$B/sp.rsp" atf_c bin/pwait pwait_reap
atf_sh bin/pwait pwait_test
atf_sh bin/timeout timeout_test
# Batch 1 of part 2 (docs/architecture/freebsd-parity.md §2.1).
atf_sh bin/chflags chflags_test
atf_sh bin/chmod chmod_test
atf_sh bin/cp cp_test
atf_sh bin/date format_string_test
atf_sh bin/dd dd2_test
netbsd_sh bin/dd dd_test
atf_sh bin/echo echo_test
atf_sh bin/ed ed_test
atf_sh bin/hostname hostname_test
atf_sh bin/ln ln_test
atf_sh bin/mkdir mkdir_test
atf_sh bin/mv mv_test
# pkill: TAP tests and their spin_helper (PROGS); the jail cases need jail(8).
prog bin/pkill spin_helper bin/pkill/tests/spin_helper.c
for t in $(cd bin/pkill/tests && ls p*_test.sh); do tap_sh bin/pkill "${t%.sh}"; done
for t in pgrep-j_test pkill-j_test; do
	meta bin/pkill $t 'required_user="root"'; meta bin/pkill $t 'required_programs="jail jls"'
done
atf_sh bin/pwd pwd_test
atf_sh bin/rm rm_test
atf_sh bin/rmdir rmdir_test
# sh: one functional_test per TESTS_SUBDIRS directory, each case a script.
for d in builtins errors execution expansion invocation parameters parser set-e; do
	atf_sh bin/sh/$d functional_test bin/sh/tests/functional_test.sh
	tree_files bin/sh/$d bin/sh/tests/$d
done
# NeoDarwin's TIMEOUT: builtins' fc4 hangs (expected.tsv); every other case
# takes under 5 s.
meta bin/sh/builtins functional_test 'timeout="60"'
netbsd_sh bin/sleep sleep_test
tap_sh bin/test legacy_test
meta bin/test legacy_test 'required_user="unprivileged"'

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
# bmake: a TAP legacy_test per leaf directory, common.sh and test-new.mk at
# the top; sysmk's mk/sys.mk.test installs as sys.mk.
files usr.bin/bmake usr.bin/bmake/tests common.sh test-new.mk
for t in $(cd usr.bin/bmake/tests && find . -name legacy_test.sh | sort); do
	d="${t#./}"; d="usr.bin/bmake/${d%/legacy_test.sh}"
	tap_sh "$d" legacy_test "usr.bin/bmake/tests/${t#./}"
	tree_files "$d" "usr.bin/bmake/tests/${d#usr.bin/bmake/}"
done
for m in $(cd usr.bin/bmake/tests && find . -name sys.mk.test); do
	mkdir -p "$T/usr.bin/bmake/${m%/sys.mk.test}"
	install -m 0444 "usr.bin/bmake/tests/$m" "$T/usr.bin/bmake/${m%.test}"
done
# mkimg: each baseline from its .hex less comments; the 4 MB partition.
atf_sh usr.bin/mkimg mkimg_test
atf_sh usr.bin/mkimg mkimg_offset_test
for h in usr.bin/mkimg/tests/*.hex; do
	f="${h##*/}"; sed -e '/^#.*/D' < "$h" > "$T/usr.bin/mkimg/${f%.hex}"; chmod 0444 "$T/usr.bin/mkimg/${f%.hex}"
done
awk 'BEGIN { for (i = 0; i < 2097152; i++) print "P" }' > "$T/usr.bin/mkimg/partition_data_4M.bin"; chmod 0444 "$T/usr.bin/mkimg/partition_data_4M.bin"

# --- sbin ---
atf_c sbin/devd client_test
for m in 'required_files="/var/run/devd.pid"' 'required_programs="devd"' 'required_user="root"' 'timeout="15"'; do
	meta sbin/devd client_test "$m"
done
# dhclient: pcp needs /usr/tests/sys's vnet.subr and vnet jails;
# option-domain-search_test links dhclient's parser with fake.c, built as
# base/dhclient builds them.
atf_sh sbin/dhclient pcp
meta sbin/dhclient pcp 'is_exclusive="true"'
DH="$PROJ/../dhclient/compat"
write_rsp "$B/dh.rsp" "${TARGET_FLAGS[@]}" -O2 -w -fno-common -DINET6 -DWITHOUT_NETLINK \
	-include "$DH/nd_dhclient_compat.h" -I"$DH" -Isbin/dhclient -I"$UTIL/usr/local/include" $(cmd_sysroot_flags "$SYSROOT")
plain_c sbin/dhclient option-domain-search_test "$B/dh.rsp" \
	sbin/dhclient/{alloc,convert,hash,options,tables,parse,conflex,tree}.c sbin/dhclient/tests/{fake,option-domain-search}.c \
	-- -L"$UTIL/usr/lib" -lutil
# ifconfig: every test in a vnet jail.
netbsd_sh sbin/ifconfig nonexistent_test
atf_sh sbin/ifconfig ifconfig
atf_sh sbin/ifconfig inet6
for t in nonexistent_test ifconfig inet6; do
	meta sbin/ifconfig $t 'execenv="jail"'; meta sbin/ifconfig $t 'execenv_jail_params="vnet allow.raw_sockets"'
done
atf_sh sbin/md5 md5_test
# pfctl: pfctl_test reads pfctl's output from the write end of its pipe
# (FreeBSD's pipes are bidirectional): a socketpair here, as pwait_reap's,
# and <sys/module.h> (included, unused) from compat/.
write_rsp "$B/pf.rsp" "@$B/sp.rsp" -I"$PROJ/compat" -Isbin/pfctl/tests -isystem "$SBUF/usr/local/include"
CRSP="$B/pf.rsp" atf_c sbin/pfctl pfctl_test sbin/pfctl/tests/pfctl_test.c -- -L"$SBUF/usr/lib" -lsbuf
atf_sh sbin/pfctl macro
files sbin/pfctl/files sbin/pfctl/tests/files $(cd sbin/pfctl/tests/files && ls pf????.*)
atf_sh sbin/ping ping_test
meta sbin/ping ping_test 'is_exclusive="true"'
files sbin/ping sbin/ping/tests ping_c1_s56_t1.out ping_6_c1_s8_t1.out ping_c1_s56_t1_S127.out ping_c1_s8_t1_S1.out
atf_sh sbin/route basic
meta sbin/route basic 'is_exclusive="true"'
files sbin/route sbin/route/tests utils.subr
atf_sh sbin/sysctl sysctl_test

# --- usr.sbin ---
atf_sh usr.sbin/certctl certctl_test
files usr.sbin/certctl usr.sbin/certctl/tests certctl.subr
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
# makefs: as root; cd9660 and msdos mount what they make (FreeBSD's mount
# programs and modules), ffs through mdconfig.
for t in makefs_cd9660_tests makefs_ffs_tests makefs_msdos_tests; do
	atf_sh usr.sbin/makefs $t
	meta usr.sbin/makefs $t 'required_user="root"'
done
meta usr.sbin/makefs makefs_cd9660_tests 'required_files="/sbin/mount_cd9660"'
meta usr.sbin/makefs makefs_cd9660_tests 'required_kmods="cd9660"'
meta usr.sbin/makefs makefs_msdos_tests 'required_files="/sbin/mount_msdosfs"'
meta usr.sbin/makefs makefs_msdos_tests 'required_kmods="msdosfs"'
script usr.sbin/makefs makefs_tests_common.sh usr.sbin/makefs/tests/makefs_tests_common.sh

# The Kyuafiles: suite.test.mk's for each directory (its tests sorted, as
# _TESTS:O; TEST_METADATA after the name; then include() for each
# TESTS_SUBDIRS directory), tests/Kyuafile for the rest.
for kf in $(cd "$B/kf" && find . -name tests -type f); do
	d="${kf#./}"; d="${d%/tests}"
	while [ "${d%/*/*}" != "$d" ]; do
		printf '%s\n' "${d##*/}" >> "$B/kf/${d%/*}/subdirs"; d="${d%/*}"
	done
done
for kf in $(cd "$B/kf" && find . -mindepth 2 -type d); do
	dir="${kf#./}"; kf="$B/kf/$dir"
	{ echo '-- Automatically generated by bsd.test.mk.'; echo; echo 'syntax(2)'; echo
	  echo 'test_suite("FreeBSD")'; echo
	  [ -f "$kf/tests" ] && LC_ALL=C sort -k2 "$kf/tests" | while read -r iface name; do
		md=""; [ -f "$kf/meta.$name" ] && md="$(awk '{ printf ", %s", $0 }' "$kf/meta.$name")"
		printf '%s_test_program{name="%s"%s}\n' "$iface" "$name" "$md"
	  done
	  [ -f "$kf/subdirs" ] && LC_ALL=C sort -u "$kf/subdirs" | while read -r sub; do
		printf 'include("%s/Kyuafile")\n' "$sub"
	  done; true; } > "$T/$dir/Kyuafile"
	chmod 0444 "$T/$dir/Kyuafile"
done
for d in . bin sbin usr.bin usr.sbin; do install -m 0444 tests/Kyuafile "$T/$d/Kyuafile"; done
# usr.bin/tests' FILES: regress.m4, the TAP driver of the legacy tests
# (usr.bin/tr's legacy_test runs ../regress.m4).
install -m 0444 usr.bin/tests/regress.m4 "$T/usr.bin/"
