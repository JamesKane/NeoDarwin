#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The first session's commands from shell_cmds-326 (docs/base/libsystem.md):
# replays shell_cmds.xcodeproj's sh, echo, test, pwd, kill, sleep, env, id,
# printf, uname, date and hostname targets (project settings: gnu99,
# __FBSDID=__RCSID, DEAD_CODE_STRIPPING; INSTALL_PATH /usr/bin unless the
# target says /bin), and install-files.sh's [ link.
#   build.sh OUT SHELL_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# OUT receives bin/sh, bin/{echo,test,[,pwd,kill,sleep,date,hostname} and
# usr/bin/{env,id,groups,whoami,printf,uname}.
# The sh target is FreeBSD's ash, which Apple installs as /usr/local/bin/ash
# (sh.xcconfig) and links with libedit; its /bin/sh is closed. NeoDarwin
# installs ash as /bin/sh, built as FreeBSD builds it without libedit
# (-DNO_HISTORY, mkbuiltins -h): no line editing or history yet.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
PROJ="$(cd "$(dirname "$0")" && pwd)"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S" "$B/src" "$PROJ/patches")"   # patches/ applied
cd "$S"
D="$B/derived"; mkdir -p "$D"

# The project's Release settings; warning flags change no interface and are
# left out. VERSIONING_SYSTEM = apple-generic, CURRENT_PROJECT_VERSION 326.
base=("${TARGET_FLAGS[@]}" -Os -std=gnu99 -fno-common $(cmd_sysroot_flags "$SYSROOT"))
write_rsp "$B/cflags" "${base[@]}" -D__FBSDID=__RCSID
# vers NAME [PREFIX]: the target's version symbols, as a source to compile.
vers() { write_vers "$D/${1}_vers.c" "$1" shell_cmds 326 "${2:-}"; printf '%s' "$D/${1}_vers.c"; }

# sh: sh.xcconfig (GCC_PREPROCESSOR_DEFINITIONS = SHELL, which replaces the
# project's; USER_HEADER_SEARCH_PATHS = BUILT_PRODUCTS_DIR and sh/) with
# -DNO_HISTORY. Its script phases generate builtins, nodes, syntax and tokens:
# mknodes and mksyntax are host programs, compiled for the build machine.
# base.xcconfig adds VERSION_INFO_PREFIX = __.
(cd "$D" && sh "$S/sh/mkbuiltins" -h "$S/sh")
xcrun -sdk macosx cc "$S/sh/mknodes.c" -o "$B/mknodes"
xcrun -sdk macosx cc "$S/sh/mksyntax.c" -o "$B/mksyntax"
(cd "$D" && "$B/mknodes" "$S/sh/nodetypes" "$S/sh/nodes.c.pat" && "$B/mksyntax" && sh "$S/sh/mktokens")
write_rsp "$B/sh.rsp" "${base[@]}" -DSHELL -DNO_HISTORY -iquote "$D" -iquote "$S/sh"
tool "$B" "$ROOT" "$OUT/bin/sh" "$B/sh.rsp" sh/alias.c sh/arith_yacc.c sh/arith_yylex.c sh/cd.c sh/bltin/echo.c \
	sh/error.c sh/eval.c sh/exec.c sh/expand.c sh/histedit.c sh/input.c sh/jobs.c kill/kill.c sh/mail.c sh/main.c \
	sh/memalloc.c sh/miscbltin.c sh/mystring.c sh/options.c sh/output.c sh/parser.c printf/printf.c sh/redir.c \
	sh/show.c test/test.c sh/trap.c sh/var.c "$D/builtins.c" "$D/nodes.c" "$D/syntax.c" "$(vers ash __)"

# The other targets: one source each, or two for env and date.
for t in bin/echo:echo/echo.c bin/test:test/test.c bin/pwd:pwd/pwd.c bin/kill:kill/kill.c bin/sleep:sleep/sleep.c \
	bin/date:date/vary.c,date/date.c bin/hostname:hostname/hostname.c usr/bin/env:env/env.c,env/envopts.c \
	usr/bin/printf:printf/printf.c usr/bin/uname:uname/uname.c; do
	dest="${t%%:*}"; IFS=, read -r -a srcs <<< "${t#*:}"
	tool "$B" "$ROOT" "$OUT/$dest" "$B/cflags" "${srcs[@]}" "$(vers "${dest##*/}")"
done
# id adds USE_BSM_AUDIT (getaudit_addr(2), a libsystem_kernel call; no libbsm).
write_rsp "$B/id.rsp" "${base[@]}" -D__FBSDID=__RCSID -DUSE_BSM_AUDIT
tool "$B" "$ROOT" "$OUT/usr/bin/id" "$B/id.rsp" id/id.c "$(vers id)"

# install-files.sh: hard links, which the install tree holds as copies.
cp "$OUT/bin/test" "$OUT/bin/["
cp "$OUT/usr/bin/id" "$OUT/usr/bin/groups"; cp "$OUT/usr/bin/id" "$OUT/usr/bin/whoami"
