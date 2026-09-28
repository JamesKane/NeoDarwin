"""UEFI applications in Embedded Swift (language policy T3).

swift_embedded_uefi_binary builds a PE32+ EFI application for AArch64:
Embedded Swift -> LLVM bitcode -> aarch64 Windows COFF -> lld-link. The
toolchain is located on the host by //toolchains/embedded:repo.bzl until the
pinned NeoDarwin toolchain lands (P0-02).
"""

load("@nd_embedded_swift//:toolchain.bzl", "EMBEDDED_TOOLCHAIN")

def _dirs(files):
    seen = {}
    for f in files:
        seen[f.dirname] = True
    return seen.keys()

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out or (ctx.label.name + ".efi"))
    args = [EMBEDDED_TOOLCHAIN, out.path, ctx.attr.entry, ":".join(_dirs(ctx.files.hdrs)), "--"]
    args += [f.path for f in ctx.files.srcs] + ["--"] + [f.path for f in ctx.files.c_srcs]
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = ctx.files.srcs + ctx.files.c_srcs + ctx.files.hdrs,
        outputs = [out],
        mnemonic = "EmbeddedSwiftEfi",
        progress_message = "Building EFI application %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

swift_embedded_uefi_binary = rule(
    implementation = _impl,
    doc = "A PE32+ AArch64 EFI application written in Embedded Swift, built with -no-allocations.",
    attrs = {
        "srcs": attr.label_list(allow_files = [".swift"], mandatory = True),
        "c_srcs": attr.label_list(allow_files = [".c"], doc = "Justified C (language policy T4), e.g. compiler runtime routines."),
        "hdrs": attr.label_list(allow_files = True, doc = "C headers and module maps; their directories are on the include path."),
        "entry": attr.string(default = "efi_main"),
        "out": attr.string(doc = "Output file name; defaults to <name>.efi."),
        "_script": attr.label(default = "//tools/efi:build_efi.sh", allow_single_file = True),
    },
)
