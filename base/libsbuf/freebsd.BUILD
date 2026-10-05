# BUILD file for @freebsd_libsbuf: FreeBSD's lib/libsbuf source
# (sys/kern, sys/sys), pinned by freebsd.lock. base/libsbuf/build.sh compiles
# them; this only exposes them.
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["**"], exclude = ["BUILD.bazel"]),
)
