#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# Re-sign a Mach-O ad hoc with an identifier and, optionally, entitlements,
# as a NeoDarwin image's test binaries need (P1-15): the host's codesign(1)
# writes the XML and DER entitlement slots. A different identifier gives
# the same code a different cdhash.
#   sign.sh IN OUT IDENTIFIER [ENTITLEMENTS]
set -euo pipefail
in="$1"; out="$2"; id="$3"; ent="${4:-}"
cp "$in" "$out"; chmod u+w "$out"
if [ -n "$ent" ]; then
	codesign -s - -f -i "$id" --entitlements "$ent" "$out" 2>/dev/null
else
	codesign -s - -f -i "$id" "$out" 2>/dev/null
fi
