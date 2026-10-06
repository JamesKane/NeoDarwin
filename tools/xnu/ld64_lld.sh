#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# ld64.lld in place of ld64 for XNU's kernel link (P0-06, the
# --//rules:kernel_linker=lld experiment; docs/architecture/build-system.md
# "Linking the kernel with ld64.lld"). clang runs this as its linker
# (--ld-path); it runs $ND_LD64_LLD with ld64's arguments, translated where
# lld spells them differently:
#   -alias_list FILE   lld doesn't implement it: each "symbol alias" line
#                      becomes -alias symbol alias.
#   -no_fixup_chains   added: with chained fixups lld turns every
#                      __mod_init_func into __TEXT,__init_offsets, which
#                      dissolves __LASTDATA_CONST (lastkernelconstructor.o);
#                      rebase opcodes keep the sections.
# Every other ld64 flag lld lacks (-static, -image_base, -segaddr,
# -segment_order, -add_split_seg_info, -version_load_command) is passed
# through, and lld warns and ignores it: the result links but isn't a kernel
# kcgen accepts (build-system.md lists the gaps).
set -euo pipefail
LLD="${ND_LD64_LLD:-}"
[ -n "$LLD" ] && [ -x "$LLD" ] || { echo "ld64_lld.sh: ND_LD64_LLD ('$LLD') is not an executable ld64.lld" >&2; exit 1; }
args=(-no_fixup_chains)
while [ $# -gt 0 ]; do
	case "$1" in
	-alias_list)
		while read -r sym alias _; do
			case "$sym" in ''|'#'*) continue ;; esac
			args+=(-alias "$sym" "$alias")
		done < "$2"
		shift 2 ;;
	*) args+=("$1"); shift ;;
	esac
done
exec "$LLD" "${args[@]}"
