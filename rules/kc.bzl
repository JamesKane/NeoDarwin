"""Kernel collections (docs/architecture/build-system.md §4).

kext_collection links a kernel and kexts into an MH_FILESET boot collection
with kcgen. With no kexts the collection holds only the kernel, which is all
milestones M0-M4 need (arm64-sbsa-bringup.md §2.1). Kexts (nd_kext bundles,
rules/kext.bzl) join it with the codeless kexts they depend on: xnu's
System.kext pseudo-kexts (com.apple.kpi.*, from kernel_components) and the
families NeoDarwin builds into the kernel (codeless).
"""

# The kernel ABI (packaging.md §6, filesystems.md §2 step 3): the version
# of the kernel's exported KPIs that kexts link against. Every kext with
# code declares the ABI it was built for (nd_kext's kernel_abi, NDKernelABI
# in its Info.plist) and kcgen refuses one that isn't the collection's.
# Bumped only when an export is removed or changed incompatibly; additions
# keep it.
KERNEL_ABI = "1"

def _impl(ctx):
    kernels = [f for f in ctx.files.kernel if f.basename.startswith("kernel.") and not f.basename.endswith(".unstripped")]
    if len(kernels) != 1:
        fail("kernel must provide exactly one kernel.* image (besides .unstripped), got %s" % [f.basename for f in ctx.files.kernel])
    out = ctx.actions.declare_file(ctx.attr.out or ctx.label.name)
    args = ["--kernel", kernels[0].path, "--output", out.path]
    inputs = list(kernels)
    if ctx.attr.kexts or ctx.attr.codeless or ctx.attr.kernel_components:
        if not ctx.attr.kernel_version:
            fail("kernel_version is required with kexts")
        args += ["--kernel-version", ctx.attr.kernel_version, "--kernel-abi", ctx.attr.kernel_abi]
    for k in ctx.files.kexts:
        args += ["--kext", k.path]
        inputs.append(k)
    marker = "config/System.kext/PlugIns/"
    components = sorted([f for f in ctx.files.kernel_components if marker in f.path and f.basename == "Info.plist"],
                        key = lambda f: f.path)
    for f in components:
        plugin = f.path[f.path.index(marker) + len(marker):].split("/")[0]
        args += ["--codeless", "%s@/System/Library/Extensions/System.kext/PlugIns/%s" % (f.path, plugin)]
        inputs.append(f)
    for target, bundle_path in ctx.attr.codeless.items():
        for f in target.files.to_list():
            args += ["--codeless", "%s@%s" % (f.path, bundle_path)]
            inputs.append(f)
    ctx.actions.run(
        executable = ctx.executable._kcgen,
        arguments = args,
        inputs = inputs,
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
        "kexts": attr.label_list(doc = "nd_kext bundles to link into the collection."),
        "kernel_components": attr.label(allow_files = True, doc = "xnu's source (@apple_xnu//:all): its System.kext pseudo-kexts become codeless kexts."),
        "codeless": attr.label_keyed_string_dict(allow_files = [".plist"], doc = "Info.plists of codeless kexts, each with its bundle path."),
        "kernel_version": attr.string(doc = "The kernel's osrelease (Darwin version), stamped on the pseudo-kexts."),
        "kernel_abi": attr.string(default = KERNEL_ABI, doc = "The collection's kernel ABI; every kext with code must declare it (NDKernelABI)."),
        "kind": attr.string(default = "boot", values = ["boot"], doc = "Collection kind; system and aux arrive later (P5)."),
        "out": attr.string(doc = "Output file name; defaults to the target name."),
        "_kcgen": attr.label(default = "//tools/kcgen", executable = True, cfg = "exec"),
    },
)
