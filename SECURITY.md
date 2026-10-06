<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's private vulnerability reporting on this repository: the **Security** tab, then **Report a vulnerability**. Don't open a public issue, pull request or discussion for a suspected vulnerability.

Include:
- the affected component (kernel, a kext, neoboot, a base program, the build), and the commit or release;
- how to reproduce it, on QEMU or on hardware, with the board;
- the impact as you understand it.

You'll get an acknowledgement within 7 days. A fix is developed privately and released with a security advisory. Credit is given unless you ask otherwise.

## Scope

- **In scope:** everything this repository builds, which is the kernel and its patch series, the kexts and dexts, neoboot, the base userland and its patches, and the signing and trust-cache tooling.
- **Upstream components** (xnu, OpenZFS, OpenSSL, OpenSSH, FreeBSD and Apple command projects, ...): report bugs that also affect upstream to the upstream project as well. NeoDarwin carries the fix as a numbered patch until upstream releases one (`docs/repository.md` §3).
- **Not in scope:** Apple's closed components, which NeoDarwin doesn't ship.

## Supported versions

There are no releases yet. Until 1.0, fixes land on `main` only.

## Signing keys

Release artifacts and repository indexes will be signed with keys described in `docs/architecture/packaging.md`. The rotation policy is defined there once `ndsign` lands (P2-01). This file will name the current key fingerprints when the first release is signed.
