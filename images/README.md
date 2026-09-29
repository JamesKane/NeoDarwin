<!-- SPDX-License-Identifier: BSD-2-Clause -->
# images

`system_image`, `esp_image` and ramdisk targets that assemble bootable images from packages.

`//images:pid1_root` is the first: an HFS+ ramdisk (`rules/ramdisk.bzl`) holding the PID 1 test program as `/sbin/launchd`, booted by `//kernel:sbsa_boot_test`.
