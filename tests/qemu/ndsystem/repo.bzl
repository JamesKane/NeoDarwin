"""The local repository //tests/qemu/ndsystem's test upgrades from (P2-03)."""

# Sets 2 and 3 and their components, as //images:ndsystem_volume carries them.
REPO_FILES = {
    "//tests/qemu/ndsystem:" + p + "_" + v: "private/var/ndpkg-test/sysrepo/%s-%s.ndpkg" % (n, v)
    for v in ["2", "3"]
    for p, n in [("set_pkg", "neodarwin-system"), ("kernel_pkg", "kernel"), ("nd_release_pkg", "nd-release")]
}

def wait_for(text):
    """A shell loop: up to 20 s for TEXT in the boot job's log, then the log."""
    # The boot job's log, from this boot (BEs are clones, so a log holds
    # the boots of the BE it was cloned from too).
    return "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q \"" + text + "\" /var/log/ndpkg-boot.log && break; sleep 1; done; cat /var/log/ndpkg-boot.log; "

