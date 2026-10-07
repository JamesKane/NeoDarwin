#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-2-Clause
# A root-on-ZFS disk (rules/zfs_image.bzl, docs/architecture/filesystems.md
# §8): the host has no ZFS, so a NeoDarwin guest makes the pool. The blank
# disk (an ESP and an empty FreeBSD-ZFS partition, rules/disk.bzl
# zfs_partition) is copied to OUT; QEMU boots BUILDER (a disk with zfs.kext
# and the zpool and zfs commands) with OUT as a second disk, written in
# place, and the root volume (an HFS+ image) as a third, read-only. In the
# guest: `zpool create POOL` on OUT's partition 2, POOL/ROOT (mountpoint
# none) and POOL/ROOT/BE (mountpoint /, canmount noauto, as bectl makes a
# boot environment) as the pool's bootfs, the root volume's files copied in
# with tar, a snapshot BE@install, and `zpool export`.
#   mkpool.sh OUT LOG QEMU_EFI_TEST BLANK BUILDER ROOT_VOLUME POOL BE
# Not bit-reproducible: ZFS stamps GUIDs, transaction groups and times; the
# files and the layout are the same on every build.
set -euo pipefail
out="$1"; log="$2"; qemu_test="$3"; blank="$4"; builder="$5"; volume="$6"; pool="$7"; be="$8"
cat "$blank" > "$out"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
# Exclusions: HFS+'s journal and metadata files at the volume's root.
ex="--exclude ./.journal --exclude ./.journal_info_block --exclude './.HFS+ Private Directory Data*' --exclude ./.Trashes --exclude ./.fseventsd --exclude ./.Spotlight-V100"
p='root@localhost ~ # '
status=0
ND_QEMU_LOG_DIR="$work" "$qemu_test" --mem 2G --smp 2 --disk "$builder" \
	--drive-in-place "tgt=$out" --device virtio-blk-pci,drive=tgt,disable-legacy=on \
	--drive "src=$volume" --device virtio-blk-pci,drive=src,disable-legacy=on \
	--until-lines --absent 'panic(' \
	--send-after 'login: ' 'root\n' \
	--send-after "$p" 'r=$(df / | awk '"'"'NR==2{print $1}'"'"'); r=${r%s2}; s=; t=; for x in /dev/disk[0-9]; do [ $x = $r ] && continue; if [ -e ${x}s2 ]; then t=${x}s2; else s=$x; fi; done; echo found-$((6*7)) src=$s tgt=$t\n' \
	--send-after "$p" 'mkdir -p /tmp/src && mount -r -t hfs $s /tmp/src && echo mounted-$((6*7))\n' \
	--send-after "$p" "zpool create -f -o ashift=12 -O mountpoint=none -O canmount=off -O atime=off -R /tmp/be $pool \$t && zfs create $pool/ROOT && zfs create -o canmount=noauto -o mountpoint=/ $pool/ROOT/$be && zfs mount $pool/ROOT/$be && zpool set bootfs=$pool/ROOT/$be $pool && echo created-\$((6*7))\n" \
	--send-after "$p" "(cd /tmp/src && tar $ex -cf - .) | (cd /tmp/be && tar -xpf -) && echo copied-\$((6*7)); du -sh /tmp/be\n" \
	--send-after "$p" "umount /tmp/src; zfs snapshot $pool/ROOT/$be@install && zfs list -t all -o name,used,refer,mountpoint,canmount && echo pool-bootfs-is-\$(zpool get -H -o value bootfs $pool) && zpool export $pool && echo exported-\$((6*7))\n" \
	- 1500 \
	'found-42 src=/dev/disk' 'mounted-42' 'created-42' 'copied-42' "pool-bootfs-is-$pool/ROOT/$be" 'exported-42' \
	|| status=$?
cp "$work/serial.log" "$log" 2> /dev/null || true
if [ "$status" -ne 0 ]; then tail -60 "$log" 2> /dev/null; echo "mkpool.sh: the build guest failed (status $status)"; exit "$status"; fi
