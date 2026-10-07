"""The Embedded Swift toolchain (language policy T3).

Embedded builds (neoboot, static_macho, T2/T3, the kext trial) use the pinned
swift.org toolchain, @nd_swift (toolchains/repos.bzl, P0-02): it ships the
Embedded stdlib for aarch64-none-none-elf and arm64-apple-macos, and lld-link.
ND_EMBEDDED_TOOLCHAIN overrides it with another toolchain root (an
.xctoolchain directory), for trying a newer or locally built Swift.
"""

_REQUIRED = [
    "usr/lib/swift/embedded/aarch64-none-none-elf",
    "usr/lib/swift/embedded/arm64-apple-macos",
    "usr/bin/lld-link",
]

def _impl(rctx):
    override = rctx.getenv("ND_EMBEDDED_TOOLCHAIN", "")
    if override:
        found = override
        source = "ND_EMBEDDED_TOOLCHAIN"
    else:
        text = rctx.read(rctx.path(Label("@nd_swift//:toolchain.bzl")))
        found = [l.split('"')[1] for l in text.splitlines() if l.startswith("SWIFT_TOOLCHAIN = ")][0]
        source = "@nd_swift"
    missing = [r for r in _REQUIRED if not rctx.path(found + "/" + r).exists]
    if missing:
        fail("Embedded Swift toolchain %s (from %s) lacks %s" % (found, source, ", ".join(missing)))
    rctx.file("BUILD.bazel", 'exports_files(["toolchain.bzl"])\n')
    rctx.file("toolchain.bzl", 'EMBEDDED_TOOLCHAIN = "%s"\n' % found)

nd_embedded_swift = repository_rule(
    implementation = _impl,
    environ = ["ND_EMBEDDED_TOOLCHAIN"],
)
