# SPDX-License-Identifier: BSD-2-Clause
# Shared helpers for the XNU build actions. Sourced, not executed.
set -euo pipefail

# Absolute path of a Bazel-relative input or output.
abspath() { case "$1" in /*) printf '%s' "$1" ;; *) printf '%s/%s' "$PWD" "$1" ;; esac; }

# Host Xcode macOS SDK and platform (phase 1: the host toolchain; see toolchains/README.md).
host_sdk()      { xcrun -sdk macosx --show-sdk-path; }
host_platform() { xcrun -sdk macosx --show-sdk-platform-path; }
host_sdk_ver()  { xcrun -sdk macosx --show-sdk-version; }

# Variables XNU's makefiles would otherwise ask xcrun for; xcrun cannot resolve
# an out-of-tree SDK, so NeoDarwin passes them explicitly.
xnu_sdk_vars() {  # $1 = overlay SDK path
	printf '%s\n' \
		"SDKROOT=$1" "SDKROOT_RESOLVED=$1" \
		"HOST_SDKROOT=$(host_sdk)" "HOST_SDKROOT_RESOLVED=$(host_sdk)" \
		"SDKVERSION=$(host_sdk_ver)" "PLATFORM=MacOSX" "PLATFORMPATH=$(host_platform)"
}

# Copy a read-only source tree into a writable work directory.
stage_tree() { mkdir -p "$2"; (cd "$1" && tar chf - .) | (cd "$2" && tar xf -); chmod -R u+w "$2"; }

# Assemble a usable build SDK in DIR: symlinks into the host macOS SDK, with the
# directory chains to every file in ADDITIONS made real and those files copied
# in. ADDITIONS is the output of mksdk.sh and contains only real files, so the
# Bazel artifact never holds symlinks into Xcode.
assemble_sdk() {  # $1 = ADDITIONS, $2 = DIR
	local add base out rel part e
	add="$(abspath "$1")"; out="$2"; base="$(host_sdk)"
	rm -rf "$out"; mkdir -p "$out"
	for e in "$base"/*; do ln -s "$e" "$out/$(basename "$e")"; done
	_materialise() {
		rel=""
		IFS=/ read -ra parts <<< "$1"
		for part in "${parts[@]}"; do
			rel="${rel:+$rel/}$part"
			if [ -L "$out/$rel" ]; then
				rm "$out/$rel"; mkdir "$out/$rel"
				shopt -s dotglob nullglob
				for e in "$base/$rel"/*; do ln -s "$e" "$out/$rel/$(basename "$e")"; done
				shopt -u dotglob nullglob
			fi
			[ -d "$out/$rel" ] || mkdir -p "$out/$rel"
		done
	}
	(cd "$add" && find -L . -type d | sed 's|^\./||' | grep -v '^\.$') | while read -r d; do _materialise "$d"; done
	(cd "$add" && find -L . -type f | sed 's|^\./||') | while read -r f; do rm -f "$out/$f"; cp "$add/$f" "$out/$f"; done
}
