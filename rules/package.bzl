"""nd_package: a signed .ndpkg from install trees and files (P2-01).

docs/architecture/packaging.md §3. The rule stages its trees and files, lists
their signed arm64 Mach-O files in a trust cache (//tools/trustcache, as the
images do), and packs and signs everything with //tools/ndsign: a zstd
ustar archive of manifest.toml, manifest.sig, trustcache, trustcache.grant
and files/. Every input is content (no timestamps, owners or build paths
reach the archive; `issued` is an attribute), and Ed25519 signatures are
deterministic, so a package is byte-identical across builds.

Outputs: TARGET/NAME-VERSION-ARCH.ndpkg (the default), and in output groups the
trust cache module (`trustcache`) and its grant (`grant`), which an image
can carry to load at run time.
"""

def _impl(ctx):
    out = ctx.actions.declare_file("%s/%s-%s-%s.ndpkg" % (ctx.label.name, ctx.attr.pkg_name, ctx.attr.version, ctx.attr.arch))
    tc = ctx.actions.declare_file(ctx.label.name + ".trustcache")
    grant = ctx.actions.declare_file(ctx.label.name + ".trustcache.grant")
    stage = out.path + ".stage"

    inputs = list(ctx.files.trees) + ctx.files.chain + [ctx.file.key]
    copies = []
    for t in ctx.files.trees:
        copies.append('cp -R "%s/." "$stage/"' % t.path)
    for target, dest in ctx.attr.files.items():
        f = target.files.to_list()
        if len(f) != 1:
            fail("%s must provide exactly one file for %s" % (target.label, dest))
        inputs.append(f[0])
        copies.append('mkdir -p "$stage/$(dirname "%s")" && cp "%s" "$stage/%s"' % (dest, f[0].path, dest))

    pack = [ctx.executable._ndsign.path, "pack", "--name", ctx.attr.pkg_name, "--version", ctx.attr.version,
            "--arch", ctx.attr.arch, "--license", ctx.attr.license, "--kind", ctx.attr.kind,
            "--key", ctx.file.key.path, "--issued", str(ctx.attr.issued),
            "--trust-cache", tc.path, "--grant-out", grant.path, "-o", out.path]
    for c in ctx.files.chain:
        pack += ["--chain", c.path]
    for flag, values in [("--provides", ctx.attr.provides), ("--requires", ctx.attr.requires), ("--conflicts", ctx.attr.conflicts)]:
        for v in values:
            pack += [flag, v]
    exclude = " ".join(['--exclude "%s"' % e for e in ctx.attr.trust_cache_exclude])

    ctx.actions.run_shell(
        inputs = inputs,
        tools = [ctx.executable._ndsign, ctx.executable._trustcache],
        outputs = [out, tc, grant],
        command = "\n".join([
            "set -euo pipefail",
            'stage="%s"; rm -rf "$stage"; mkdir -p "$stage"' % stage,
        ] + copies + [
            'chmod -R u+w "$stage"',
            '"%s" create "%s" %s "$stage" > /dev/null' % (ctx.executable._trustcache.path, tc.path, exclude),
            " ".join(['"%s"' % a for a in pack]) + ' "$stage" > /dev/null',
            'rm -rf "$stage"',
        ]),
        mnemonic = "NdPackage",
        progress_message = "Packaging %{label}",
        execution_requirements = {"requires-darwin": ""},
    )
    return [
        DefaultInfo(files = depset([out])),
        OutputGroupInfo(trustcache = depset([tc]), grant = depset([grant])),
    ]

nd_package = rule(
    implementation = _impl,
    doc = "A signed NeoDarwin package (.ndpkg) of install trees and files.",
    attrs = {
        "pkg_name": attr.string(mandatory = True, doc = "The package's name ([a-z0-9.+_-]+)."),
        "version": attr.string(mandatory = True),
        "arch": attr.string(default = "aarch64"),
        "license": attr.string(mandatory = True, doc = "An SPDX expression."),
        "kind": attr.string(default = "app", values = ["app", "lib", "service", "kext", "kernel-collection", "system", "system-set", "port"]),
        "provides": attr.string_list(),
        "requires": attr.string_list(),
        "conflicts": attr.string_list(),
        "trees": attr.label_list(allow_files = True, doc = "Install trees (directories) whose contents go at the package root, e.g. a base library."),
        "files": attr.label_keyed_string_dict(allow_files = True, doc = "File -> path in the package, without a leading slash."),
        "trust_cache_exclude": attr.string_list(doc = "Paths whose Mach-O files the package's trust cache leaves out."),
        "chain": attr.label_list(allow_files = True, mandatory = True, doc = "The channel and release certificates, in that order."),
        "key": attr.label(allow_single_file = True, mandatory = True, doc = "The release key (its seed file)."),
        "issued": attr.int(default = 0, doc = "The statements' `issued` time (Unix seconds); fixed, so builds reproduce."),
        "_ndsign": attr.label(default = "//tools/ndsign", executable = True, cfg = "exec"),
        "_trustcache": attr.label(default = "//tools/trustcache", executable = True, cfg = "exec"),
    },
)

def _system_tree_impl(ctx):
    out = ctx.actions.declare_directory(ctx.label.name)
    cmds = ["set -euo pipefail", 'out="%s"' % out.path, 'work="$(mktemp -d)"; trap \'rm -rf "$work"\' EXIT']
    for pkg in ctx.files.packages:
        cmds += [
            'rm -rf "$work/p"; mkdir -p "$work/p"; "%s" unpack --root "%s" "%s" "$work/p" > /dev/null' % (ctx.executable._ndsign.path, ctx.file.root.path, pkg.path),
            'name="$(sed -n \'s/^name = "\\(.*\\)"$/\\1/p\' "$work/p/manifest.toml")"',
            'r="$out/System/Library/Receipts/ndpkg/$name"; mkdir -p "$r"',
            'if [ -d "$work/p/files" ]; then (cd "$work/p/files" && tar -cf - .) | (cd "$out" && tar -xpf -); fi',
            'for f in manifest.toml manifest.sig trustcache trustcache.grant; do [ -e "$work/p/$f" ] && cp "$work/p/$f" "$r/$f"; done; chmod 0444 "$r"/*',
        ]
    ctx.actions.run_shell(
        inputs = ctx.files.packages + [ctx.file.root],
        tools = [ctx.executable._ndsign],
        outputs = [out],
        command = "\n".join(cmds),
        mnemonic = "NdSystemTree",
        progress_message = "Installing system packages into %{label}",
        execution_requirements = {"requires-darwin": ""},
    )
    return [DefaultInfo(files = depset([out]))]

nd_system_tree = rule(
    implementation = _system_tree_impl,
    doc = """A boot environment's system packages installed as an image tree (P2-03,
packaging.md §6.1): each package verified (ndsign unpack, against `root`),
its files at their paths and its receipt in
System/Library/Receipts/ndpkg/<name>/ (manifest.toml, manifest.sig and the
trust cache and grant), as `ndpkg system upgrade` installs into a BE.""",
    attrs = {
        "packages": attr.label_list(allow_files = [".ndpkg"], mandatory = True),
        "root": attr.label(allow_single_file = True, mandatory = True, doc = "The trusted root's public key (hex)."),
        "_ndsign": attr.label(default = "//tools/ndsign", executable = True, cfg = "exec"),
    },
)
