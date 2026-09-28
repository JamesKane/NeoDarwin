# BUILD file for @freebsd_crypto: FreeBSD sources pinned by freebsd.lock,
# compiled unmodified (FreeBSD's userland branches, NeoDarwin's prelude).
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

exports_files(glob(["**"]))

filegroup(
    name = "all",
    srcs = glob(["**"], exclude = ["BUILD.bazel"]),
)

_SODIUM = "sys/contrib/libsodium/src/libsodium"

# MD5, SHA-1, SHA-384/512, AES (rijndael), DES, ChaCha20, and the
# poly1305-donna source ndcrypto compiles into nd_chachapoly.c.
cc_library(
    name = "crypto",
    srcs = [
        "sys/crypto/chacha20/chacha.c",
        "sys/crypto/des/des_ecb.c",
        "sys/crypto/des/des_enc.c",
        "sys/crypto/des/des_locl.h",
        "sys/crypto/des/des_setkey.c",
        "sys/crypto/des/podd.h",
        "sys/crypto/des/sk.h",
        "sys/crypto/des/spr.h",
        "sys/crypto/md5c.c",
        "sys/crypto/rijndael/rijndael-alg-fst.c",
        "sys/crypto/rijndael/rijndael_local.h",
        "sys/crypto/sha1.c",
        "sys/crypto/sha2/sha512c.c",
        "sys/crypto/sha2/sha512c_impl.h",
    ],
    hdrs = [
        "sys/crypto/chacha20/_chacha.h",
        "sys/crypto/chacha20/chacha.h",
        "sys/crypto/des/des.h",
        "sys/crypto/rijndael/rijndael.h",
        "sys/crypto/sha1.h",
        "sys/crypto/sha2/sha384.h",
        "sys/crypto/sha2/sha512.h",
        "sys/crypto/sha2/sha512t.h",
        "sys/sys/md5.h",
        _SODIUM + "/include/sodium/crypto_onetimeauth_poly1305.h",
        _SODIUM + "/include/sodium/crypto_verify_16.h",
        _SODIUM + "/include/sodium/export.h",
        _SODIUM + "/include/sodium/private/common.h",
        _SODIUM + "/include/sodium/utils.h",
    ],
    copts = [
        "-include",
        "nd_freebsd.h",
        "-w",
    ],
    includes = [
        "sys",
        _SODIUM,
        _SODIUM + "/include/sodium",
    ],
    textual_hdrs = [
        _SODIUM + "/crypto_onetimeauth/poly1305/donna/poly1305_donna.c",
        _SODIUM + "/crypto_onetimeauth/poly1305/donna/poly1305_donna.h",
        _SODIUM + "/crypto_onetimeauth/poly1305/donna/poly1305_donna64.h",
        _SODIUM + "/crypto_onetimeauth/poly1305/onetimeauth_poly1305.h",
    ],
    deps = ["@@//kernel/neodarwin/crypto:freebsd_compat"],
)

cc_library(
    name = "bearssl_inner",
    hdrs = [
        "contrib/bearssl/src/config.h",
        "contrib/bearssl/src/inner.h",
    ],
    strip_include_prefix = "contrib/bearssl/src",
    deps = [":bearssl_headers"],
)

cc_library(
    name = "bearssl_headers",
    hdrs = glob(["contrib/bearssl/inc/*.h"]),
    strip_include_prefix = "contrib/bearssl/inc",
)

# BearSSL RSA PKCS#1 v1.5 verification, the path FreeBSD's libsecureboot uses.
cc_library(
    name = "bearssl_rsa_verify",
    srcs = [
        "contrib/bearssl/src/codec/ccopy.c",
        "contrib/bearssl/src/int/i31_add.c",
        "contrib/bearssl/src/int/i31_bitlen.c",
        "contrib/bearssl/src/int/i31_decmod.c",
        "contrib/bearssl/src/int/i31_decode.c",
        "contrib/bearssl/src/int/i31_encode.c",
        "contrib/bearssl/src/int/i31_fmont.c",
        "contrib/bearssl/src/int/i31_modpow2.c",
        "contrib/bearssl/src/int/i31_montmul.c",
        "contrib/bearssl/src/int/i31_muladd.c",
        "contrib/bearssl/src/int/i31_ninv31.c",
        "contrib/bearssl/src/int/i31_sub.c",
        "contrib/bearssl/src/int/i31_tmont.c",
        "contrib/bearssl/src/int/i32_div32.c",
        "contrib/bearssl/src/rsa/rsa_i31_pkcs1_vrfy.c",
        "contrib/bearssl/src/rsa/rsa_i31_pub.c",
        "contrib/bearssl/src/rsa/rsa_pkcs1_sig_unpad.c",
    ],
    copts = ["-w"],
    deps = [
        ":bearssl_headers",
        ":bearssl_inner",
    ],
)
