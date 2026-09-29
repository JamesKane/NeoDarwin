<!-- SPDX-License-Identifier: BSD-2-Clause -->
# images

`system_image`, `esp_image` and ramdisk targets that assemble bootable images from packages.

`//images:pid1_root` is the first: an HFS+ ramdisk (`rules/ramdisk.bzl`) holding the PID 1 test program as `/sbin/launchd`, booted by `//kernel:sbsa_boot_test`.

`//images:session_disk` (P1-10, `rules/disk.bzl`) is the first disk image: a raw GPT disk with an EFI System Partition (neoboot, the kernel collection, no ramdisk) and a journaled HFS+ root partition holding the session system (`session_root_volume`). The partitions' unique GUIDs are name-based UUIDs of the target's label, listed in the `uuids` output group; neoboot passes the root partition's as `/chosen boot-uuid` (`docs/kernel/storage.md`). `//kernel:sbsa_disk_boot_test` boots it from virtio-blk. `//kernel:sbsa_nvme_boot_test` boots the same image from NVMe; `//images:data_disk` is the same root volume on a GPT disk whose ESP has no loader, a second disk that firmware never boots (`//kernel:sbsa_nvme_readonly_test`).
