"""The pinned host toolchain (P0-02; toolchains/README.md).

Three repository rules, all pinned in MODULE.bazel and recorded in
toolchains/upstream.lock:

  nd_macos_sdk         the host-installed macOS SDK, pinned in place: an exact
                       version (SDKSettings.json) and a content hash
                       (toolchains/sdk/sdk_hash.sh), checked at fetch. The SDK
                       can't be redistributed, so it isn't downloaded.
  nd_llvm_toolchain    llvm.org's macOS arm64 release by URL and sha256, and a
                       cc_toolchain for arm64-apple-macos on it.
  nd_swift_toolchain   swift.org's macOS toolchain .pkg by URL and sha256,
                       expanded with pkgutil (never installed), and a
                       rules_swift toolchain on it. Embedded Swift uses the
                       same toolchain (@nd_embedded_swift).

None of them runs xcrun or reads DEVELOPER_DIR, except nd_macos_sdk's
fallback when the Command Line Tools' SDK directory is missing.
"""

# --- the macOS SDK ----------------------------------------------------------

_CLT_SDKS = "/Library/Developer/CommandLineTools/SDKs"

def _sdk_version(rctx, sdk):
    settings = rctx.path(sdk + "/SDKSettings.json")
    if not settings.exists:
        return None
    return json.decode(rctx.read(settings)).get("Version")

def _nd_macos_sdk_impl(rctx):
    version = rctx.attr.version
    want = rctx.attr.sha256
    override = rctx.getenv("ND_MACOS_SDK", "")
    # ND_MACOS_SDK, when set, is the only candidate.
    candidates = [override] if override else ["%s/MacOSX%s.sdk" % (_CLT_SDKS, version)]
    if not override and not rctx.path(candidates[-1]).exists:
        # Fallback only: the selected developer directory's SDK.
        res = rctx.execute(["/usr/bin/xcrun", "--sdk", "macosx", "--show-sdk-path"], timeout = 60)
        if res.return_code == 0 and res.stdout.strip():
            candidates.append(res.stdout.strip())
    how = ("Install the Command Line Tools that ship the macOS %s SDK (`xcode-select --install`, " +
           "or the matching \"Command Line Tools for Xcode\" from developer.apple.com/download), " +
           "or point ND_MACOS_SDK at an SDK with this exact content. To move the pin to another " +
           "SDK, update version and sha256 in MODULE.bazel and toolchains/upstream.lock " +
           "(toolchains/sdk/sdk_hash.sh prints the hash).") % version
    tried = []
    for sdk in candidates:
        path = rctx.path(sdk)
        if not path.exists:
            tried.append("%s: missing" % sdk)
            continue
        real = str(path.realpath)
        got_version = _sdk_version(rctx, real)
        if got_version != version:
            tried.append("%s: version %s, not %s" % (sdk, got_version, version))
            continue
        rctx.report_progress("Hashing the macOS SDK at " + real)
        res = rctx.execute(["/bin/sh", str(rctx.path(rctx.attr._hash_script)), real, "manifest.txt"], timeout = 600)
        if res.return_code != 0:
            fail("nd_macos_sdk: hashing %s failed: %s" % (real, res.stderr))
        got = res.stdout.strip()
        if got != want:
            tried.append("%s: version %s, content sha256 %s, not %s" % (sdk, version, got, want))
            continue
        rctx.file("BUILD.bazel", 'exports_files(["sdk.bzl", "manifest.txt"])\n')
        rctx.file("sdk.bzl", "\n".join([
            '"""The pinned macOS SDK (toolchains/repos.bzl), verified at fetch."""',
            'MACOS_SDK = "%s"' % real,
            'MACOS_SDK_VERSION = "%s"' % version,
            'MACOS_SDK_SHA256 = "%s"' % want,
            "",
        ]))
        return
    fail("nd_macos_sdk: no macOS %s SDK with content sha256 %s.\n  %s\n%s" % (version, want, "\n  ".join(tried), how))

nd_macos_sdk = repository_rule(
    implementation = _nd_macos_sdk_impl,
    attrs = {
        "version": attr.string(mandatory = True, doc = "SDKSettings.json Version, exactly."),
        "sha256": attr.string(mandatory = True, doc = "sdk_hash.sh's content hash."),
        "_hash_script": attr.label(default = "//toolchains/sdk:sdk_hash.sh"),
    },
    environ = ["ND_MACOS_SDK"],
    # The SDK is host state, so it's hashed again whenever Bazel refetches
    # (each server start included, about 15 s), and a changed SDK fails the
    # build. (rctx.watch_tree can't watch it: the SDK has symlink cycles.)
    local = True,
)

# --- LLVM (llvm.org) ---------------------------------------------------------

