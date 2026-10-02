#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The first session's commands from shell_cmds-326 (docs/base/libsystem.md):
# replays shell_cmds.xcodeproj's sh, echo, test, pwd, kill, sleep, env, id,
# printf, uname, date and hostname targets (project settings: gnu99,
# __FBSDID=__RCSID, DEAD_CODE_STRIPPING; INSTALL_PATH /usr/bin unless the
# target says /bin), and install-files.sh's [ link.
#   build.sh OUT SHELL_CMDS_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root, //base:libedit_dylib)
# OUT receives bin/sh, bin/{echo,test,[,pwd,kill,sleep,date,hostname,expr}
# and usr/bin/{env,id,groups,whoami,printf,uname,basename,dirname,true,
# false,seq,mktemp,which,tee,xargs,find,hexdump,od,script} and
# usr/libexec/path_helper.
# The sh target is FreeBSD's ash, which Apple installs as /usr/local/bin/ash
# (sh.xcconfig) and links with libedit (OTHER_LDFLAGS -ledit); its /bin/sh
# is closed. NeoDarwin installs ash as /bin/sh, built as sh.xcconfig builds
# it: with history, fc and emacs/vi line editing (set -o emacs, set -o vi).
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
LE=""; for d in "${DEPS[@]}"; do [ -f "$d/usr/lib/libedit.3.dylib" ] && LE="$d"; done
[ -n "$LE" ] || { echo "shell_cmds: no DEPROOT holds usr/lib/libedit.3.dylib (pass //base:libedit_dylib)" >&2; exit 1; }
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
# project's; USER_HEADER_SEARCH_PATHS = BUILT_PRODUCTS_DIR and sh/;
# OTHER_LDFLAGS = -ledit). Its script phases generate builtins (fc among
# them: no mkbuiltins -h), nodes, syntax and tokens: mknodes and mksyntax are
# host programs, compiled for the build machine. libedit's histedit.h is
# build-only in its tree. base.xcconfig adds VERSION_INFO_PREFIX = __.
(cd "$D" && sh "$S/sh/mkbuiltins" "$S/sh")
xcrun -sdk macosx cc "$S/sh/mknodes.c" -o "$B/mknodes"
xcrun -sdk macosx cc "$S/sh/mksyntax.c" -o "$B/mksyntax"
(cd "$D" && "$B/mknodes" "$S/sh/nodetypes" "$S/sh/nodes.c.pat" && "$B/mksyntax" && sh "$S/sh/mktokens")
write_rsp "$B/sh.rsp" "${base[@]}" -DSHELL -iquote "$D" -iquote "$S/sh" -isystem "$LE/usr/local/include"
tool "$B" "$ROOT" "$OUT/bin/sh" "$B/sh.rsp" sh/alias.c sh/arith_yacc.c sh/arith_yylex.c sh/cd.c sh/bltin/echo.c \
	sh/error.c sh/eval.c sh/exec.c sh/expand.c sh/histedit.c sh/input.c sh/jobs.c kill/kill.c sh/mail.c sh/main.c \
	sh/memalloc.c sh/miscbltin.c sh/mystring.c sh/options.c sh/output.c sh/parser.c printf/printf.c sh/redir.c \
	sh/show.c test/test.c sh/trap.c sh/var.c "$D/builtins.c" "$D/nodes.c" "$D/syntax.c" "$(vers ash __)" \
	-- -L"$LE/usr/lib" -ledit

# The other targets: one source each, or two for env and date.
for t in bin/echo:echo/echo.c bin/test:test/test.c bin/pwd:pwd/pwd.c bin/kill:kill/kill.c bin/sleep:sleep/sleep.c \
	bin/date:date/vary.c,date/date.c bin/hostname:hostname/hostname.c usr/bin/env:env/env.c,env/envopts.c \
	usr/bin/printf:printf/printf.c usr/bin/uname:uname/uname.c; do
	dest="${t%%:*}"; IFS=, read -r -a srcs <<< "${t#*:}"
	tool "$B" "$ROOT" "$OUT/$dest" "$B/cflags" "${srcs[@]}" "$(vers "${dest##*/}")"
done
# path_helper (INSTALL_PATH /usr/libexec): /etc/profile (bash, sh) and
# /etc/zprofile (zsh) run it if it is executable, to set PATH from /etc/paths
# and /etc/paths.d (MANPATH from /etc/manpaths if it is set).
tool "$B" "$ROOT" "$OUT/usr/libexec/path_helper" "$B/cflags" path_helper/path_helper.c "$(vers path_helper)"
# id adds USE_BSM_AUDIT (getaudit_addr(2), a libsystem_kernel call; no libbsm).
write_rsp "$B/id.rsp" "${base[@]}" -D__FBSDID=__RCSID -DUSE_BSM_AUDIT
tool "$B" "$ROOT" "$OUT/usr/bin/id" "$B/id.rsp" id/id.c "$(vers id)"

# install-files.sh: hard links, which the install tree holds as copies.
cp "$OUT/bin/test" "$OUT/bin/["
cp "$OUT/usr/bin/id" "$OUT/usr/bin/groups"; cp "$OUT/usr/bin/id" "$OUT/usr/bin/whoami"

# The ZFS test suite's commands (docs/architecture/filesystems.md §7): one
# source each, INSTALL_PATH /usr/bin; expr's target says /bin and adds
# OTHER_CFLAGS -fwrapv. expr.y and find's getdate.y go through the
# toolchain's yacc, as Xcode's yacc rule runs it.
for t in basename dirname true false seq mktemp which tee script; do
	tool "$B" "$ROOT" "$OUT/usr/bin/$t" "$B/cflags" "$t/$t.c" "$(vers "$t")"
done
xcrun yacc -o "$D/expr.c" expr/expr.y
write_rsp "$B/expr.rsp" "${base[@]}" -D__FBSDID=__RCSID -fwrapv
tool "$B" "$ROOT" "$OUT/bin/expr" "$B/expr.rsp" "$D/expr.c" "$(vers expr)"
# find: its GCC_PREPROCESSOR_DEFINITIONS add _DARWIN_USE_64_BIT_INODE;
# xargs and find include Libc's private get_compat.h (compat/).
write_rsp "$B/find.rsp" "${base[@]}" -D__FBSDID=__RCSID -D_DARWIN_USE_64_BIT_INODE -I"$PROJ/compat" -iquote find
xcrun yacc -o "$D/getdate.c" find/getdate.y
tool "$B" "$ROOT" "$OUT/usr/bin/find" "$B/find.rsp" find/find.c find/function.c "$D/getdate.c" find/ls.c \
	find/main.c find/misc.c find/operator.c find/option.c "$(vers find)"
write_rsp "$B/xargs.rsp" "${base[@]}" -D__FBSDID=__RCSID -I"$PROJ/compat"
tool "$B" "$ROOT" "$OUT/usr/bin/xargs" "$B/xargs.rsp" xargs/strnsubst.c xargs/xargs.c "$(vers xargs)"
tool "$B" "$ROOT" "$OUT/usr/bin/hexdump" "$B/cflags" hexdump/conv.c hexdump/display.c hexdump/hexdump.c \
	hexdump/hexsyntax.c hexdump/odsyntax.c hexdump/parse.c "$(vers hexdump)"
# install-files.sh: od is a hard link to hexdump (a copy here).
cp "$OUT/usr/bin/hexdump" "$OUT/usr/bin/od"
