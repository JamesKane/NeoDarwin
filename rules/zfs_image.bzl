"""Root-on-ZFS disk images (docs/architecture/filesystems.md §8).

zfs_root_disk turns a blank disk (gpt_disk_image with zfs_partition: an ESP
and an empty FreeBSD-ZFS partition) into a bootable root-on-ZFS disk: the
host has no ZFS, so tools/zfsimage/mkpool.sh boots a NeoDarwin build guest
under QEMU (the builder disk: zfs.kext and the zpool and zfs commands),
which creates the pool on the blank disk's partition 2, a boot environment
POOL/ROOT/BE as its bootfs with the root volume's files, a snapshot
BE@install, and exports it. The output group "log" has the guest's serial
log. Not bit-reproducible (ZFS GUIDs, txgs and times); same files and
layout on every build.
"""

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".img")
    log = ctx.actions.declare_file(ctx.label.name + ".serial.log")
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = [out.path, log.path, ctx.file._qemu_test.path, ctx.file.blank.path,
                     ctx.file.builder.path, ctx.file.root.path, ctx.attr.pool, ctx.attr.be] +
                    ["%s=%s" % (k, v) for k, v in sorted(ctx.attr.datasets.items())],
        inputs = [ctx.file.blank, ctx.file.builder, ctx.file.root, ctx.file._qemu_test],
        outputs = [out, log],
        mnemonic = "ZfsRootDisk",
        progress_message = "Building root-on-ZFS disk %{label} in a QEMU guest",
        execution_requirements = {"requires-darwin": "", "no-remote": "", "no-sandbox": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out])), OutputGroupInfo(log = depset([log]))]

zfs_root_disk = rule(
    implementation = _impl,
    doc = "A root-on-ZFS disk image made by a NeoDarwin guest from a blank disk and a root volume.",
    attrs = {
        "blank": attr.label(allow_single_file = [".img"], mandatory = True, doc = "A gpt_disk_image with zfs_partition: the ESP and a blank pool partition."),
        "builder": attr.label(allow_single_file = [".img"], mandatory = True, doc = "The build guest's boot disk: zfs.kext, zpool, zfs, tar and mount_hfs."),
        "root": attr.label(allow_single_file = [".hfs"], mandatory = True, doc = "The root volume whose files the boot environment gets (an hfs_ramdisk)."),
        "pool": attr.string(default = "ndpool"),
        "datasets": attr.string_dict(doc = "Dataset -> mountpoint: datasets POOL/NAME shared by every BE, e.g. {\"pkg\": \"/private/var/db/ndpkg\"} (packaging.md §5.2)."),
        "be": attr.string(default = "default", doc = "The first boot environment, POOL/ROOT/BE."),
        "_script": attr.label(default = "//tools/zfsimage:mkpool.sh", allow_single_file = True),
        "_qemu_test": attr.label(default = "//tools/efi:qemu_efi_test.sh", allow_single_file = True),
    },
)
