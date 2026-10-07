#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# The pinned cc_toolchain's linker (toolchains/llvm.BUILD.tpl): llvm.org's
# ld64.lld, with one translation. rules_swift's generic swift_toolchain names
# a binary's entry point with GNU ld's `--defsym main=MODULE_main`; Mach-O
# linkers spell that `-alias _MODULE_main _main`.
real="$(dirname "$0")/../bin/ld64.lld"
n=$#
while [ "$n" -gt 0 ]; do
	a=$1; shift; n=$((n - 1))
	if [ "$a" = "--defsym" ] && [ "$n" -gt 0 ]; then
		d=$1; shift; n=$((n - 1))
		set -- "$@" -alias "_${d#*=}" "_${d%%=*}"
	else
		case "$a" in
		--defsym=*) d=${a#--defsym=}; set -- "$@" -alias "_${d#*=}" "_${d%%=*}" ;;
		*) set -- "$@" "$a" ;;
		esac
	fi
done
exec "$real" "$@"
