"""HFS+ ramdisk images for neoboot (\\NeoDarwin\\ramdisk, rooted as md0).

hfs_ramdisk stages files at their paths in a directory tree and builds a
raw HFS+ volume from it with tools/ramdisk/mkhfs.sh (host hdiutil in
Phase 1). Used for the HFS+ root of P1-08a onward.
"""

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".hfs")
    args = [out.path, ctx.attr.volume_name]
    if ctx.attr.volume_size:
        args += ["--size", ctx.attr.volume_size]
    if ctx.attr.journaled:
        args.append("--journaled")
    for d in ctx.attr.dirs:
        args += ["--dir", d]
    inputs = []
    for t in ctx.files.trees:
        inputs.append(t)
        args += ["--tree", t.path]
    for target, dest in ctx.attr.files.items():
        f = target.files.to_list()
        if len(f) != 1:
            fail("%s must provide exactly one file for %s" % (target.label, dest))
        inputs.append(f[0])
        args += ["--file", f[0].path, dest, ctx.attr.modes.get(dest, "0755")]
    for link, target in ctx.attr.links.items():
        args += ["--link", target, link]
    for path, mode in ctx.attr.tree_modes.items():
        args += ["--mode", path, mode]
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = inputs,
        outputs = [out],
        mnemonic = "HfsRamdisk",
        progress_message = "Building HFS+ ramdisk %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": "", "no-sandbox": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

hfs_ramdisk = rule(
    implementation = _impl,
    doc = "A raw HFS+ volume image holding the given files and directories.",
    attrs = {
        "files": attr.label_keyed_string_dict(allow_files = True, doc = "File -> path in the volume, without a leading slash."),
        "trees": attr.label_list(allow_files = True, doc = "Directories whose contents go at the volume's root, e.g. //base:root."),
        "links": attr.string_dict(doc = "Path -> symbolic link target, e.g. {\"etc\": \"private/etc\"}."),
        "tree_modes": attr.string_dict(doc = "Path -> octal mode, applied last, for files that come from trees."),
        "dirs": attr.string_list(doc = "Empty directories to create, e.g. mount points such as dev."),
        "modes": attr.string_dict(doc = "Path -> octal mode; files default to 0755."),
        "volume_name": attr.string(default = "NeoDarwin"),
        "volume_size": attr.string(doc = "The volume's size (hdiutil's syntax, e.g. 256m); default: just big enough for its files."),
        "journaled": attr.bool(doc = "Journaled HFS+, for a writable root on a disk (rules/disk.bzl)."),
        "_script": attr.label(default = "//tools/ramdisk:mkhfs.sh", allow_single_file = True),
    },
)
