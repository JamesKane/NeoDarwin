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
chmod -R a+rX,u+w "$out"
