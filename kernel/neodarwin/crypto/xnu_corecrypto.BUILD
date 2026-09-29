# BUILD file for @apple_xnu_corecrypto: the same pinned xnu archive as
# @apple_xnu, exposing the xnu sources NeoDarwin's host tests build against:
# xnu's own corecrypto subset (osfmk/corecrypto), the APSL corecrypto and
# libkern crypto headers (ndcrypto), and the trust-cache format (ndamfi).
# Only these headers are exposed: EXTERNAL_HEADERS as a whole shadows libc.
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "corecrypto_headers",
    hdrs = glob(["EXTERNAL_HEADERS/corecrypto/*.h"]),
    defines = ["CC_USE_ASM=0"],  # as in the kernel (CC_XNU_KERNEL_PRIVATE)
    strip_include_prefix = "EXTERNAL_HEADERS",
)

# Some xnu sources include corecrypto headers without the directory.
cc_library(
    name = "corecrypto_headers_bare",
    hdrs = glob(["EXTERNAL_HEADERS/corecrypto/*.h"]),
    strip_include_prefix = "EXTERNAL_HEADERS/corecrypto",
)

cc_library(
    name = "corecrypto_internal",
    hdrs = glob(["osfmk/corecrypto/*.h"]),
    strip_include_prefix = "osfmk/corecrypto",
)

cc_library(
    name = "libkern_crypto_headers",
    hdrs = [
        "libkern/libkern/crypto/crypto.h",
        "libkern/libkern/crypto/rand.h",
        "libkern/libkern/crypto/register_crypto.h",
    ],
    strip_include_prefix = "libkern",
)

cc_library(
    name = "corecrypto",
    srcs = glob(["osfmk/corecrypto/*.c"]),
    copts = [
        "-D__STDC_WANT_LIB_EXT1__=1",
        "-w",
    ],
    deps = [
        ":corecrypto_headers",
        ":corecrypto_headers_bare",
        ":corecrypto_internal",
        ":libkern_crypto_headers",
    ],
)

# Apple's trust-cache module format, for ndamfi's host tests.
cc_library(
    name = "kern_trustcache_headers",
    hdrs = [
        "osfmk/kern/cs_blobs.h",
        "osfmk/kern/trustcache.h",
    ],
    strip_include_prefix = "osfmk",
)

# The video console's 8x16 font, with which qemu_efi_test.sh reads text back
# from a screendump (//kernel:sbsa_fb_console_boot_test).
exports_files(["osfmk/console/iso_font.c"])
