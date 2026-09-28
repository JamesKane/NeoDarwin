"""Locate the Embedded Swift toolchain on the host (phase 0).

Xcode's toolchain ships no Embedded Swift standard library, so Embedded builds
(language policy T3) use a swift.org toolchain installed by swiftly. This
repository rule finds one and records its path; it is replaced by the pinned
NeoDarwin toolchain tarball in P0-02. Override with ND_EMBEDDED_TOOLCHAIN.
"""

_REQUIRED = "usr/lib/swift/embedded/aarch64-none-none-elf"
_VERSION_PREFIX = "swift-6.3"

def _impl(rctx):
    candidates = []
    override = rctx.os.environ.get("ND_EMBEDDED_TOOLCHAIN", "")
    if override:
        candidates.append(override)
    home = rctx.os.environ.get("HOME", "")
    tdir = rctx.path(home + "/Library/Developer/Toolchains")
    if tdir.exists:
        names = sorted([p.basename for p in tdir.readdir() if p.basename.startswith(_VERSION_PREFIX) and p.basename.endswith(".xctoolchain")], reverse = True)
        candidates += [str(tdir) + "/" + n for n in names]
    found = ""
    for c in candidates:
        if rctx.path(c + "/" + _REQUIRED).exists and rctx.path(c + "/usr/bin/lld-link").exists:
            found = c
            break
    if not found:
        fail("No Embedded Swift toolchain found. Install one with `swiftly install 6.3.2` or set ND_EMBEDDED_TOOLCHAIN. Searched: " + ", ".join(candidates))
    rctx.file("BUILD.bazel", 'exports_files(["toolchain.bzl"])\n')
    rctx.file("toolchain.bzl", 'EMBEDDED_TOOLCHAIN = "%s"\n' % found)

nd_embedded_swift = repository_rule(
    implementation = _impl,
    environ = ["ND_EMBEDDED_TOOLCHAIN", "HOME"],
    local = True,
)
