# BUILD file for @freebsd_msun: the FreeBSD sources libsystem_m builds, pinned
# by freebsd.lock. base/libm/build.sh compiles them; this only exposes them.
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["**"], exclude = ["BUILD.bazel"]),
)
