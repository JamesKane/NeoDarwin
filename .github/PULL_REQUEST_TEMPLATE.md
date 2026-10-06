<!-- CONTRIBUTING.md: sign off every commit (DCO, `git commit -s`); upstream code changes only through numbered patches. -->

**Backlog item:** <!-- e.g. P4-21 checkpoint 7 -->

**What and why**

**Upstream patches** (new or changed, with their `Rebase-risk:`)

**Tests**
- [ ] `bazel test //...`
- [ ] The QEMU set, if the kernel, neoboot or an image changed: `bazel test $(bazel query 'attr(tags, qemu, tests(//kernel:all + //boot/... + //tests/...))') //kernel:sbsa_isa_audit`
- [ ] Ratchets updated with reasons (`kexts/zfs/tests/expected.tsv`, `base/freebsd_tests/expected.tsv`, `tools/parity`), if results moved

**Docs** updated (the design doc, `roadmap/backlog.yaml`, `THIRD_PARTY_NOTICES.md` for a new upstream)
