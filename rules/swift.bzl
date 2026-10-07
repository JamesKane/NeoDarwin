"""NeoDarwin Swift macros enforcing the language policy (T1, T2).

Every first-party Swift target uses these instead of the rules_swift symbols
directly, so the language mode and warning policy cannot drift per target.
See docs/architecture/language-policy.md and toolchains/README.md for the
flag spellings.
"""

load("@nd_embedded_swift//:toolchain.bzl", "EMBEDDED_TOOLCHAIN")
load("@nd_macos_sdk//:sdk.bzl", "MACOS_SDK")
load("@rules_shell//shell:sh_test.bzl", "sh_test")
load("@rules_swift//swift:swift.bzl", "swift_binary", "swift_library")

# Swift 6 language mode implies complete strict concurrency checking.
ND_SWIFT_COPTS = [
    "-swift-version",
    "6",
    "-warnings-as-errors",
]

def nd_swift_library(name, copts = [], tier = "T1", **kwargs):
    """A first-party Swift library.

    tier = "T2" (allocation-free Swift) adds <name>_t2, a test that runs the
    T2 gate (//tools/t2check:t2check.sh) on srcs: every public entry point
    carries @_noLocks, the hosted compile's performance diagnostics
    pass, and an Embedded -no-allocations compile of the whole module
    passes. A T2 module is a leaf: it depends on the standard library only.
    """
    if tier not in ("T1", "T2"):
        fail("%s: tier must be T1 or T2 (T3 uses rules/efi.bzl or rules/static_macho.bzl), not %r" % (name, tier))
    if tier == "T2":
        if kwargs.get("deps"):
            fail("%s: a T2 module is a leaf (standard library only), so it has no deps" % name)
        sh_test(
            name = name + "_t2",
            size = "small",
            srcs = ["//tools/t2check:t2check.sh"],
            args = [EMBEDDED_TOOLCHAIN, kwargs.get("module_name", name)] +
                   ["$(rootpaths %s)" % s for s in kwargs["srcs"]],
            data = kwargs["srcs"],
            env = {"ND_MACOS_SDK": MACOS_SDK},
            tags = ["requires-darwin", "no-remote"] + kwargs.get("tags", []),
        )
    swift_library(name = name, copts = ND_SWIFT_COPTS + copts, **kwargs)

def nd_swift_binary(name, copts = [], **kwargs):
    swift_binary(name = name, copts = ND_SWIFT_COPTS + copts, **kwargs)
