#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# /etc for the first interactive session (docs/base/libsystem.md): files-968's
# private/etc install-files for macOS, and NeoDarwin's user and group
# databases.
#   build.sh OUT FILES_SRC SYSROOT   (no DEPROOT)
# OUT receives private/etc/..., where macOS keeps /etc. The /etc ->
# private/etc link (files' Makefile) and the directories the hierarchy files
# create (private/var/root, private/var/empty, private/tmp) belong to the
# image, as do modes: master.passwd is 0600 on macOS.
# Upstream files install unmodified. ttys keeps macOS's console entry
# (loginwindow): launchd-842 doesn't read ttys, so getty runs from its
# launchd job (base/system_cmds), and login(1) only reads the entry's
# "secure" flag (root may log in on the console) and type (TERM=vt100).
# master.passwd and group are NeoDarwin's (Apple publishes only the iPhone
# ones); passwd is derived from master.passwd as the Makefile does for them.
source "$(dirname "$0")/../../tools/base/common.sh"
OUT="$(abspath "$1")"; F="$(abspath "$2")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
F="$(stage_src "$F" "$B/src" "$PROJ/patches")"   # patches/ (none yet) applied
cd "$F/private/etc"
E="$OUT/private/etc"; mkdir -p "$E"

# private/etc/Makefile, install-files, CONTENT_PLATFORM osx.
for f in afpovertcp.cfg ftpusers gettytab kern_loader.conf shells ttys hosts manpaths networks paths \
	protocols rpc services; do
	install -m 0644 "$f" "$E/$f"
done
for f in find.codes hosts.equiv rmtab xtab; do : > "$E/$f"; chmod 0644 "$E/$f"; done
# The resolv.conf -> ../var/run/resolv.conf link is left out: it dangles
# until a resolver writes it, which a Bazel tree artifact doesn't hold.
# SRC_PASSWD and SRC_GROUP, NeoDarwin's; passwd as the Makefile derives it
install -m 0600 "$PROJ/master.passwd" "$E/master.passwd"
install -m 0644 "$PROJ/group" "$E/group"
# (cut -d : -f 1-4,8-10), keeping the comment lines, with "*" for every
# password, as pwd_mkdb -p writes it: the file is world-readable, and only
# root's lookups (login, su and passwd, which are setuid) read hashes, from
# master.passwd.
awk -F: -v OFS=: '/^#/ { print; next } { print $1, "*", $3, $4, $8, $9, $10 }' "$PROJ/master.passwd" > "$E/passwd"
chmod 0644 "$E/passwd"
