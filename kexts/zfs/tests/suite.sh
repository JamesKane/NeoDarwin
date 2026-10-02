#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The OpenZFS test suite's scripts for NeoDarwin (P3-01 checkpoint 2,
# docs/architecture/filesystems.md §7): the groups of kexts/zfs/tests/groups.txt
# and what they include, with kexts/zfs/tests/patches applied, and the
# runner that replaces zfs-tests.sh and test-runner.py.
#   suite.sh OUT ZFS_SRC SYSROOT
# OUT receives usr/share/zfs/zfs-tests (STF_SUITE: include/, callbacks/,
# tests/functional/..., neodarwin.run), usr/share/zfs/test-runner/include
# (STF_TOOLS: logapi.shlib) and usr/share/zfs/nd-zfs-tests. The helpers
# (STF_SUITE/bin) are //kexts/zfs:zfs_test_commands.
#
# neodarwin.run is generated from tests/runfiles/common.run: for each group,
#   DIR TIMEOUT PRE POST TEST...
# with common.run's [DEFAULT] (pre setup, post cleanup, timeout 600) unless
# the group's section overrides them, and "-" for a pre or post script the
# directory doesn't have (test-runner.py would drop such a group).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }
OUT="$(abspath "$1")"; Z="$(abspath "$2")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
mkdir -p "$B/src"
(cd "$Z" && tar chf - tests/zfs-tests/include tests/zfs-tests/callbacks tests/zfs-tests/tests/functional \
	tests/test-runner/include tests/runfiles/common.run) | (cd "$B/src" && tar xf -)
chmod -R u+w "$B/src"
for p in "$HERE"/patches/*.patch; do [ -e "$p" ] && patch -d "$B/src" -p1 --quiet < "$p"; done
T="$B/src/tests/zfs-tests"
ZGROUPS=($(awk '!/^#/ && NF { print $1 }' "$HERE/groups.txt"))

SUITE="$OUT/usr/share/zfs/zfs-tests"
mkdir -p "$SUITE/tests/functional/cli_root" "$OUT/usr/share/zfs/test-runner"
cp -R "$T/include" "$T/callbacks" "$SUITE/"
rm -f "$SUITE/include/default.cfg.in" "$SUITE/include/Makefile.am"
# default.cfg as configure writes it, with FreeBSD's paths (userland.sh).
sed -e 's|@sysconfdir@|/etc|g' -e 's|@zfsexecdir@|/usr/libexec/zfs|g' -e 's|@datadir@|/usr/share|g' \
	-e 's|@ASAN_ENABLED@|no|g' -e 's|@UBSAN_ENABLED@|no|g' "$T/include/default.cfg.in" > "$SUITE/include/default.cfg"
! grep -n '@[a-zA-Z_]*@' "$SUITE/include/default.cfg" || { echo "suite.sh: unsubstituted default.cfg" >&2; exit 1; }
cp -R "$B/src/tests/test-runner/include" "$OUT/usr/share/zfs/test-runner/"

# The groups, and the directories they include from (cli_root's
# zfs_load-key, zfs_upgrade, zpool_reopen, zpool_upgrade's pool images, and
# removal's library). zpool_create's draidcfg.gz (21 MB) is left out: its
# only user, zpool_create_draid_004_pos, needs the draid helper, which
# links libzpool (not built yet).
C="$T/tests/functional/cli_root"
cp "$C/cli_common.kshlib" "$SUITE/tests/functional/cli_root/"
for d in "${ZGROUPS[@]}" zfs_load-key zfs_upgrade zpool_reopen zpool_upgrade; do
	cp -R "$C/$d" "$SUITE/tests/functional/cli_root/"
done
rm -f "$SUITE/tests/functional/cli_root/zpool_create/draidcfg.gz"
mkdir -p "$SUITE/tests/functional/removal"
cp "$T/tests/functional/removal/removal.kshlib" "$SUITE/tests/functional/removal/"
find "$SUITE" -name Makefile.am -delete

# neodarwin.run.
awk -v groups="${ZGROUPS[*]}" -v suite="$SUITE" '
function flush(   n, i, g, pre, post) {
	if (sect == "") return
	if (sect !~ /^tests\/functional\/cli_root\//) return
	g = sect; sub(/.*\//, "", g)
	if (!(g in want)) return
	pre = ("pre" in kv) ? kv["pre"] : dpre; post = ("post" in kv) ? kv["post"] : dpost
	if (pre == "" || system("test -x " suite "/" sect "/" pre ".ksh") != 0) pre = "-"
	if (post == "" || system("test -x " suite "/" sect "/" post ".ksh") != 0) post = "-"
	line[g] = sect " " (("timeout" in kv) ? kv["timeout"] : dto) " " pre " " post " " tests
}
BEGIN { n = split(groups, gs, " "); for (i = 1; i <= n; i++) want[gs[i]] = 1; dpre = "setup"; dpost = "cleanup"; dto = 600 }
/^\[/ { flush(); sect = substr($0, 2, length($0) - 2); delete kv; tests = ""; intests = 0; next }
/^#/ { next }
/^[a-z_]+ *=/ {
	key = $1; val = $0; sub(/^[^=]*= */, "", val)
	if (sect == "DEFAULT") { if (key == "pre") dpre = val; if (key == "post") dpost = val; if (key == "timeout") dto = val; next }
	if (key == "tests") { intests = 1; tests = ""; $0 = val } else { kv[key] = val; intests = 0; next }
}
intests {
	s = $0; gsub(/[][,'\'']/, " ", s); gsub(/  */, " ", s); sub(/^ /, "", s); sub(/ $/, "", s)
	if (s != "") tests = (tests == "" ? s : tests " " s)
	if ($0 ~ /\]/) intests = 0
}
END { flush(); for (i = 1; i <= n; i++) { if (!(gs[i] in line)) { print "no section for " gs[i] > "/dev/stderr"; exit 1 } print line[gs[i]] } }
' "$B/src/tests/runfiles/common.run" > "$SUITE/neodarwin.run"
install -m 0755 "$HERE/nd-zfs-tests.ksh" "$OUT/usr/share/zfs/nd-zfs-tests"
