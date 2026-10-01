#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Plant one bug in nd_tc956x.h for the simulation's mutation tests
# (BUILD.bazel, tc956x_mutation_tests): mutate.sh NAME MUTATIONS IN OUT.
# MUTATIONS has one line per bug, "name @@ old @@ new", with \n and \t
# escapes; old must occur exactly once in IN, so a mutation that no longer
# applies fails the build instead of passing unnoticed.
set -eu
name="$1"
list="$2"
in="$3"
out="$4"
awk -v name="$name" -v listfile="$list" '
BEGIN {
	found = 0
	while ((getline line < listfile) > 0) {
		if (index(line, name " @@ ") != 1) continue
		n = split(line, f, / @@ /)
		if (n != 3) { print "mutate.sh: malformed mutation " name > "/dev/stderr"; exit 1 }
		old = f[2]; new = f[3]
		gsub(/\\n/, "\n", old); gsub(/\\t/, "\t", old)
		gsub(/\\n/, "\n", new); gsub(/\\t/, "\t", new)
		found = 1
	}
	if (!found) { print "mutate.sh: no mutation " name > "/dev/stderr"; exit 1 }
}
{ buf = buf $0 "\n" }
END {
	if (!found) exit 1
	i = index(buf, old)
	if (i == 0) { print "mutate.sh: " name ": the text to change is not in the header" > "/dev/stderr"; exit 1 }
	if (index(substr(buf, i + 1), old) != 0) { print "mutate.sh: " name ": the text occurs more than once" > "/dev/stderr"; exit 1 }
	printf "%s%s%s", substr(buf, 1, i - 1), new, substr(buf, i + length(old))
}' "$in" > "$out"
