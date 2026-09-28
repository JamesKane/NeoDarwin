<!-- SPDX-License-Identifier: BSD-2-Clause -->
# ndamfi: the AMFI and Image4 interface provider (P1-14)

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

No implementation of libTrustCache, CoreEntitlements, AMFI or Image4 is published as open source; `apple-oss-distributions` carries only their APSL headers. FreeBSD has no equivalent.

## 2. Behaviour

- **Trust caches.**
  - **Format:** only version 1 modules, the format xnu publishes. At load, a module must be version 1, fit its declared entry count, and list cdhashes in strictly increasing order, because the lookup is a binary search.
  - **Lists:** static and loadable trust caches sit on separate lists. A duplicate UUID is refused. Engineering and legacy types are allowed only when the runtime allows them.
  - **Queries:** they return the entry's hash type and flags. Version 1 modules have no launch-constraint category, so it reads 0.
  - **Loading policy:** default deny. Every type requires the entitlement `neodarwin.trust-cache.load`, and until P1-15 (entitlements and trust-cache signing) nothing holds it.
- **No static trust cache yet.** neoboot doesn't pass one; `load_static_trust_cache()` logs "no external trust caches found" and continues.
- **Image4.** NeoDarwin will sign trust caches with `ndsign`, not Image4. The interface is registered at version 14, so `kern_trustcache.c` itself refuses Image4 objects with embedded manifests. `amfi->TrustCache.load` refuses those with external manifests, and `extractModule` refuses too. No Image4 function is ever called, so the other members stay `NULL`.
- **Entitlements: default deny** until a CoreEntitlements implementation lands:
  - queries return `KERN_DENIED`;
  - copies return `KERN_NOT_FOUND`;
  - the XML and transmuted-blob getters return false;
  - a code blob's entitlement context is accepted as empty, so programs run with no entitlements.

  `amfi->CoreEntitlements` stays unset: only the PPL pmap path reaches it, and SBSA doesn't build that path.

## 3. Verification

- `//kernel/neodarwin/amfi:trustcache_test` (host), 17 checks:
  - loading, duplicates, the engineering-type gate;
  - rejection of version 2, truncated and unsorted modules;
  - every entry of a 100-entry module found with its hash type and flags;
  - misses just past the last entry;
  - static versus loadable queries, UUID lookups, capabilities, and the refused Image4 path.
- `//kernel:sbsa_boot_test`: the kernel boots past trust-cache initialisation.
