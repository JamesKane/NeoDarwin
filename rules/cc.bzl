"""NeoDarwin C/C++ macros (language policy T4).

First-party C and C++ is a justified fallback. Every nd_cc_* target gets a
lang_audit test over its sources, so a file without a justification line
fails `bazel test`.
"""

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

ND_C_COPTS = ["-std=c23", "-Wall", "-Wextra", "-Werror"]

# Label() resolves in this repository, so the macros also work when a
# downstream module loads them as @neodarwin//rules:cc.bzl.
_LANG_AUDIT = Label("//tools/lang_audit")
_LANG_AUDIT_RUN = Label("//tools/lang_audit:run.sh")

def lang_audit_test(name, srcs, **kwargs):
    """Fails if any file in srcs lacks a NeoDarwin-Language justification."""
    sh_test(
        name = name,
        size = "small",
        srcs = [_LANG_AUDIT_RUN],
        args = ["$(rootpath %s)" % _LANG_AUDIT] + ["$(rootpath %s)" % s for s in srcs],
        data = [_LANG_AUDIT] + srcs,
        **kwargs
    )

def _audited(name, srcs, hdrs):
    files = srcs + hdrs
    if files:
        lang_audit_test(name = name + "_lang_audit", srcs = files)

def nd_cc_library(name, srcs = [], hdrs = [], copts = [], **kwargs):
    cc_library(name = name, srcs = srcs, hdrs = hdrs, copts = ND_C_COPTS + copts, **kwargs)
    _audited(name, srcs, hdrs)

def nd_cc_binary(name, srcs = [], copts = [], **kwargs):
    cc_binary(name = name, srcs = srcs, copts = ND_C_COPTS + copts, **kwargs)
    _audited(name, srcs, [])

def nd_cc_test(name, srcs = [], copts = [], **kwargs):
    cc_test(name = name, srcs = srcs, copts = ND_C_COPTS + copts, **kwargs)
    _audited(name, srcs, [])
