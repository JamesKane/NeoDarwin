"""Individual upstream files pinned by commit and SHA-256.

For upstream trees too large to vendor whole when NeoDarwin needs a few dozen
files (FreeBSD's kernel crypto). The lock file, in `shasum -a 256` format
with a `# commit: <sha>` line, is the pin; tools/pinned/lock.sh refreshes it
from a local checkout. Each file is downloaded from
`url_template.format(commit = ..., path = ...)` and checked against its hash.
"""

_PARALLEL = 32

def _impl(ctx):
    lock = ctx.read(ctx.path(ctx.attr.lockfile))
    commit = None
    files = []
    for line in lock.splitlines():
        line = line.strip()
        if line.startswith("# commit:"):
            commit = line.split(":", 1)[1].strip()
        elif line and not line.startswith("#"):
            sha256, path = line.split("  ", 1)
            files.append((path, sha256))
    if not commit or not files:
        fail("%s: needs a '# commit:' line and at least one file" % ctx.attr.lockfile)
    # Up to _PARALLEL downloads at a time: a lock of a thousand files
    # (tools/parity/freebsd.lock) takes minutes one by one.
    for start in range(0, len(files), _PARALLEL):
        pending = [
            ctx.download(
                url = ctx.attr.url_template.format(commit = commit, path = path),
                output = path,
                sha256 = sha256,
                block = False,
            )
            for path, sha256 in files[start:start + _PARALLEL]
        ]
        for p in pending:
            p.wait()
    ctx.file("BUILD.bazel", ctx.read(ctx.path(ctx.attr.build_file)))

pinned_files = repository_rule(
    implementation = _impl,
    attrs = {
        "lockfile": attr.label(mandatory = True, allow_single_file = True),
        "url_template": attr.string(mandatory = True, doc = "Contains {commit} and {path}."),
        "build_file": attr.label(mandatory = True, allow_single_file = True),
    },
)
