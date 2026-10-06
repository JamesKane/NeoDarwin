"""QEMU boot tests (P0-04, docs/architecture/build-system.md §4).

qemu_test boots an EFI application (neoboot by default) on QEMU through
tools/efi/qemu_efi_test.sh, captures the guest's serial output and passes
when every expected line has appeared. It is a macro over sh_test that
builds the script's arguments and the test's data from named attributes,
so a BUILD file states the machine, the ESP's files, the steps to type and
the lines to expect, not the script's command line.

Strings in expect and send_after are the script's literal text: the macro
single-quotes them and doubles every "$" for Bazel's make-variable
expansion. A backslash stays a backslash, so "\\n" in a Starlark string is
the two characters the script reads as Enter. extra_args are passed as
written (the script's rarer flags, already quoted, with $(rootpath ...)
where they name a file).
"""

load("@rules_shell//shell:sh_test.bzl", "sh_test")

_SCRIPT = "//tools/efi:qemu_efi_test.sh"

def _quote(s):
    """One shell word for a Bazel test argument: single-quoted, with $ escaped."""
    return "'" + s.replace("$", "$$").replace("'", "'\\''") + "'"

def qemu_test(
        name,
        expect,
        efi = "//boot/neoboot",
        esp = {},
        machine = None,
        cpu = None,
        mem = None,
        smp = None,
        send_after = [],
        until_lines = True,
        timeout_s = 60,
        extra_args = [],
        data = [],
        size = "medium",
        tags = [],
        **kwargs):
    """Boots an EFI application on QEMU and requires lines on its serial console.

    Args:
      name: the test's name.
      expect: lines that must appear on serial (substrings of a line), in any order.
      efi: the EFI application placed as \\EFI\\BOOT\\BOOTAA64.EFI on the ESP.
      esp: dict from a file's label to its path on the ESP, e.g.
        {":sbsa_kc": "NeoDarwin/kernelcache"}.
      machine: virt (the script's default), virt-secure or sbsa-ref.
      cpu: QEMU CPU model (the script's default: cortex-a76; neoverse-n2 on sbsa-ref).
      mem: guest RAM, e.g. "2G" (the script's default depends on the machine).
      smp: number of CPUs (default 1).
      send_after: list of (line, text) pairs: once line appears on serial,
        after the previous step's match, type text ("\\n" is Enter).
      until_lines: pass as soon as every expected line has appeared and stop
        QEMU (a kernel never powers off); False requires QEMU to exit.
      timeout_s: seconds the guest has to pass.
      extra_args: further qemu_efi_test.sh options, passed as written.
      data: further files the test needs (those named in extra_args).
      size: the test's size.
      tags: tags; "qemu" is always added.
      **kwargs: passed to sh_test.
    """
    args = []
    files = [efi]
    if machine:
        args.append("--machine " + _quote(machine))
    if cpu:
        args.append("--cpu " + _quote(cpu))
    if mem:
        args.append("--mem " + _quote(mem))
    if smp != None:
        args.append("--smp %d" % smp)
    for label, path in esp.items():
        args.append("--esp %s=$(rootpath %s)" % (_quote(path), label))
        files.append(label)
    if until_lines:
        args.append("--until-lines")
    for step in send_after:
        if len(step) != 2:
            fail("%s: each send_after step is a (line, text) pair, not %r" % (name, step))
        args.append("--send-after %s %s" % (_quote(step[0]), _quote(step[1])))
    args.extend(extra_args)
    args.append("$(rootpath %s)" % efi)
    args.append("%d" % timeout_s)
    args.extend([_quote(line) for line in expect])

    # One entry per target: :neoboot and //boot/neoboot name the same one.
    seen = {}
    deduped = []
    for f in files + data:
        key = str(native.package_relative_label(f))
        if key not in seen:
            seen[key] = True
            deduped.append(f)

    sh_test(
        name = name,
        size = size,
        srcs = [_SCRIPT],
        args = args,
        data = deduped,
        tags = tags + ([] if "qemu" in tags else ["qemu"]),
        **kwargs
    )
