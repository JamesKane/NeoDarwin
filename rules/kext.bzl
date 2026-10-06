"""Kexts built from upstream source (docs/architecture/build-system.md §4).

nd_kext runs a kext's build script (kexts/<name>/<script>.sh) against the
pinned upstream tree, NeoDarwin's Kernel.framework headers (//kernel:headers)
and xnu's source (libkmod), and produces the kext bundle: a directory
<bundle>.kext with Contents/Info.plist and Contents/MacOS/<executable>.
kext_collection (rules/kc.bzl) links bundles into a boot kernel collection.
Phase 1 uses the host Xcode toolchain, like the kernel and base rules;
embedded_swift kexts also get the Embedded Swift toolchain's path (the
kext_swift trial, //kexts/swift_trial).
"""

load("@nd_embedded_swift//:toolchain.bzl", "EMBEDDED_TOOLCHAIN")

_XCODE_REQS = {"requires-darwin": "", "no-remote": ""}

def _root(target):
    """The directory an upstream archive's filegroup lives in."""
    return target.label.workspace_root or target.label.package

def _kext_impl(ctx):
    out = ctx.actions.declare_directory(ctx.attr.bundle)
    headers = ctx.files.kernel_headers
    ctx.actions.run(
        executable = ctx.file.script,
        arguments = [out.path, _root(ctx.attr.srcs), headers[0].path, _root(ctx.attr.xnu)] +
                    [_root(t) for t in ctx.attr.upstream_headers] +
                    ([EMBEDDED_TOOLCHAIN] if ctx.attr.embedded_swift else []),
        inputs = ctx.files.srcs + headers + ctx.files.xnu + ctx.files.upstream_headers + ctx.files.data,
        outputs = [out],
        tools = ctx.files._tools,
        mnemonic = "Kext",
        progress_message = "Building %{label}",
        execution_requirements = _XCODE_REQS,
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

nd_kext = rule(
    implementation = _kext_impl,
    doc = "A kext bundle built from a pinned upstream tree by its build script.",
    attrs = {
        "bundle": attr.string(mandatory = True, doc = "The bundle's directory name, e.g. zfs.kext."),
        "script": attr.label(mandatory = True, allow_single_file = True, doc = "kexts/<name>/<script>.sh."),
        "srcs": attr.label(mandatory = True, doc = "The pinned upstream tree (@<repo>//:all)."),
        "kernel_headers": attr.label(default = "//kernel:headers", doc = "Kernel.framework (make installhdrs)."),
        "xnu": attr.label(default = "@apple_xnu//:all", doc = "xnu's source, for libkmod (libkern/kmod)."),
        "upstream_headers": attr.label_list(doc = "Further pinned trees whose roots the script takes, in order."),
        "data": attr.label_list(allow_files = True, doc = "The script's own files: patches, source lists, headers."),
        "embedded_swift": attr.bool(doc = "Pass the Embedded Swift toolchain's path as the script's last argument."),
        "_tools": attr.label(default = "//tools/base:scripts"),
    },
)