def _sdk_path(rctx):
    # Reads @nd_macos_sdk, which verifies the SDK first.
    text = rctx.read(rctx.path(Label("@nd_macos_sdk//:sdk.bzl")))
    for line in text.splitlines():
        if line.startswith("MACOS_SDK = "):
            return line.split('"')[1]
    fail("nd_llvm_toolchain: @nd_macos_sdk//:sdk.bzl has no MACOS_SDK")

def _nd_llvm_toolchain_impl(rctx):
    rctx.download_and_extract(
        url = rctx.attr.urls,
        sha256 = rctx.attr.sha256,
        stripPrefix = rctx.attr.strip_prefix,
    )
    sdk = _sdk_path(rctx)
    res = rctx.execute(["bin/clang", "-print-resource-dir"])
    if res.return_code != 0:
        fail("nd_llvm_toolchain: bin/clang doesn't run: " + res.stderr)
    resource = res.stdout.strip()
    root = str(rctx.path("."))
    rctx.template("nd/ld64.lld", rctx.attr._ld_wrapper, executable = True)
    rctx.template("BUILD.bazel", rctx.attr._build_tpl, {
        "{sdk}": sdk,
        "{root}": root,
        "{resource_dir}": resource,
        "{major}": resource.rsplit("/", 1)[-1],
        "{repo}": rctx.name,
        "{version}": rctx.attr.version,
        "{min_os}": rctx.attr.min_os,
    })

nd_llvm_toolchain = repository_rule(
    implementation = _nd_llvm_toolchain_impl,
    attrs = {
        "urls": attr.string_list(mandatory = True),
        "sha256": attr.string(mandatory = True),
        "strip_prefix": attr.string(mandatory = True),
        "version": attr.string(mandatory = True),
        "min_os": attr.string(mandatory = True, doc = "Deployment target (arm64-apple-macosMIN_OS)."),
        "_build_tpl": attr.label(default = "//toolchains:llvm.BUILD.tpl"),
        "_ld_wrapper": attr.label(default = "//toolchains:ld64_lld.sh"),
    },
)

# --- Swift (swift.org) -------------------------------------------------------

def _nd_swift_toolchain_impl(rctx):
    name = "swift-%s-RELEASE.xctoolchain" % rctx.attr.version
    rctx.report_progress("Downloading " + rctx.attr.urls[0])
    rctx.download(url = rctx.attr.urls, sha256 = rctx.attr.sha256, output = "swift.pkg")
    rctx.report_progress("Expanding the swift.org package (pkgutil --expand-full)")
    res = rctx.execute(["/usr/sbin/pkgutil", "--expand-full", "swift.pkg", "expanded"], timeout = 1800)
    if res.return_code != 0:
        fail("nd_swift_toolchain: pkgutil --expand-full failed: " + res.stderr)
    payload = None
    for comp in rctx.path("expanded").readdir():
        if rctx.path(str(comp) + "/Payload/usr/bin/swiftc").exists:
            payload = str(comp) + "/Payload"
    if not payload:
        fail("nd_swift_toolchain: no component with Payload/usr/bin/swiftc in the package")
    res = rctx.execute(["/bin/mv", payload, name])
    if res.return_code != 0:
        fail("nd_swift_toolchain: " + res.stderr)
    rctx.delete("expanded")
    rctx.delete("swift.pkg")  # the repository cache keeps the download
    for req in rctx.attr.required:
        if not rctx.path(name + "/" + req).exists:
            fail("nd_swift_toolchain: %s has no %s" % (name, req))
    res = rctx.execute([name + "/usr/bin/swiftc", "-version"])
    if res.return_code != 0:
        fail("nd_swift_toolchain: swiftc doesn't run: " + res.stderr)
    rctx.file("swift_version.txt", res.stdout + res.stderr)
    rctx.template("BUILD.bazel", rctx.attr._build_tpl, {
        "{tc}": name,
        "{version}": rctx.attr.version,
    })
    rctx.file("toolchain.bzl", "\n".join([
        '"""The pinned swift.org toolchain (toolchains/repos.bzl)."""',
        'SWIFT_TOOLCHAIN = "%s"' % rctx.path(name),
        'SWIFT_VERSION = "%s"' % rctx.attr.version,
        "",
    ]))

nd_swift_toolchain = repository_rule(
    implementation = _nd_swift_toolchain_impl,
    attrs = {
        "urls": attr.string_list(mandatory = True),
        "sha256": attr.string(mandatory = True),
        "version": attr.string(mandatory = True),
        "required": attr.string_list(doc = "Paths the toolchain must contain (the Embedded stdlibs, lld-link)."),
        "_build_tpl": attr.label(default = "//toolchains:swift.BUILD.tpl"),
    },
)
