"""Kernel collections (docs/architecture/build-system.md §4).

kext_collection links a kernel into an MH_FILESET boot collection with kcgen.
Kexts join the collection in Phase 5; until then the collection holds only
the kernel, which is all milestones M0-M4 need (arm64-sbsa-bringup.md §2.1).
"""

def _impl(ctx):
    kernels = [f for f in ctx.files.kernel if f.basename.startswith("kernel.") and not f.basename.endswith(".unstripped")]
    if len(kernels) != 1:
        fail("kernel must provide exactly one kernel.* image (besides .unstripped), got %s" % [f.basename for f in ctx.files.kernel])
    out = ctx.actions.declare_file(ctx.attr.out or ctx.label.name)
    ctx.actions.run(
        executable = ctx.executable._kcgen,
        arguments = ["--kernel", kernels[0].path, "--output", out.path],
        inputs = kernels,
        outputs = [out],
        mnemonic = "KCGen",
        progress_message = "Linking kernel collection %{label}",
    )
    return [DefaultInfo(files = depset([out]))]

kext_collection = rule(
    implementation = _impl,
    doc = "An MH_FILESET boot kernel collection built by //tools/kcgen.",
    attrs = {
        "kernel": attr.label(mandatory = True, allow_files = True, doc = "An xnu_kernel target or a kernel file."),
        "kind": attr.string(default = "boot", values = ["boot"], doc = "Collection kind; system and aux arrive with kexts (P5)."),
        "out": attr.string(doc = "Output file name; defaults to the target name."),
        "_kcgen": attr.label(default = "//tools/kcgen", executable = True, cfg = "exec"),
    },
)
