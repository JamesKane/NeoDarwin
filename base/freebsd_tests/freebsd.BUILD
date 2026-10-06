# BUILD file for @freebsd_tests: the FreeBSD test suite's tests, pinned by
# freebsd.lock. base/freebsd_tests/build.sh installs them; this only exposes them.
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["**"], exclude = ["BUILD.bazel"]),
)
