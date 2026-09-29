"""Bootable raw GPT disk images (docs/kernel/storage.md, "The disk image").

gpt_disk_image makes a disk with an EFI System Partition holding the given
files (neoboot as \\EFI\\BOOT\\BOOTAA64.EFI and the kernel collection as
\\NeoDarwin\\kernelcache, no ramdisk) and an HFS+ partition holding a root
volume from hfs_ramdisk. The partitions' unique GUIDs are name-based UUIDs of
the target's label, so the root's boot-uuid is stable across builds; the
output group "uuids" (NAME.uuids) lists them. tools/gptimage/mkdisk.sh builds it with the host's
hdiutil (FAT32) and gptimage (the GPT).
"""

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".img")
    uuids = ctx.actions.declare_file(ctx.label.name + ".uuids")
    root = ctx.file.root
    args = [out.path, uuids.path, ctx.executable._gptimage.path, str(ctx.label), root.path]
    inputs = [root]
    for target, dest in ctx.attr.esp.items():
        f = target.files.to_list()
        if len(f) != 1:
            fail("%s must provide exactly one file for %s" % (target.label, dest))
        inputs.append(f[0])
        args.append("%s=%s" % (dest, f[0].path))
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = inputs,
        tools = [ctx.executable._gptimage],
        outputs = [out, uuids],
        mnemonic = "GptDiskImage",
        progress_message = "Building GPT disk image %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": "", "no-sandbox": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out])), OutputGroupInfo(uuids = depset([uuids]))]

gpt_disk_image = rule(
    implementation = _impl,
    doc = "A raw disk image: GPT, an EFI System Partition with the given files, and an HFS+ root partition.",
    attrs = {
        "esp": attr.label_keyed_string_dict(allow_files = True, doc = "File -> path on the ESP, e.g. EFI/BOOT/BOOTAA64.EFI."),
        "root": attr.label(allow_single_file = [".hfs"], mandatory = True, doc = "The root volume (partition 2), an hfs_ramdisk."),
        "_gptimage": attr.label(default = "//tools/gptimage", executable = True, cfg = "exec"),
        "_script": attr.label(default = "//tools/gptimage:mkdisk.sh", allow_single_file = True),
    },
)
