# SPDX-License-Identifier: BSD-2-Clause
# Shared by kexts/zfs/kext.sh and kexts/zfs/userland.sh. Sourced.
source "$(dirname "${BASH_SOURCE[0]}")/../../tools/base/common.sh"
PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The OS-layer directories the macOS port adds; NeoDarwin's layer is a copy
# of each at .../os/neodarwin, with the patches applied on top.
ND_OS_DIRS=(
	include/os/macos
	lib/libspl/include/os/macos
	lib/libspl/os/macos
	lib/libzfs/os/macos
	lib/libzfs_core/os/macos
	lib/libzutil/os/macos
	module/os/macos
	cmd/zpool/os/macos
)

# prepare_tree ZFS_SRC DEST: stage the pinned tree, copy the macOS layer to
# os/neodarwin, apply kexts/zfs/patches; prints DEST.
prepare_tree() {
	mkdir -p "$2"; (cd "$1" && tar chf - .) | (cd "$2" && tar xf -); chmod -R u+w "$2"
	local d p
	for d in "${ND_OS_DIRS[@]}"; do cp -R "$2/$d" "$2/${d%/macos}/neodarwin"; done
	for p in "$PROJ"/patches/*.patch; do patch -d "$2" -p1 --quiet < "$p"; done
	printf '%s' "$2"
}
