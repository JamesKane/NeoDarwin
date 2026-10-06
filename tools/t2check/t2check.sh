#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# The T2 gate (language policy §2, allocation-free Swift):
#   t2check.sh EMBEDDED_TOOLCHAIN MODULE SRC.swift...
# 1. annotations: every public or open func and init carries @_noLocks,
#    the stricter performance annotation: no locks, and so no allocation,
#    reference counting or metadata instantiation, which may lock
#    (@_noAllocation alone still allows reference counting);
# 2. the pinned Xcode swiftc compiles the module, where the performance
#    diagnostics reject an allocation, lock or metadata use reachable from an
#    annotated entry point (an error, not a warning);
# 3. the Embedded Swift toolchain compiles it again with -no-allocations,
#    which rejects an allocating type anywhere in the module, annotated or
#    not, so a stray allocation in a helper fails too.
# A T2 module is a leaf (standard library only), so it compiles standalone.
set -euo pipefail
tc="$1"; module="$2"; shift 2
[ $# -gt 0 ] || { echo "t2check: no sources" >&2; exit 2; }
work="$(mktemp -d "${TEST_TMPDIR:-${TMPDIR:-/tmp}}/t2check.XXXXXX")"; trap 'rm -rf "$work"' EXIT
fail=0

# 1. Attribute lines before a declaration accumulate; a public/open func or
# init must have @_noLocks among them or on its own line.
for f in "$@"; do
	awk -v file="$f" '
		/^[[:space:]]*\/\// { next }
		/^[[:space:]]*@[A-Za-z_]+(\([^)]*\))?([[:space:]]+@[A-Za-z_]+(\([^)]*\))?)*[[:space:]]*$/ { attrs = attrs " " $0; next }
		{
			line = attrs " " $0; attrs = ""
			if ($0 ~ /(^|[[:space:]])(public|open)[[:space:]]/ && $0 ~ /(^|[[:space:]])(func|init)[[:space:](<?]/ &&
			    line !~ /@_noLocks/) {
				printf "%s:%d: error: T2 entry point without @_noLocks\n", file, NR; bad = 1
			}
		}
		END { exit bad }' "$f" || fail=1
done
[ $fail -eq 0 ] || exit 1

# 2. Hosted compile with the performance diagnostics (always on for annotated code).
xcrun swiftc -parse-as-library -wmo -O -swift-version 6 -warnings-as-errors \
	-module-name "$module" -module-cache-path "$work/mc" -emit-object -o "$work/hosted.o" "$@"

# 3. Embedded -no-allocations: the module-wide second check.
"$tc/usr/bin/swiftc" -target aarch64-none-none-elf -enable-experimental-feature Embedded -no-allocations \
	-parse-as-library -wmo -O -swift-version 6 -warnings-as-errors \
	-module-name "$module" -module-cache-path "$work/mc-embedded" -emit-object -o "$work/embedded.o" "$@"
echo "t2check: $module ok ($# file(s))"
