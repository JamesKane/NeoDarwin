<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndamfi: the AMFI and Image4 interface provider (P1-14, P1-15)

XNU's code-signing path requires two interfaces that Apple's closed kexts register:
- **The AMFI table** (`libkern/libkern/amfi/amfi.h`), from AppleMobileFileIntegrity. It includes the trust-cache engine (Apple's libTrustCache, also closed) and the entitlement queries.
- **The Image4 interface** (`libkern/libkern/img4/interface.h`), from AppleImage4.

`trust_cache_runtime_init()` (`bsd/kern/kern_trustcache.c`) panics if either is missing, and XNU also links `trustCacheInitializeRuntime()` and `TCTypeConfig` directly. ndamfi (`kernel/neodarwin/amfi`) provides all of them. It is compiled into the kernel and registers at `STARTUP(EARLY_BOOT, STARTUP_RANK_LAST)`, well before trust-cache initialisation in `kernel_bootstrap_thread`.

## 1. Sources, by the reuse order (`repository.md` §3.1)

| Piece | Source | Licence |
|---|---|---|
| Trust-cache module format (`struct trust_cache_module1`, `trust_cache_entry1`, `CS_TRUST_CACHE_*`) | xnu-12377.1.9 `osfmk/kern/trustcache.h`: the current pin, used as is | APSL 2.0 |
| Module lookup (binary search) | `lookup_in_trust_cache_module()`, xnu-8019.80.24 `osfmk/arm/trustcache.c`, the last release that carried it. It is copied verbatim into `nd_tc_lookup.c` (renamed; the rest of that file is tied to state later xnu removed) | APSL 2.0 |
| libTrustCache runtime and the `amfi->TrustCache` functions | `nd_trustcache.c`, new, over NeoDarwin's `TrustCache/API.h` shim (`kernel/sdk`) | BSD-2-Clause |
| AMFI entitlement members, Image4 registration | `nd_amfi.c`, new | BSD-2-Clause |
| DER entitlements reader (CoreEntitlements' format, as `codesign` writes it; the interface in xnu's `EXTERNAL_HEADERS/CoreEntitlements`) | `nd_entitlements.c`, new | BSD-2-Clause |
| Entitlement objects (`OSEntitlements`, libkern C++) | `nd_entitlements_os.cpp`, new; XML plists through libkern's `OSUnserializeXML` | BSD-2-Clause |
| Code-signing MAC policy and enforcement | `nd_amfi_policy.c`, new | BSD-2-Clause |
| Trust-cache writer and cdhash computation (host) | `tools/trustcache`, new (Swift) | BSD-2-Clause |

No implementation of libTrustCache, CoreEntitlements, AMFI or Image4 is published as open source; `apple-oss-distributions` carries only their APSL headers. FreeBSD has no equivalent.

## 2. Behaviour

- **Trust caches.**
  - **Format:** only version 1 modules, the format xnu publishes. At load, a module must be version 1, fit its declared entry count, and list cdhashes in strictly increasing order, because the lookup is a binary search.
  - **Lists:** static and loadable trust caches sit on separate lists. A duplicate UUID is refused. Engineering and legacy types are allowed only when the runtime allows them.
  - **Queries:** they return the entry's hash type and flags. Version 1 modules have no launch-constraint category, so it reads 0.
  - **Loading policy:** static trust caches come from the loader (§4.2). A runtime load needs, in XNU, the entitlement `com.apple.private.pmap.load-trust-cache` with the value `neodarwin.trust-cache.load`, and, in ndamfi, a grant that a registered verifier accepts; none is registered until P2-01 (§4.5), so every runtime load is refused.
- **Image4.** NeoDarwin will sign trust caches with `ndsign`, not Image4. The interface is registered at version 14, so `kern_trustcache.c` itself refuses Image4 objects with embedded manifests. `amfi->TrustCache.load` refuses those with external manifests, and `extractModule` refuses too. No Image4 function is ever called, so the other members stay `NULL`.
- **Entitlements** (P1-15, §4.1): a trusted binary has exactly the entitlements its signature carries; any other binary, and any blob that doesn't parse, has none. `amfi->CoreEntitlements` stays unset: only the PPL pmap path reaches it, and SBSA doesn't build that path.
- **Code-signing policy** (P1-15, §4.3): trust-cached binaries are platform binaries; under enforcement, the default when neoboot passes a trust cache, nothing else runs.

## 3. Verification (P1-14)

- `//kernel/neodarwin/amfi:trustcache_test` (host), 17 checks:
  - loading, duplicates, the engineering-type gate;
  - rejection of version 2, truncated and unsorted modules;
  - every entry of a 100-entry module found with its hash type and flags;
  - misses just past the last entry;
  - static versus loadable queries, UUID lookups, capabilities, and the refused Image4 path.
- `//kernel:sbsa_boot_test`: the kernel boots past trust-cache initialisation.

## 4. Entitlements, trust caches and enforcement (P1-15)

The exit: a platform binary listed in a neoboot-supplied trust cache runs with its entitlements; one not listed is refused under enforcement. What Apple's AMFI kext decides with its closed MAC policy, NeoDarwin decides in ndamfi, from the trust caches alone: there are no certificates, no provisioning profiles and no Team IDs. Everything a trust cache lists is a platform binary, and nothing else is trusted.

### 4.1 Entitlements

- **Where they come from.** `ubc_cs_blob_add()` calls the MAC policy (§4.3), which attaches an `NDEntitlements` object (`nd_entitlements_os.cpp`, an `OSObject`) to a trusted binary's signature, and none to any other. Once the signature's storage is final, XNU calls `amfi->OSEntitlements.adjustContextWithoutMonitor`, and the object reads the signature's entitlements:
  - the DER blob (slot 7, `0xfade7172`) when there is one, through `nd_entitlements.c`;
  - otherwise the XML plist (slot 5, `0xfade7171`), through libkern's `OSUnserializeXML`.

  XNU checks each blob's hash against the code directory's special slot first (`csblob_get_der_entitlements`, `csblob_get_entitlements`). A mismatch refuses the signature; a blob that doesn't parse grants nothing (logged `ndamfi: ID: entitlements unreadable; none granted`). When both blobs are present the DER one is authoritative; the XML is kept only for `csops`.
- **The DER format.** CoreEntitlements' format, as `codesign` writes it: `[APPLICATION 16] { INTEGER 1, [CONTEXT 16] { SEQUENCE { UTF8String key, value } ... } }`, where a value is a BOOLEAN, INTEGER, UTF8String, OCTET STRING, SEQUENCE (an array) or `[CONTEXT 16]` dictionary. The reader validates the whole blob before reading anything: definite minimal DER lengths, known types, DER booleans (`0x00`/`0xff`), minimal integers of up to 8 bytes, no NUL in a string, keys strictly increasing, at most 16 levels. It allocates nothing, so the same code runs in the kernel and in the host test.
- **Queries.** Over the object's dictionary:
  - `queryEntitlementBoolean` (`IOTaskHasEntitlement`): granted only for the value true;
  - `queryEntitlementString` (`IOTaskHasStringEntitlement`): the string itself, or an array holding it;
  - `copyEntitlementAsOSObject` (`IOTaskGetEntitlement`, `IOUserClient::copyClientEntitlement`): the value, retained;
  - `OSEntitlements_asdict` (`IOUserClient::copyClientEntitlements`): the dictionary, retained;
  - `OSEntitlements_get_xml` (`csops(CS_OPS_ENTITLEMENTS_BLOB)`): a copy of the XML blob, for a trusted binary only;
  - `OSEntitlements_get_transmuted`: none. `csops(CS_OPS_DER_ENTITLEMENTS_BLOB)` returns the signature's own DER blob, trusted or not: it is the signature's content, not a grant.

  The `...WithProc` forms read the process's main executable (`csproc_get_blob`) and grant nothing to a process that has lost `CS_VALID`. `OSEntitlements_invalidate` (a signature whose vnode changed) makes the object grant nothing.

### 4.2 Static trust caches: the tool, the image and the load path

- **The tool.** `//tools/trustcache` (Swift, no Foundation in its library):
  - `trustcache create OUT [--manifest FILE] [--exclude PATH]... ROOT` lists every signed arm64 Mach-O under a staged image root (executables, dylibs, bundles, dyld; thin or a universal file's arm64 slices; symbolic links not followed, since their targets are in the image).
  - The cdhash is XNU's: the code directory whose hash type ranks highest (`hashPriorities` in `ubc_subr.c`), hashed with that type, first 20 bytes. Only SHA-256 directories are listed (hash type 2 in the entry, flags 0), which is all `ld` and `codesign` write for arm64.
  - Before listing a binary the tool verifies its signature as the kernel will at run time: every code page and every embedded special blob (requirements, XML and DER entitlements) against the directory's hashes. An unsigned Mach-O, or one whose signature doesn't match its contents, fails the build unless excluded.
  - The output is a version 1 module (`osfmk/kern/trustcache.h`): entries sorted and unique, and a UUID derived from them, so the same binaries give the same bytes.
  - `trustcache cdhash FILE` prints a file's cdhashes; `trustcache dump MODULE` checks and prints a module.
- **The images.** Every `hfs_ramdisk` (`rules/ramdisk.bzl`) runs the tool over its staged root in the same action as `hdiutil`, so the trust cache lists exactly what the volume holds. `NAME_trustcache` is the module and `NAME_trustcache_manifest` its `CDHASH PATH` listing; `trust_cache_exclude` leaves paths out. `//images:session_disk` and `session_disk_intx` carry `session_root_volume`'s module on the ESP as `\NeoDarwin\trustcache`. The ramdisk tests pass `NAME_trustcache` beside `NAME` with `--esp`. The session volume lists 106 binaries.
- **The load path.**
  - neoboot reads `\NeoDarwin\trustcache` before placing anything. It checks the module as ndamfi will (version 1, entries within the file, strictly increasing: `TrustCache.swift`) and refuses to boot with one the kernel would reject, since `load_static_trust_cache()` panics on a module it can't load. It logs `neoboot: trust cache: N entries, UUID ...`.
  - neoboot places the module behind iBoot's `trust_cache_offsets_t` header (one cache at offset 8), in whole pages just below the kernel collection, where iBoot puts it: `physBase` and `virtBase` then start there. It names the segment in `/chosen/memory-map` `TrustCache` (`dt-abi.md`).
  - XNU's `load_static_trust_cache()` (`bsd/kern/kern_trustcache.c`) reads it through the physmap and loads the first module as the static trust cache through `amfi->TrustCache.loadModule`. ndamfi logs `ndamfi: static trust cache UUID: N entries`, and XNU `loaded external trust cache module: 0`.
  - `sysctl security.codesigning.trustcaches.num_static` reads 1.

  Without the file the kernel logs `no external trust caches found`, as before.

### 4.3 The policy (`nd_amfi_policy.c`)

A MAC policy, `ndamfi`, registered at `EARLY_BOOT` with the interfaces. Its `mpo_vnode_check_signature` runs whenever XNU attaches a code signature to a vnode: at exec for the executable and dyld, and through `F_ADDFILESIGS_RETURN` for each library dyld maps.

- **A listed signature.** Its cdhash is in a loaded trust cache, static or loadable, with the hash type the signature uses. It gets `CS_SIGNED` (without which XNU never looks a process's signature up: `csproc_get_blob`), `CS_PLATFORM_BINARY` and an entitlements object (§4.1), and under enforcement `CS_HARD | CS_KILL`: an invalid page is refused and kills the process.
- **Any other signature.**
  - Under enforcement it is refused (`EPERM`, logged `ndamfi: refused NAME (cdhash ...): not in a trust cache`, counted in `security.codesigning.neodarwin.refused`). XNU then fails the exec: with process enforcement on, `load_code_signature()`'s failure is fatal (`load code signature error 4`). The exec is past its point of no return, so the process is killed (zsh: `killed`, status 137). dyld can't add the library's signature and `dlopen` fails.
  - Without enforcement it is accepted (`CS_SIGNED`): ad hoc, not a platform binary, entitled to nothing.
- **Debuggers.** `mpo_proc_check_run_cs_invalid`: under enforcement, `ptrace` can't lift `CS_KILL`/`CS_HARD` unless the target is entitled `get-task-allow`.

### 4.4 Enforcement: the default and the boot-args

XNU's switch is `cs_process_enforcement_enable` (`bsd/kern/kern_cs.c`). `CONFIG_ENFORCE_SIGNED_CODE` sets it on iOS; `MASTER.arm64.MacOSX` doesn't, so macOS leaves it off and AMFI refuses what it must. ndamfi sets it at `EARLY_BOOT`, after `cs_init()` and while it is still writable:
- **The default.** On when neoboot supplied a static trust cache (`/chosen/memory-map TrustCache`), off otherwise. Every image NeoDarwin builds carries one, so every NeoDarwin boot enforces.
- **`nd_cs_enforcement=0`** turns it off (`//kernel:sbsa_cs_permissive_test`); **`nd_cs_enforcement=1`** forces it on without a trust cache, when nothing can run.
- **`cs_enforcement_disable=1`**, XNU's own boot-arg, honoured only on debug-enabled boots (`debug=`, which neoboot's default command line sets), also turns it off.

The decision is logged as `ndamfi: code-signing enforcement on (a static trust cache from the loader): code no trust cache lists is refused` and readable as `sysctl security.codesigning.neodarwin.enforcement`.

With process enforcement on, XNU also refuses an exec without a signature, marks every map `cs_enforcement` (no page executes unless a signature covers it; no writable and executable mappings), and refuses `MH_ALLOW_STACK_EXECUTION`. Nothing in the session image needed any of these.

**dyld** (`base/dyld/src/nd_amfi.c`, the stand-in for libamfi's `amfi_check_dyld_policy_self`) applies AMFI's rule for restricted processes over what the kernel reports. A process is restricted when it is setugid, has a `__RESTRICT` segment or `CS_RESTRICT`, or is entitled (`csops` reports an XML or DER entitlements blob). A restricted process keeps `@`-paths, the classic fallback paths and interposing by the libraries it links, but gets no `DYLD_*` variables. Other processes get everything, as before. What code dyld may load is the kernel's decision.

### 4.5 Runtime trust-cache loads: the hook P2-01 fills

XNU's `load_trust_cache_with_type()` passes a payload and an external manifest to `amfi->TrustCache.load`. It first checks the caller's `com.apple.private.pmap.load-trust-cache` entitlement against `TCTypeConfig` (`neodarwin.trust-cache.load` for every type), which the entitlements of §4.1 now answer. For NeoDarwin the payload is a version 1 module and the manifest is its grant. `nd_tc_load()` accepts a load only when a verifier registered with `nd_tc_set_grant_verifier()` returns true for it; then the module is checked and loaded as a static one is, onto the loadable list. None is registered, so every runtime load is refused, and static loads never go this way. Nothing in NeoDarwin calls `load_trust_cache*()` yet: on Apple systems AMFI's user client does.

**P2-01 status.** Checkpoint 1 is built: ndsign's grant format and its C verifier, `nd_ndsign_verify_tc_grant()` (`nd_ndsign.c`, with Ed25519 through `nd_ed25519.c`), host-tested by `//kernel/neodarwin/amfi:ndsign_test` against grants `//tools/ndsign` writes (`packaging.md` §4.1). The kernel doesn't build them yet and no verifier is registered: items 1–3 below are checkpoint 2 (`packaging.md` §4.2 has the plan).

P2-01 must provide:
1. **The grant format and its verifier.** An ndsign signature over the module, checked against the package key chain, including what the signature binds: the module's bytes, its trust-cache type, and the signer's authority for that type. Registered with `nd_tc_set_grant_verifier()` from ndamfi's startup.
2. **A userland entry point** that calls `load_trust_cache(module, size, grant, grant_size)`, for example an ndamfi sysctl or user client, for a process entitled `com.apple.private.pmap.load-trust-cache` = `neodarwin.trust-cache.load`.
3. **The static trust cache's own authentication.** Today neoboot passes whatever `\NeoDarwin\trustcache` holds (§4.6).

### 4.6 What this does not protect

- **The trust cache and root are unauthenticated.** `\NeoDarwin\trustcache` on the ESP and the HFS+ root (`storage.md`, patch 0028) are as trustworthy as the disk. Someone who can write the disk can replace a listed binary's trust-cache entry, or delete the file, which turns enforcement off by default (or add `nd_cs_enforcement=0` to `boot.cfg`). Enforcement stops code arriving at run time: downloaded, written by a process, or copied in. It is not a secure boot chain, which needs neoboot, the kernel collection and the trust cache verified (P2-01's ndsign) and a sealed root.
- **No monitor.** There is no PPL or TXM on SBSA: the trust caches, the policy and XNU's code-signing state are ordinary kernel memory, so a kernel write primitive defeats all of it (`kern_trustcache.c` says as much of "older HW").
- **dyld's policy is dyld's.** Environment filtering for restricted processes runs in the process. The kernel's refusal of untrusted code doesn't depend on it.
- **Everything listed is fully trusted.** There is one trust level: a listed binary is a platform binary with every entitlement its signature names. The build decides what is listed and signed with which entitlements (`codesign --entitlements` for the probes; `ld -adhoc_codesign` writes none).

### 4.7 Findings

| Finding | Fix |
|---|---|
| With the trust cache after the ramdisk, above the kernel, the kernel died before printing anything. `arm_vm_init()` requires `/chosen/memory-map TrustCache` below the kernel's lowest segment and maps it as `EXTRADATA`; a RELEASE kernel panics ("TrustCache region is in an unexpected place") before the console is up | neoboot puts it below the collection and starts `physBase`/`virtBase` there, as iBoot does |
| Trusted binaries ran, but `csops` saw no platform bit and no entitlements, and the kernel denied the entitled probe. XNU looks a process's signature up (`csproc_get_blob`) only when the process has `CS_SIGNED`, which AMFI's hook sets | the policy sets `CS_SIGNED` on every signature it accepts |
| An unlisted executable isn't refused with an error: the signature is loaded past exec's point of no return, so XNU kills the process (`load code signature error 4`, SIGKILL, status 137 in zsh) | expected: Apple systems do the same |

### 4.8 Verification

- **Host tests.**
  - `//kernel/neodarwin/amfi:entitlements_test`, 24 checks: a blob `codesign` wrote (booleans true and false, a string, an array, an integer, a nested dictionary); boolean and string queries with XNU's semantics; iteration; and rejection of every damaged copy (version, truncation, trailing bytes, a non-DER boolean, NUL in a key, an unknown type, unsorted keys, a non-minimal length, empty, nesting past 16).
  - `//kernel/neodarwin/amfi:trustcache_test`, 25 checks: P1-14's, the grant hook (refused without a verifier; a bad grant or a static type refused; a granted module loaded as loadable), and a module the tool wrote over itself, loaded as the static trust cache and found with hash type 2.
  - `//tools/trustcache:trustcache_test`, 17 checks against `codesign` as the reference: cdhashes of a linker-signed binary and of a copy re-signed with entitlements; a module that is sorted, 24 + 22·n bytes, hash type 2, flags 0, byte-identical across runs; a manifest that skips links and non-Mach-O files; a tampered binary refused (naming the page) unless excluded; an unsigned one refused; a truncated module refused by `dump`.
  - `//tools/dtdump`: the golden tree with `TrustCache`, and one too short refused.
- **Boot tests.**
  - `//kernel:sbsa_boot_test` and every ramdisk and disk boot test boot under enforcement with their image's trust cache.
  - `//kernel:sbsa_mockfs_boot_test` boots without one: enforcement off, `no external trust caches found`.
  - `//kernel:sbsa_cs_enforcement_test` is the exit, on `session_disk`. The listed probe runs as a platform binary with `CS_KILL` and its entitlements: the kernel lets it, as uid −2, force case-sensitive lookups, which needs `com.apple.private.iopol.case_sensitivity`. It `dlopen`s the listed `libutil`. The unlisted library and the unlisted probe are refused (dyld: `code signature invalid ... (errno=1)`; the exec killed, status 137), and `sysctl` shows enforcement 1 and one static trust cache.
  - `//kernel:sbsa_cs_permissive_test`: with `nd_cs_enforcement=0` the unlisted probe runs as a non-platform binary with no entitlements, and the kernel denies it; the listed one is still granted.
