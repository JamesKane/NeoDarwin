"""The userland base built from Apple source (docs/base/libsystem.md).

base_sysroot stages the header tree every base library builds against
(tools/base/stage_sysroot.sh). base_library runs one project's build script
(base/<project>/build.sh) against the sysroot and the install trees of the
libraries it links, and produces the project's own install tree
(usr/lib/system/..., usr/local/lib/dyld/...). Phase 1 uses the host Xcode
toolchain, like the kernel rules (rules/xnu.bzl).
"""

_XCODE_REQS = {"requires-darwin": "", "no-remote": ""}

def _root(target):
    """The directory an upstream archive's filegroup lives in."""
    return target.label.workspace_root or target.label.package

def _sysroot_impl(ctx):
    out = ctx.actions.declare_directory(ctx.label.name)
    repos = [ctx.attr.xnu, ctx.attr.libplatform, ctx.attr.libpthread, ctx.attr.libmalloc,
             ctx.attr.availability, ctx.attr.dyld, ctx.attr.libc, ctx.attr.libinfo, ctx.attr.libclosure,
             ctx.attr.libdispatch, ctx.attr.objc4, ctx.attr.llvm]
    inputs = ctx.files.xnu_headers + ctx.files.shims + ctx.files.mdns_patches + ctx.attr.mdnsresponder.files.to_list() + [
        ctx.file.dispatch_headers_script,
        ctx.file.llvm_headers_script,
        ctx.file.mdns_headers_script,
    ]
    for r in repos:
        inputs += r.files.to_list()
    ctx.actions.run(
        executable = ctx.file._script,
        arguments = [out.path, ctx.files.xnu_headers[0].path] + [_root(r) for r in repos] +
                    [ctx.file.dispatch_headers_script.path, ctx.file.llvm_headers_script.path, ctx.attr.shims_root,
                     _root(ctx.attr.mdnsresponder), ctx.file.mdns_headers_script.path],
        inputs = inputs,
        outputs = [out],
        tools = ctx.files._tools,
        mnemonic = "BaseSysroot",
        progress_message = "Staging the base header sysroot %{label}",
        execution_requirements = _XCODE_REQS,
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

base_sysroot = rule(
    implementation = _sysroot_impl,
    doc = "The header sysroot of the userland base: xnu's installed headers, each project's, and base/sdk's shims.",
    attrs = {
        "xnu_headers": attr.label(mandatory = True, doc = "//kernel:headers (make installhdrs)."),
        "xnu": attr.label(mandatory = True),
        "libplatform": attr.label(mandatory = True),
        "libpthread": attr.label(mandatory = True),
        "libmalloc": attr.label(mandatory = True),
        "availability": attr.label(mandatory = True),
        "dyld": attr.label(mandatory = True),
        "libc": attr.label(mandatory = True),
        "libinfo": attr.label(mandatory = True),
        "libclosure": attr.label(mandatory = True),
        "libdispatch": attr.label(mandatory = True),
        "objc4": attr.label(mandatory = True),
        "llvm": attr.label(mandatory = True, doc = "@llvm_project (libc++ and libunwind headers)."),
        "llvm_headers_script": attr.label(mandatory = True, allow_single_file = True,
                                          doc = "base/llvm/install_headers.sh."),
        "dispatch_headers_script": attr.label(mandatory = True, allow_single_file = True,
                                              doc = "base/libdispatch/install_headers.sh."),
        "mdnsresponder": attr.label(mandatory = True, doc = "@apple_mdnsresponder (libsystem_dnssd's headers)."),
        "mdns_headers_script": attr.label(mandatory = True, allow_single_file = True,
                                          doc = "base/mdnsresponder/install_headers.sh."),
        "mdns_patches": attr.label_list(allow_files = [".patch"], doc = "base/mdnsresponder/patches, which it applies."),
        "shims": attr.label(mandatory = True, doc = "base/sdk's headers."),
        "shims_root": attr.string(mandatory = True, doc = "Their directory, e.g. base/sdk."),
        "_script": attr.label(default = "//tools/base:stage_sysroot.sh", allow_single_file = True),
        "_tools": attr.label(default = "//tools/base:scripts"),
    },
)

def _library_impl(ctx):
    out = ctx.actions.declare_directory(ctx.label.name)
    sysroot = ctx.files.sysroot[0]
    deps = [d.files.to_list()[0] for d in ctx.attr.deps]
    ctx.actions.run(
        executable = ctx.file.script,
        arguments = [out.path, _root(ctx.attr.srcs), sysroot.path] + [d.path for d in deps],
        inputs = ctx.files.srcs + ctx.files.data + [sysroot] + deps,
        outputs = [out],
        tools = ctx.files._tools,
        mnemonic = "BaseLibrary",
        progress_message = "Building %{label}",
        execution_requirements = _XCODE_REQS,
        use_default_shell_env = True,
    )
    return [DefaultInfo(files = depset([out]))]

base_library = rule(
    implementation = _library_impl,
    doc = "One base project built from Apple source by its build script; an install tree.",
    attrs = {
        "script": attr.label(mandatory = True, allow_single_file = True, doc = "base/<project>/build.sh."),
        "srcs": attr.label(mandatory = True, doc = "The pinned upstream tree (@apple_<project>//:all)."),
        "sysroot": attr.label(mandatory = True, doc = "The base_sysroot."),
        "deps": attr.label_list(doc = "base_library targets whose install trees this one links."),
        "data": attr.label_list(allow_files = True, doc = "The script's own files: source lists, patches, NeoDarwin sources."),
        "_tools": attr.label(default = "//tools/base:scripts"),
    },
)
