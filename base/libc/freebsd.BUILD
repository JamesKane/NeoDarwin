# BUILD file for @freebsd_libcrypt: FreeBSD's libcrypt schemes and digests,
# pinned by freebsd.lock. base/libc/build.sh compiles them into
# libsystem_c; the cc_library builds the same files for the host test
# (//tests/crypt).
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "all",
    srcs = glob(["**"], exclude = ["BUILD.bazel"]),
)

# crypt_md5, crypt_blowfish, crypt_sha256 and crypt_sha512, compiled as
# libsystem_c compiles them (FreeBSD's userland branches, NeoDarwin's
# prelude for <sys/endian.h> and explicit_bzero).
cc_library(
    name = "libcrypt",
    srcs = [
        "lib/libcrypt/crypt-md5.c",
        "lib/libcrypt/crypt-sha256.c",
        "lib/libcrypt/crypt-sha512.c",
        "lib/libcrypt/crypt.h",
        "lib/libcrypt/misc.c",
        "secure/lib/libcrypt/blowfish.c",
        "secure/lib/libcrypt/blowfish.h",
        "secure/lib/libcrypt/crypt-blowfish.c",
        "sys/crypto/md5c.c",
        "sys/crypto/sha2/sha224.h",
        "sys/crypto/sha2/sha256.h",
        "sys/crypto/sha2/sha256c.c",
        "sys/crypto/sha2/sha256c_impl.h",
        "sys/crypto/sha2/sha384.h",
        "sys/crypto/sha2/sha512.h",
        "sys/crypto/sha2/sha512c.c",
        "sys/crypto/sha2/sha512c_impl.h",
        "sys/crypto/sha2/sha512t.h",
        "sys/sys/md5.h",
    ],
    copts = [
        "-include",
        "nd_freebsd.h",
        "-fvisibility=hidden",
        "-w",
    ],
    # lib/libcrypt: crypt-blowfish.c's "crypt.h".
    includes = [
        "lib/libcrypt",
        "sys",
        "sys/crypto/sha2",
        "sys/sys",
    ],
    deps = ["@@//kernel/neodarwin/crypto:freebsd_compat"],
)
