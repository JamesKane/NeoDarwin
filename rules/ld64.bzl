"""Apple's ld64 built from source as a host tool (P0-06, toolchains/ld64).

ld64_binary runs toolchains/ld64/build.sh against the pinned @apple_ld64 tree
and produces an executable `ld`. The kernel and kext rules take it through
an exec-configuration attribute (rules/xnu.bzl, rules/kext.bzl). Phase 1
compiles with the host Xcode's clang, like the other Apple-project replays.
"""

_XCODE_REQS = {"requires-darwin": "", "no-remote": ""}

def _ld64_binary_impl(ctx):
    ld = ctx.actions.declare_file(ctx.label.name + "/bin/ld")
    version = ctx.actions.declare_file(ctx.label.name + "/VERSION.txt")
    root = ctx.attr.srcs.label.workspace_root
    ctx.actions.run(
        executable = ctx.file.script,
        arguments = [ld.dirname[:-len("/bin")], root, ctx.attr.version],
        inputs = ctx.files.srcs + ctx.files.data,
        outputs = [ld, version],
        mnemonic = "Ld64",
        progress_message = "Building ld64-%s %%{label}" % ctx.attr.version,
        execution_requirements = _XCODE_REQS,
        use_default_shell_env = True,
    )
    return [DefaultInfo(executable = ld, files = depset([ld])), OutputGroupInfo(version = depset([version]))]

ld64_binary = rule(
    implementation = _ld64_binary_impl,
    executable = True,
    doc = "ld64's `ld` (ld64.xcodeproj's ld target), built from a pinned apple-oss-distributions/ld64 tree.",
    attrs = {
        "srcs": attr.label(mandatory = True, doc = "The pinned ld64 tree (@apple_ld64//:all)."),
        "version": attr.string(mandatory = True, doc = "The tag's version, e.g. 957.1: LD_VERS and -v's PROJECT."),
        "script": attr.label(mandatory = True, allow_single_file = True),
        "data": attr.label_list(allow_files = True, doc = "The script's own files: compat headers and nd_stubs.cpp."),
    },
)
