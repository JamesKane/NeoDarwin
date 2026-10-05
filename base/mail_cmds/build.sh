#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# mail and mailx from mail_cmds-41 (P4-21 checkpoint 3,
# docs/architecture/freebsd-parity.md §2.1): replays mail_cmds.xcodeproj's
# mail target (INSTALL_PATH /usr/bin, no further settings) with its
# install-misc.sh, and the mailx target, a hard link.
#   build.sh OUT MAIL_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives usr/bin/{mail,mailx}, usr/share/misc/mail.help and
# mail.tildehelp, private/etc/mail.rc, and mail.1 and mailx.1. Sending
# needs a sendmail(8) (/usr/sbin/sendmail), which the base doesn't have.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S/mail"
D="$B/derived"; mkdir -p "$D"

write_vers "$D/mail_vers.c" mail mail_cmds 41
write_rsp "$B/mail.rsp" "${TARGET_FLAGS[@]}" -Os $(cmd_sysroot_flags "$SYSROOT")
tool "$B" "$ROOT" "$OUT/usr/bin/mail" "$B/mail.rsp" cmd1.c cmd2.c cmd3.c util.c cmdtab.c collect.c edit.c fio.c \
	getname.c head.c lex.c list.c main.c names.c popen.c quit.c send.c strings.c temp.c tty.c v7.local.c vars.c \
	version.c "$D/mail_vers.c"
ln -f "$OUT/usr/bin/mail" "$OUT/usr/bin/mailx"
mkdir -p "$OUT/usr/share/misc" "$OUT/private/etc" "$OUT/usr/share/man/man1"
install -m 0644 misc/mail.help misc/mail.tildehelp "$OUT/usr/share/misc/"
install -m 0644 misc/mail.rc "$OUT/private/etc/"
install -m 0444 mail.1 mailx.1 "$OUT/usr/share/man/man1/"
