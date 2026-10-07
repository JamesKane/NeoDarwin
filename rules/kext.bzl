"""Kexts built from upstream source (docs/architecture/build-system.md §4).

nd_kext runs a kext's build script (kexts/<name>/<script>.sh) against the
pinned upstream tree, NeoDarwin's Kernel.framework headers (//kernel:headers)
and xnu's source (libkmod), and produces the kext bundle: a directory
<bundle>.kext with Contents/Info.plist and Contents/MacOS/<executable>.
kext_collection (rules/kc.bzl) links bundles into a boot kernel collection.
Phase 1 uses the host Xcode toolchain, like the kernel and base rules;
embedded_swift kexts also get the Embedded Swift toolchain's path (the
kext_swift trial, //kexts/swift_trial).

--//rules:kernel_linker selects the kexts' linker as it does the kernel's:
with ld64, the script gets the from-source ld64 (//toolchains/ld64) in
ND_KEXT_LD and links with it; with xcode it runs `xcrun ld`.
"""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@nd_embedded_swift//:toolchain.bzl", "EMBEDDED_TOOLCHAIN")

_XCODE_REQS = {"requires-darwin": "", "no-remote": ""}

def _root(target):
    """The directory an upstream archive's filegroup lives in."""
    return target.label.workspace_root or target.label.package

def _kext_impl(ctx):
    out = ctx.actions.declare_directory(ctx.attr.bundle)
    headers = ctx.files.kernel_headers
    tools = list(ctx.files._tools)
    env = {}
    if ctx.attr._kernel_linker[BuildSettingInfo].value == "ld64":
        tools.append(ctx.executable._ld64)
        env["ND_KEXT_LD"] = ctx.executable._ld64.path
    # The script builds the bundle; its Info.plist then declares the kernel
    # ABI the kext was built for (NDKernelABI), which kcgen checks against
    # the collection's (packaging.md §6, rules/kc.bzl KERNEL_ABI).
    ctx.actions.run_shell(
        command = '"$1" "${@:3}" && /usr/bin/plutil -replace NDKernelABI -string "$2" "$3/Contents/Info.plist"',
        arguments = [ctx.file.script.path, ctx.attr.kernel_abi] +
                    [out.path, _root(ctx.attr.srcs), headers[0].path, _root(ctx.attr.xnu)] +
                    [_root(t) for t in ctx.attr.upstream_headers] +
                    ([EMBEDDED_TOOLCHAIN] if ctx.attr.embedded_swift else []),
        inputs = ctx.files.srcs + headers + ctx.files.xnu + ctx.files.upstream_headers + ctx.files.data + [ctx.file.script],
        outputs = [out],
        tools = tools,
        env = env,
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
        "kernel_abi": attr.string(mandatory = True, doc = "The kernel ABI the kext is built for (rules/kc.bzl KERNEL_ABI), stamped as NDKernelABI in its Info.plist; kcgen refuses a kext whose ABI isn't the collection's."),
        "embedded_swift": attr.bool(doc = "Pass the Embedded Swift toolchain's path as the script's last argument."),
        "_tools": attr.label(default = "//tools/base:scripts"),
        "_kernel_linker": attr.label(default = "//rules:kernel_linker"),
        "_ld64": attr.label(default = "//toolchains/ld64", executable = True, cfg = "exec"),
    },
)
