"""HFS+ ramdisk images for neoboot (\\NeoDarwin\\ramdisk, rooted as md0).

hfs_ramdisk stages files at their paths in a directory tree and builds a
raw HFS+ volume from it with tools/ramdisk/mkhfs.sh (host hdiutil in
Phase 1). Used for the HFS+ root of P1-08a onward.

Each volume also gets its static trust cache (P1-15,
docs/kernel/amfi-provider.md): //tools/trustcache lists every signed arm64
Mach-O staged in it, except trust_cache_exclude, in NAME.trustcache (a
version 1 module, which neoboot loads from \\NeoDarwin\\trustcache), with
NAME.trustcache.txt naming each listed file. The target NAME_trustcache is
the module alone.

Every file and folder in the volume is root:wheel, except the paths in
owners, and modes keep their set-user-ID bits (//tools/hfsowners: hdiutil
records the build user as the owner and drops them).
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
    tc = ctx.actions.declare_file(ctx.label.name + ".trustcache")
    manifest = ctx.actions.declare_file(ctx.label.name + ".trustcache.txt")
    args += ["--trust-cache", ctx.executable._trustcache.path, tc.path, manifest.path]
    for path in ctx.attr.trust_cache_exclude:
        args += ["--trust-cache-exclude", path]
    args += ["--owners", ctx.executable._hfsowners.path]
    for path, owner in ctx.attr.owners.items():
        args += ["--owner", path, owner]
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = inputs,
        tools = [ctx.executable._trustcache, ctx.executable._hfsowners],
        outputs = [out, tc, manifest],
        mnemonic = "HfsRamdisk",
        progress_message = "Building HFS+ ramdisk %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": "", "no-sandbox": ""},
        use_default_shell_env = True,
    )
    return [
        DefaultInfo(files = depset([out])),
        OutputGroupInfo(trustcache = depset([tc]), trustcache_manifest = depset([manifest])),
    ]

_hfs_ramdisk = rule(
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
        "trust_cache_exclude": attr.string_list(doc = "Paths (files or directories) whose Mach-Os the trust cache leaves out, e.g. a test binary that must be refused."),
        "owners": attr.string_dict(doc = "Path -> \"UID:GID\" for a file or folder not owned by root:wheel, e.g. a home directory."),
        "_script": attr.label(default = "//tools/ramdisk:mkhfs.sh", allow_single_file = True),
        "_trustcache": attr.label(default = "//tools/trustcache", executable = True, cfg = "exec"),
        "_hfsowners": attr.label(default = "//tools/hfsowners", executable = True, cfg = "exec"),
    },
)

def _contents_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name + ".txt")
    args = ctx.actions.args()
    args.add(out)
    args.add_all(ctx.files.trees, expand_directories = False)
    listed = ctx.actions.declare_file(ctx.label.name + ".listed.txt")
    ctx.actions.write(listed, "".join([dest + "\n" for dest in ctx.attr.files.values() + ctx.attr.links.keys()]))
    ctx.actions.run_shell(
        inputs = ctx.files.trees + [listed],
        outputs = [out],
        arguments = [args],
        command = """out="$1"; shift
{ cat "%s"; for t in "$@"; do (cd "$t" && find . -mindepth 1 \\( -type f -o -type l \\) | sed 's|^\\./||'); done; } | LC_ALL=C sort -u > "$out"
""" % listed.path,
        mnemonic = "VolumeContents",
        progress_message = "Listing %{label}",
    )
    return [DefaultInfo(files = depset([out]))]

_volume_contents = rule(
    implementation = _contents_impl,
    doc = "The paths of an hfs_ramdisk's files and links, one per line, sorted (no image is built).",
    attrs = {
        "files": attr.label_keyed_string_dict(allow_files = True),
        "trees": attr.label_list(allow_files = True),
        "links": attr.string_dict(),
    },
)

def hfs_ramdisk(name, tags = [], **kwargs):
    """An HFS+ volume (NAME, NAME.hfs), its static trust cache (NAME_trustcache) and its file list (NAME_contents)."""
    _hfs_ramdisk(name = name, tags = tags, **kwargs)
    _volume_contents(
        name = name + "_contents",
        files = kwargs.get("files", {}),
        trees = kwargs.get("trees", []),
        links = kwargs.get("links", {}),
        tags = tags,
    )
    native.filegroup(
        name = name + "_trustcache",
        srcs = [":" + name],
        output_group = "trustcache",
        tags = tags,
    )
    native.filegroup(
        name = name + "_trustcache_manifest",
        srcs = [":" + name],
        output_group = "trustcache_manifest",
        tags = tags,
    )
