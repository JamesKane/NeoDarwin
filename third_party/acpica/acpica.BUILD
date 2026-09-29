# BUILD file for the ACPICA release archive (@acpica).
#
# kernel_srcs: what the SBSA kernel compiles (kernel/BUILD.bazel overlays it at
# iokit/ndacpi/acpica; patch 0018 lists the objects). The OS-independent core
# only: no debugger, disassembler, compiler or tools, and no rsdump.c, which
# is debugger code (it doesn't compile without ACPI_DEBUGGER). utclib.c and
# utprint.c are ACPICA's own C library, used in place of the kernel's
# (acnd.h).
filegroup(
    name = "kernel_srcs",
    srcs = glob(
        [
            "source/include/*.h",
            "source/include/platform/acenv.h",
            "source/include/platform/acenvex.h",
            "source/include/platform/acgcc.h",
            "source/include/platform/acgccex.h",
            "source/include/platform/acmacosx.h",
            "source/components/dispatcher/*.c",
            "source/components/events/*.c",
            "source/components/executer/*.c",
            "source/components/hardware/*.c",
            "source/components/namespace/*.c",
            "source/components/parser/*.c",
            "source/components/resources/*.c",
            "source/components/tables/*.c",
            "source/components/utilities/*.c",
        ],
        exclude = ["source/components/resources/rsdump.c"],
    ) + ["LICENSE"],
    visibility = ["//visibility:public"],
)
