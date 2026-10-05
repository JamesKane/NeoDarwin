#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Merge base_library install trees into the runtime root of the userland base
# (docs/base/libsystem.md): each tree's files at their install paths. usr/local
# (the static archives dyld links) is build-only and left out. Relative
# symbolic links are kept, and restored where Bazel copied them. Two trees
# installing the same path is an error.
#   stage_root.sh OUT TREE...
set -euo pipefail
out="$1"; shift
rm -rf "$out"; mkdir -p "$out"
for tree in "$@"; do
	(cd "$tree" && find . \( -type f -o -type l \) -not -path './usr/local/*') | while IFS= read -r p; do
		if [ -e "$out/$p" ] || [ -L "$out/$p" ]; then
			echo "stage_root.sh: $p is installed by two libraries" >&2; exit 1
		fi
		mkdir -p "$out/$(dirname "$p")"
		# A library's own links (libSystem.dylib -> libSystem.B.dylib) are
		# relative and kept; anything else, including the sandbox's links to
		# the input files, is copied as the file it names.
		if [ -L "$tree/$p" ] && case "$(readlink "$tree/$p")" in /*) false ;; *) true ;; esac; then
			ln -s "$(readlink "$tree/$p")" "$out/$p"
		else
			cp -L "$tree/$p" "$out/$p"
		fi
	done
done
# Bazel stores a tree's relative links as copies (libSystem.dylib, libm.dylib
# and the other BSD names, libc++.dylib, libobjc.dylib). A dylib under a name
# other than its install name, with that install name in the root, becomes a
# link to it again.
find "$out" -type f -name '*.dylib' | while IFS= read -r f; do
	id="$(xcrun otool -D "$f" | sed -n 2p)"
	[ -n "$id" ] && [ "$out$id" != "$f" ] && [ -f "$out$id" ] || continue
	[ "$(dirname "$out$id")" = "$(dirname "$f")" ] || continue
	cmp -s "$f" "$out$id" || continue
	rm "$f"; ln -s "$(basename "$id")" "$f"
done
# A framework's links, which a tree artifact doesn't keep: Versions/Current
# to its one version, and the binary at the top to Current's.
for fw in "$out"/System/Library/Frameworks/*.framework; do
	[ -d "$fw/Versions/A" ] || continue
	name="$(basename "$fw" .framework)"
	ln -sfn A "$fw/Versions/Current"
	ln -sfn "Versions/Current/$name" "$fw/$name"
done
chmod -R a+rX,u+w "$out"
# The whatis database apropos(1) and whatis(1) search (man-62's man.sh greps
# usr/share/man/whatis): mandoc's makewhatis indexes the pages, then its
# apropos lists every entry as "name(section) - description". Both are the
# root's own mandoc, run on the build machine: NeoDarwin's userland binaries
# link only libSystem and libz there, which macOS has. A page installed under
# several names is a copy each time (gunzip.1, zcat.1 for gzip.1); makewhatis
# runs on a tree of hard links where identical pages of a section share one
# file, so it makes one entry of them with every name, as it does for
# FreeBSD's MLINKS. mandoc.db itself isn't kept; man-62 doesn't read it.
if [ -x "$out/usr/bin/makewhatis" ] && [ -d "$out/usr/share/man" ]; then
	tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
	ln -s "$(cd "$out" && pwd)/usr/bin/mandoc" "$tmp/apropos"
	for sect in "$out"/usr/share/man/man*/; do
		s="$(basename "$sect")"; mkdir -p "$tmp/man/$s"
		(cd "$sect" && find . -maxdepth 1 -type f | sed 's|^\./||' | LC_ALL=C sort) | while IFS= read -r f; do
			sum="$(cksum < "$sect/$f" | tr -c '0-9\n' _)"
			if [ -e "$tmp/sums/$s/$sum" ]; then
				ln "$tmp/man/$s/$(cat "$tmp/sums/$s/$sum")" "$tmp/man/$s/$f"
			else
				mkdir -p "$tmp/sums/$s"; printf '%s' "$f" > "$tmp/sums/$s/$sum"
				cp "$sect/$f" "$tmp/man/$s/$f"
			fi
		done
	done
	"$out/usr/bin/makewhatis" "$tmp/man"
	"$tmp/apropos" -M "$tmp/man" 'Nm~.' | LC_ALL=C sort -u > "$tmp/whatis"
	[ -s "$tmp/whatis" ] || { echo "stage_root.sh: makewhatis indexed no pages" >&2; exit 1; }
	install -m 0444 "$tmp/whatis" "$out/usr/share/man/whatis"
fi
