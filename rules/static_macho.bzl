"""Static Darwin executables in Embedded Swift (language policy T3).

swift_embedded_static_executable builds an arm64 MH_EXECUTE with no dyld or
libSystem: Embedded Swift and justified C for arm64-apple-macos, linked
-static by the from-source ld64 (//toolchains/ld64) with an LC_UNIXTHREAD
entry and an ad hoc signature. For programs that run before NeoDarwin has a
userland, such as the first PID 1 (P1-07). Swift and C compile with the
pinned swift.org toolchain (@nd_embedded_swift, P0-02).
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
    args = [EMBEDDED_TOOLCHAIN, ctx.executable._ld64.path, out.path, ctx.attr.entry, module, ":".join(_dirs(ctx.files.hdrs)), "--"]
    args += [f.path for f in ctx.files.srcs] + ["--"] + [f.path for f in ctx.files.c_srcs]
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = args,
        inputs = ctx.files.srcs + ctx.files.c_srcs + ctx.files.hdrs,
        tools = [ctx.executable._ld64],
        outputs = [out],
        mnemonic = "EmbeddedSwiftStaticMachO",
        progress_message = "Building static executable %{label}",
        execution_requirements = {"requires-darwin": "", "no-remote": ""},
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

swift_embedded_static_executable = rule(
    implementation = _impl,
    doc = "A static arm64 Darwin executable written in Embedded Swift, built with -no-allocations.",
    attrs = {
        "srcs": attr.label_list(allow_files = [".swift"], mandatory = True),
        "c_srcs": attr.label_list(allow_files = [".c"], doc = "Justified C (language policy T4): the entry point, trap stubs, compiler runtime routines."),
        "hdrs": attr.label_list(allow_files = True, doc = "C headers and module maps; their directories are on the include path."),
        "entry": attr.string(default = "start", doc = "C name of the entry symbol."),
        "module_name": attr.string(doc = "Swift module name; defaults to the target name."),
        "out": attr.string(doc = "Output file name; defaults to the target name."),
        "_ld64": attr.label(default = "//toolchains/ld64", executable = True, cfg = "exec"),
        "_script": attr.label(default = "//tools/static_macho:build_static_macho.sh", allow_single_file = True),
    },
)
