"""Dynamically linked Darwin executables in Embedded Swift (language policy T3).

swift_embedded_executable builds an arm64 MH_EXECUTE that dyld starts and
that links against NeoDarwin's libSystem: Embedded Swift and justified C for
arm64-apple-macos, linked by Xcode's ld against a NeoDarwin root (//base:root)
with an ad hoc signature. The Embedded toolchain is located on the host by
//toolchains/embedded:repo.bzl until the pinned NeoDarwin toolchain lands
(P0-02).
"""

load("@nd_embedded_swift//:toolchain.bzl", "EMBEDDED_TOOLCHAIN")

def _dirs(files):
    seen = {}
    for f in files:
        seen[f.dirname] = True
    return seen.keys()

def _impl(ctx):
    out = ctx.actions.declare_file(ctx.attr.out or ctx.label.name)
    module = ctx.attr.module_name or ctx.label.name
    root = ctx.file.root
    args = [EMBEDDED_TOOLCHAIN, out.path, module, ":".join(_dirs(ctx.files.hdrs)), root.path, "--"]
    args += [f.path for f in ctx.files.srcs] + ["--"] + [f.path for f in ctx.files.c_srcs]
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = ctx.files.srcs + ctx.files.c_srcs + ctx.files.hdrs + [root],
        outputs = [out],
        mnemonic = "EmbeddedSwiftMachO",
        progress_message = "Building executable %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

swift_embedded_executable = rule(
    implementation = _impl,
    doc = "A dynamically linked arm64 Darwin executable written in Embedded Swift, linked against a NeoDarwin root.",
    attrs = {
        "srcs": attr.label_list(allow_files = [".swift"], mandatory = True),
        "c_srcs": attr.label_list(allow_files = [".c"], doc = "Justified C (language policy T4)."),
        "hdrs": attr.label_list(allow_files = True, doc = "C headers and module maps; their directories are on the include path."),
        "root": attr.label(mandatory = True, allow_single_file = True, doc = "The NeoDarwin root to link against (a directory), e.g. //base:root."),
        "module_name": attr.string(doc = "Swift module name; defaults to the target name."),
        "out": attr.string(doc = "Output file name; defaults to the target name."),
        "_script": attr.label(default = "//tools/darwin_executable:build.sh", allow_single_file = True),
    },
)
