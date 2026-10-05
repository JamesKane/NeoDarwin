#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Run a base project's build script, then install the manual pages its
# source tree ships for the programs it installed (base_library's
# man_pages, rules/base.bzl; P4-21, docs/architecture/freebsd-parity.md §2.1).
#   pages.sh [-a PROGRAM=PAGE]... -- SCRIPT OUT SRC SYSROOT DEPROOT...
# A program is any file in OUT's bin, sbin, usr/bin, usr/sbin and
# usr/libexec. Its page is SRC's PROGRAM.N (N a section, 1 to 9), installed
# as usr/share/man/manN/PROGRAM.N, unless the build script installed a page
# for it already. Where SRC has several, the one in a directory named after
# the program (PROGRAM/, PROGRAM.tproj/) wins, then the shortest path; tests
# directories are skipped. A program without a page of its own that is a
# link to one with a page (a symbolic link, a hard link, or the identical
# copy Bazel's tree keeps of one: gunzip, sha256, unxz) gets a copy of that page under its own name, as FreeBSD's MLINKS
# give it one; -a names the page for programs installed as copies
# (cpio=bsdcpio). Pages are copies, not links: the install tree keeps
# neither kind (tools/base/stage_root.sh).
set -euo pipefail
aliases=" "   # " PROGRAM=PAGE ..." (bash 3.2: no associative arrays)
while [ "$1" != "--" ]; do
	case "$1" in -a) aliases="$aliases$2 "; shift 2 ;; *) echo "pages.sh: bad option $1" >&2; exit 2 ;; esac
done
shift
script="$1"; shift
"$script" "$@"
out="$1"; src="$2"
case "$out" in /*) ;; *) out="$PWD/$out" ;; esac
case "$src" in /*) ;; *) src="$PWD/$src" ;; esac
man="$out/usr/share/man"

# Every page file in SRC, once (find -L: the sources are links into the
# repository cache).
pagelist="$(mktemp)"; trap 'rm -f "$pagelist"' EXIT
(cd "$src" && find -L . -type f -name '*.[1-9]' -not -path '*/tests/*' -not -path '*/Tests/*' |
	sed 's|^\./||') > "$pagelist"

# has_page PROGRAM: prints the installed page's path if there is one.
has_page() { { ls "$man"/man[1-9]/"$1".[1-9]* 2>/dev/null || true; } | head -1; }
# find_page NAME: prints SRC's best page for NAME.
find_page() {
	local n="$1" re
	re="$(printf '%s' "$n" | sed 's/[][\.*^$+?(){}|]/\\&/g')"
	{ grep -E "(^|/)${re}\.[1-9]\$" "$pagelist" || true; } |
		awk -v n="$n" '{ d = $0; sub(/\/[^\/]*$/, "", d); sub(/.*\//, "", d); sub(/\.tproj$/, "", d);
			print (d == n ? 0 : 1), length($0), $0 }' | sort -n -k1,1 -k2,2 | head -1 | cut -d' ' -f3-
}
install_page() {   # install_page FILE NAME: FILE as NAME.<FILE's section>
	local f="$1" n="$2" s="${1##*.}"
	mkdir -p "$man/man$s"
	install -m 0444 "$f" "$man/man$s/$n.$s"
}

progs=()
for d in bin sbin usr/bin usr/sbin usr/libexec; do
	[ -d "$out/$d" ] || continue
	while IFS= read -r p; do progs+=("$out/$d/$p"); done < <(cd "$out/$d" && find . -maxdepth 1 \( -type f -o -type l \) | sed 's|^\./||' | sort)
done
[ ${#progs[@]} -gt 0 ] || exit 0

# Pass 1: programs with a page of their own (or named with -a).
missing=()
for p in "${progs[@]}"; do
	n="$(basename "$p")"
	[ -n "$(has_page "$n")" ] && continue
	a="${aliases#* $n=}"; [ "$a" = "$aliases" ] && a="$n" || a="${a%% *}"
	f="$(find_page "$a")"
	if [ -n "$f" ]; then install_page "$src/$f" "$n"; else missing+=("$p"); fi
done
# Pass 2: links to a program with a page.
for p in ${missing[@]+"${missing[@]}"}; do
	n="$(basename "$p")"; t=""
	if [ -L "$p" ]; then
		t="$(basename "$(readlink "$p")")"
	else
		# A hard link, or the copy a build script makes for one.
		for q in "${progs[@]}"; do
			[ "$q" != "$p" ] && [ -n "$(has_page "$(basename "$q")")" ] || continue
			if [ "$q" -ef "$p" ] || cmp -s "$q" "$p"; then t="$(basename "$q")"; break; fi
		done
	fi
	[ -n "$t" ] || continue
	f="$(has_page "$t")"
	[ -n "$f" ] && install_page "$f" "$n"
done
exit 0
