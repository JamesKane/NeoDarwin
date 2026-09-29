<!-- SPDX-License-Identifier: BSD-2-Clause -->
# The ports tree

## 1. Decision (user, 2026-09-29)

**NeoDarwin has its own ports tree, and building a port produces an `ndpkg` package.** A port is a recipe: data that says where the source comes from, how to patch it, how to build it and what it installs. Building the recipe produces a `.ndpkg` with the same manifest, signing and repositories as the base (`packaging.md`), so "install from source" and "install the binary package" end in the same installed state. The shape follows FreeBSD ports (one directory per port, grouped by category, a bulk builder, a binary repository built from the tree). The recipe format is NeoDarwin's own and is plain data.

This replaces the earlier plan of running pkgsrc and converting its binary packages (`pkgsrc2nd`). FreeBSD ports and pkgsrc become **import sources** for recipes (§6), not runtime dependencies.

## 2. Layout

The tree is its own repository, `neodarwin/ports`, checked out at `/usr/ports`:

```
/usr/ports/
  Mk/                         # the make shim (§4) and the build-system vocabulary
  Templates/                  # recipe skeletons
  Keys/                       # the ports signing key chain (packaging.md §4)
  <category>/<name>/
    port.toml                 # the recipe
    patches/NNNN-<topic>.patch
    files/                    # extra sources, launchd plists, default config
    Makefile                  # one line: include ../../Mk/port.mk
```

## 3. The recipe

```toml
schema = 1
name = "tmux"
version = "3.5a"
revision = 0                        # recipe changes without an upstream change
category = "sysutils"
summary = "Terminal multiplexer"
license = ["ISC"]
homepage = "https://github.com/tmux/tmux"
maintainer = "ports@neodarwin.org"

[[distfiles]]
url = "https://github.com/tmux/tmux/releases/download/3.5a/tmux-3.5a.tar.gz"
blake3 = "…"
size = 812345

[depends]
build = ["devel/pkgconf", "devel/bison"]
run = ["devel/libevent >= 2.1", "devel/ncurses"]

[build]
system = "gnu-configure"            # from the fixed vocabulary below
args = ["--enable-utf8proc=no"]

[options]                           # FreeBSD OPTIONS; each combination is a flavour
utf8proc = { default = false, depends.run = ["textproc/utf8proc"], args = ["--enable-utf8proc"] }

[install]
plist = "auto"                      # generated from the staging directory and checked against the last build
services = ["files/org.tmux.server.plist"]   # launchd jobs, registered by the package hook
```

Rules:

- **The build systems are a fixed vocabulary:** `gnu-configure`, `cmake`, `meson`, `make`, `bazel`, `swiftpm`, `cargo`, `go`, `python-pep517`, `perl-mm`. Each one is a module in `Mk/` with stated inputs, so most recipes contain no shell. A port that needs more supplies `files/build.sh`, which runs in the same sandbox and is flagged in the index as a custom build.
- **Dependencies name port origins** (`category/name`) with version ranges. They resolve to packages through the same `libsolv` solver as the base.
- **Flavours:** a non-default option combination builds a package named `name+flavour`. The binary repository carries the default and any flavours the tree lists as `published`.
- **Provenance:** the manifest of a built port records `kind = "port"`, the origin, the recipe's commit, the upstream version, the distfile digests and the licence, so an agent or an auditor can trace any installed file back to its source.

## 4. Commands

| Command | Does |
|---|---|
| `ndports fetch\|extract\|patch\|build\|stage\|package <origin>` | the individual steps |
| `ndports install <origin>` | build if needed, package, then `ndpkg install` the result |
| `ndports search <term>`, `ndports info <origin>` | query the tree's index |
| `ndports update` | update the tree checkout |
| `ndports outdated` | compare installed ports with the tree |
| `ndports bulk <list>` | a poudriere-style bulk build (§5) |

For FreeBSD muscle memory, `make`, `make install`, `make clean`, `make package` and `make config` in a port directory call the same steps through `Mk/port.mk`. Every command takes `--json`.

Source-built packages are signed with a **local key** that `ndports` generates on first use. `ndpkg` trusts it for that machine only, so a locally built package never looks like one from the project's repository.

## 5. Building: clean, repeatable, isolated

- **Build environment.** A build runs in a fresh ZFS clone of a *build boot environment*: the current system set plus the port's declared build dependencies, installed as packages. Nothing else from the host is visible. The clone is destroyed afterwards. This gives poudriere's clean-room property without jails, which xnu does not have (`freebsd-parity.md` §5).
- **Isolation.** The build runs chrooted into the clone as an unprivileged build user. After `fetch`, network access for that user is blocked by a `pf` anchor. Distfiles come only from the recipe's digests.
- **Reproducibility.** `SOURCE_DATE_EPOCH` is set from the recipe's commit, paths are normalised, and packages are built twice on different hosts in the nightly run. A difference files an issue against the port.
- **Bulk builds (P2-11).** The build farm builds the tree in dependency order, publishes a signed `ports` index per channel with a per-port log and status, and skips ports whose dependencies failed.

## 6. Importers (P2-10)

`ndports import freebsd <origin>` and `ndports import pkgsrc <path>` read the other tree's metadata (distfiles and checksums, dependencies, licence, configure arguments, patches) and write a recipe skeleton for review. Patches are carried with their origin recorded. Darwin-specific fixes that pkgsrc or MacPorts already carry are the first place to look when a port fails on NeoDarwin. The importer's goal is a first build, not an unattended conversion; the review step is where a human or agent confirms the licence and the plist.

## 7. Relationship to the base and to downstreams

- **The base never depends on a port.** GPL software ships only as ports (`README.md`, Licensing).
- **Overlays.** A downstream (for example Magi, `downstream.md`) or a user can keep an overlay tree with the same layout. `ndports` searches overlays in priority order, and an overlay recipe with the same origin replaces the one in the tree. Overlay packages go to the overlay's own repository and signing key.
- **Kexts and dexts from ports** declare `kernel-abi` like any other, and a `kernel-abi` bump rebuilds them in the next bulk run.

## 8. Bootstrapping order

P2-01 (format, `nd_package`) → P2-04 (repositories and signing) → P2-05 (the recipe format, `ndports`, the clean-room build, the first 100 ports) → P2-10 (importers) → P2-11 (bulk builder and ports repository). The first ports are the ones the parity suite and self-hosting need: OpenJDK for Bazel (P5-11), `git`, `curl`, `python`, `perl`, `pkgconf`, `bison`, `cmake`, `ninja` and `tmux`.
