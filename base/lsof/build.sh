#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# lsof from lsof-76 (lsof 4.91, the macOS 26.0 release; P4-21 checkpoint 6,
# docs/architecture/freebsd-parity.md §2.1): /usr/sbin/lsof, the libproc
# dialect (dialects/darwin/libproc), and its page lsof.8. FreeBSD's fstat,
# procstat and sockstat rows are its equivalents.
#   build.sh OUT LSOF_SRC SYSROOT DEPROOT...   (DEPROOT: //base:root)
# Replays what the project's Makefile has Configure do for Darwin 21 and
# later (`Configure -n darwin`, then Apple's edits): the dialect's sources
# copied into the top directory (Mksrc with LSOF_MKC=cp), lib/'s liblsof
# objects, and CFGF -DHASIPv6 -fno-common -DDARWINV=2100, with
# HAS_STRFTIME and HASUTMPX (Configure's probes find both in the SDK) and
# no -lcurses or -mdynamic-no-pic (Apple's edits). Apple's Makefile comments
# out machine.h's HASKERNIDCK (the kernel-identity check against
# /mach_kernel) and empties version.h's build host, user and compiler
# strings; so does this. dchannel.c and dnexus.c, which the dialect's
# Makefile names, aren't in the published project (Skywalk channels and
# nexuses); nothing else refers to them. Apple installs lsof mode 0755, not
# setuid: root sees every process, others their own.
source "$(dirname "$0")/../../tools/base/common.sh"
source "$(dirname "$0")/../commands.sh"
OUT="$(abspath "$1")"; S="$(abspath "$2")"; SYSROOT="$(abspath "$3")"; shift 3
DEPS=(); for d in "$@"; do DEPS+=("$(abspath "$d")"); done
ROOT="$(find_root "${DEPS[@]}")"
B="$(mktemp -d)"; trap 'rm -rf "$B"' EXIT
S="$(stage_src "$S/lsof" "$B/src" /nonexistent)"
cd "$S"
D=dialects/darwin/libproc
DSRC=(ddev.c dfile.c dmnt.c dproc.c dsock.c dstore.c dnetpolicy.c)
for f in dlsof.h dproto.h machine.h "${DSRC[@]}"; do cp "$D/$f" .; done
sed 's@^.*\(#define.*HASKERNIDCK.*1\).*@/* \1 */@' machine.h > machine.h.new && mv machine.h.new machine.h
{
	for v in BLDCMT CC CCV CCDATE CCFLAGS; do printf '#define\tLSOF_%s\t""\n' "$v"; done
	printf '#define  LSOF_CINFO      "libproc-based"\n'
	for v in HOST LDFLAGS LOGNAME SYSINFO USER; do printf '#define\tLSOF_%s\t""\n' "$v"; done
	sed -n '/VN/s/.ds VN \(.*\)/#define	LSOF_VERSION	"\1"/p' < version
} > version.h
write_rsp "$B/cflags" "${TARGET_FLAGS[@]}" -Os -w -DHASIPv6 -fno-common -DDARWINV=2100 -DHAS_STRFTIME -DHASUTMPX \
	$(cmd_sysroot_flags "$SYSROOT") -ffile-prefix-map="$S/"=lsof/
tool "$B" "$ROOT" "$OUT/usr/sbin/lsof" "$B/cflags" "${DSRC[@]}" arg.c main.c misc.c node.c print.c proc.c store.c \
	usage.c util.c lib/{ckkv,cvfs,dvch,fino,isfn,lkud,pdvn,prfp,ptti,rdev,regex,rmnt,rnam,rnch,rnmh,snpf}.c
