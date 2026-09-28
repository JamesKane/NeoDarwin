<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Consistency pass: window protocol v2, scheduling contract, audio service

Date: 2026-09-28. The specifications checked are:
- `docs/desktop/window-protocol.md` v2 and `desktop.h` (WP);
- `docs/kernel/scheduling-contract.md` v1 and `nd_sched.h` (SC);
- `docs/audio/audio-service.md` v1 and `nd_audio.h` (AU).

Rules followed (spec-conventions.md §3, §7):
- No requirement id was renumbered, removed or withdrawn. Ids were only added.
- The lower layer (SC) owns the clock, the wait, readiness and admission. WP and AU now cite SC ids instead of placeholders.
- `docs/architecture/namespaces-agents.md` was not edited. Paths and tokens missing from it are recorded as open issues in the spec that introduces them.

## 1. Change log

### WP (`window-protocol.md`, `desktop.h`)

| Section | Before | After | Reason |
|---|---|---|---|
| Intro paragraph | "how a client waits … is the scheduling contract's" | adds the clock, and says WP cites SC ids | Clock ownership |
| §1 Non-goals | `[see scheduling-contract.md §wait]` | SC-WAIT-001, SC-9P-001 to 015, SC-USER-001 to 004, SC-TIMER-001, SC-TIMER-003, SC-LIB-001, SC-INT-002 | Placeholder reference |
| §2 *deadline clock* | "the monotonic clock of the scheduling contract [see …]" (open issue 4: `mach_continuous_time` until decided) | the SC clock, `mach_absolute_time` in ns (SC-TIME-001); every ns time (`nsec`, `target`, `presented`, `present at NS`) is on it (SC-TIME-002); continuous time is derived at the edge (SC-TIME-003) | Known issue 1 (the clock) |
| §4.5 WP-EV-013 | "kqueue readiness … is the scheduling contract's [see …]" | cites SC-9P-006/008/009, SC-USER-001/002, SC-TIMER-001, SC-LIB-001, SC-WAIT-001; states that `Tready` is not a read for WP-EV-016 | Placeholder; readiness semantics |
| §4.5 | — | **new WP-EV-041**: every `events` file reports length 0, so that nd9p classifies it as a stream file (SC-9P-001) | Known issue 5: SC's length-0 rule needed a WP requirement |
| §4.9 reference loop | `/* kqueue, see scheduling-contract.md */` | `nd_wait` on a kqueue (SC-LIB-001); readable per SC-9P-006/009 | Placeholder |
| §5 | `DESK_API_VERSION` is 2 | adds `WP_API_VERSION`, the same value | Conventions §5 name `<PREFIX>_API_VERSION` |
| §7 WP-PERF-005 | "readable within 1 ms" (undefined point) | readable = the client's `kevent` has returned; SC-PERF-004 bounds queued→`kevent` at 500 µs, leaving 500 µs from sample to queue | Known issue 5 (latency bound) |
| §8 WP-T-070, 087, 135 | "scheduling contract", "deadline clock" | WP-T-070 also covers WP-EV-041 and cites SC-9P-006; WP-T-087 checks against `nd_sched_now_ns`; WP-T-135 names SC-T-023's segment | Follows the text |
| §9 "Readiness semantics here" | generic | cites SC-9P-004 to 015, SC-USER-001, SC-TIMER-001, SC-LIB-001, SC-TIME-001 | Placeholder |
| §10 open issues 4, 5 | open, with placeholders | **Resolved** (numbering kept); 5 records the remaining multi-open edge (SC open issue 9) | Known issues 1, 5 |
| §10 open issue 8 | token placeholder | also notes that namespaces-agents.md does not list `cap:wsys:input:background` | Known issue 6 |
| §10 | — | **new 13**: namespaces-agents.md §7 names `/n/wsys/wins`, not `/n/desktop/windows` | Known issue 6 |
| §11 | — | changelog row | Conventions §2 |
| `desktop.h` | `nsec` "on the deadline clock (see the text)"; `target`/`presented` with no clock | "SC clock: mach_absolute_time in ns (SC-TIME-001, SC-TIME-002)"; `WP_API_VERSION` | Header mirrors the text |

### SC (`scheduling-contract.md`; `nd_sched.h` unchanged)

No SC requirement changed. SC already fixed the clock, `Tready`/`Rready`, SC-PERF-004 and `nd_sched_error_detail()`.

| Section | Before | After | Reason |
|---|---|---|---|
| §2 *stream file* | Plan 9 files report 0 | also cites WP-EV-041 and AU-NS-009, which now require it | Known issue 5 |
| §4.0 last bullet | "Until it adopts this clock, wsys's continuous timestamps …" | WP v2 and AU adopt the SC clock; a `wsys` still on continuous time does not conform | Known issue 1 |
| §4.9 (informative) | mixer and stream threads state the deadline "each period"; refusal "carries the `sc_rt_refusal`"; notices "as audio events" | late wakes only (AU-STREAM-007, AU-MIX-008); `au_error_refusal` (AU-ABI-006); `AU_EV_RT_NOTICE` (AU-EVENT-006); D ≤ T (AU-STREAM-004) | Kept in step with AU; see the AU rows below |
| §10 open issue 3 | wsys on continuous time | **Resolved in the specifications**; the implementation (P4-03) remains | Known issue 1 |
| §10 | — | **new 9**: two opens of one shared WP stream versus SC-9P-011 | Known issue 5 (edge case) |
| §10 | — | **new 10**: `/n/sys/sched`, `cap:sched:admin` and entitlement `sched.rt.system` are not in namespaces-agents.md | Known issue 6 |
| §11 | — | changelog row | Conventions §2 |

### AU (`audio-service.md`, `nd_audio.h`, `BUILD.bazel`, new `nd_audio_cxx.cpp`)

| Section | Before | After | Reason |
|---|---|---|---|
| Status block | header C23 | C23 and C++20, and names the check files | Known issue 4 |
| Intro "leaves to other documents" | "being written at the same time" | SC owns admission, the one wait and the clock; AU cites ids | Placeholder |
| §2 *Host time*, *SC* | `mach_absolute_time` ticks | = SC clock in ticks (SC-TIME-001/002); `au_host_to_ns` gives what `nd_wait`/`nd_rt_set_deadline` take; stops in sleep (SC-TIME-003). *SC*: `nd_rt_admit` admits **and joins** (SC-RT-001) | Known issue 1: AU anchor checked and consistent |
| §4.1 tree, `events` | "readable per SC" | **new AU-NS-009**: `events` files report length 0 and are readable per SC-9P-006 when a whole `AUevent` is queued | Known issue 5 |
| §4.1.2 `admitted` | — | cites SC-RT-001/019; zeros after demotion | AU-EVENT-006 |
| AU-LIB-003, AU-EVENT-005 | "SC's kqueue readiness", "readiness of 9P fids" | SC-WAIT-001/002, SC-9P-001/006/008/009 | Known issue 2 |
| AU-ABI-002 | "SC's refusal reason verbatim" | "`nd_sched_error_detail()` (SC-RT-020) verbatim"; code authoritative | Known issue 3 |
| §4.3 | — | **new AU-ABI-006**: `au_error_refusal()` returns SC's `sc_rt_refusal` (SC-RT-007) after `AU_ERR_ADMISSION` | Known issue 3; SC §4.9 expected the record to be carried |
| §4.4.2 one wait | `EVFILT_MACHPORT` | cites SC-WAIT-001 | Known issue 2 |
| AU-STREAM-004 | constraint = `period_ns × lead_periods` − margin | constraint = `period_ns` − margin, because SC admits only C ≤ D ≤ T (SC-RT-003, test 1), so any lead > 1 would have been refused. Also: the thread has intent `SC_INTENT_AUDIO` (SC-INT-002/003) | **Contradiction fixed** |
| AU-STREAM-005 | "and SC's reason" | cites SC-RT-001/006, AU-ABI-002/006 | Known issues 2, 3 |
| AU-STREAM-007 | 5 steps; no deadline statement | adds step 3: after a **late** wake, `nd_rt_set_deadline(au_host_to_ns(deadline_host))` (SC-RT-022); doorbell receive cites SC-RT-017/021 and SC-WAIT-005 | Known issue 2; late wakes only, so AU-PERF-006 (one system call per period) still holds |
| AU-STREAM-009 | — | informative: SC-OVR-001/003/009 are authoritative | Known issue 2 (demotion) |
| AU-STREAM-010 | "While SC offers no such handle, MUST return `AU_ERR_UNSUPPORTED`" | the handle is the stream thread's `sc_ticket` (SC-RT-009/010/011), not an `AUhandle`; `AU_CAP_RT_GROUP` = `SC_CAP_RT_GROUP` | Known issue 2 (groups) |
| AU-STREAM-016 | "call `nd_rt_admit` again" | re-admits the same ticket (SC-RT-019); a refusal ends the stream with `AU_EV_STREAM_LOST`/`AU_ERR_ADMISSION` | Undefined case |
| §4.6 `rt_*` | as SC granted | 0 while demoted or revoked | AU-EVENT-006 |
| §4.7 | — | **new AU-EVENT-006**: notices via `nd_rt_watch` (SC-OVR-006) reach the session fd and come back as `AU_EV_RT_NOTICE`; demotion or revocation (SC-OVR-003/004) is also `AU_CHG_ADMISSION` | Known issue 2 (demotion notices); SC §4.9 expected it; keeps AU-LIB-003's "exactly when" true |
| AU-MIX-001 | "through SC's `nd_rt_admit`" | `nd_rt_admit_ex` with `SC_RT_F_SYSTEM` (SC-RT-001/004, SC-SEC-002) | Known issue 2 (system share); plain `nd_rt_admit` cannot draw on it |
| AU-MIX-004 | syscalls: doorbells and handshake only | also the late-wake `nd_rt_set_deadline` of AU-MIX-008 | Consistent with SC-RT-022 |
| AU-MIX-006 | re-admit | cites SC-RT-019 | Known issue 2 |
| §4.9.1 | — | **new AU-MIX-007** (mixer notices on a service thread, stats, log) and **AU-MIX-008** (late-wake mixer deadline) | Known issue 2; SC §4.9 described both |
| AU-T2-003 | exceptions: thunk, clock reads | adds the late-wake `nd_rt_set_deadline` | Otherwise AU-STREAM-007 would contradict it |
| §4.14 | S1–S8 "proposals", to be reconciled | adds a column with the SC ids that specify each | Known issue 2 |
| §6 | — | real-time service needs no audio capability (SC-SEC-001); `audiod` holds `sched.rt.system` (SC-SEC-002) | Known issue 6 |
| §7 AU-PERF-006, 007 | — | 006 notes the late-wake call; 007 cites SC-PERF-002 | Consistency |
| §8 | — | AU-T-004, 014, 027, 029, 031, 035, 050, 087 extended; **new AU-T-089** | Every new MUST is covered (`conformance_test.sh` passes: 140 ids, 122 with MUST, 89 tests) |
| §10 | 1 open | 1 **Resolved**; new 11 (re-admission after demotion), 12 (namespace and tokens), 13 (9P error spelling differs from WP) | Known issues 3, 6, 7 |
| `nd_audio.h` | `extern "C"` present, C++ unchecked | comment on C++20 and on the SC clock; `AU_EV_RT_NOTICE = 13`; `struct sc_rt_refusal;` + `au_error_refusal()`; comments on `AU_CHG_ADMISSION`, `au_stream_rt_group`, `au_error_detail` | Known issues 3, 4 |
| `nd_audio_cxx.cpp`, `BUILD.bazel` | none | C++20 check (`-Wall -Wextra -Werror -pedantic`) through plain `cc_library` `nd_audio_cxx`, plus `lang_audit_test` `nd_audio_cxx_lang_audit`, as in desktop and kernel | Known issue 4 |

## 2. Cross-reference map: what WP and AU rely on in SC

| Upper requirement | SC ids relied on |
|---|---|
| WP §2 deadline clock → WP-EV-027, WP-EXT-006, WP-EXT-007, WP-FRAME-007 to 011, WP-PAINT-017, `Deskpadstate.nsec` | SC-TIME-001, SC-TIME-002, SC-TIME-003 |
| WP-EV-013 (readability) | SC-WAIT-001, SC-9P-006, SC-9P-008, SC-9P-009, SC-USER-001, SC-USER-002, SC-TIMER-001, SC-LIB-001 |
| WP-EV-014 (a read after readiness does not block) | SC-9P-010 |
| WP-EV-016 (single outstanding read) | SC-9P-006 (`Tready` is not a read); SC-9P-018 (only the fallback parks a read) |
| WP-EV-021 (format switch) | SC-9P-006 (`Tready` leaves the format alone) |
| WP-EV-004, WP-EV-040 (coalescing) | SC-9P-006 (`Tready` does not stop coalescing) |
| WP-EV-041 (length 0) | SC-9P-001 |
| WP §1, §4.9 reference loop (how to wait) | SC-WAIT-001, SC-USER-001 to SC-USER-004, SC-TIMER-001, SC-TIMER-003, SC-LIB-001, SC-INT-002 |
| WP-PERF-004 (pacing, WP-T-134) | SC-PERF-005 (same S7 criterion, SC-T-024) |
| WP-PERF-005 (input latency) | SC-PERF-004 (SC-T-023) |
| AU §2 host time → AU-CONTRACT-002, AU-CONTRACT-003, AU-VOICE-004, `*_host` fields | SC-TIME-001, SC-TIME-002, SC-TIME-003 |
| AU-NS-009 | SC-9P-001, SC-9P-006 |
| AU-LIB-003 | SC-WAIT-001, SC-9P-006, SC-9P-008, SC-9P-009 |
| AU-EVENT-005 | SC-WAIT-001, SC-WAIT-002, SC-9P-001, SC-9P-006, SC-9P-008 |
| AU-EVENT-006 (notices, demotion) | SC-OVR-003, SC-OVR-004, SC-OVR-005, SC-OVR-006 |
| AU-ABI-002 (detail string) | SC-RT-020 |
| AU-ABI-006 (refusal record) | SC-RT-007 |
| AU-STREAM-004 (admission) | SC-INT-002, SC-INT-003, SC-RT-001, SC-RT-003, SC-RT-022 |
| AU-STREAM-005 (refusal) | SC-RT-001, SC-RT-006 |
| AU-STREAM-007 (doorbell loop, deadline) | SC-RT-017, SC-RT-021, SC-RT-022, SC-WAIT-005 |
| AU-STREAM-009 (overbudget) | SC-OVR-001, SC-OVR-003, SC-OVR-009 |
| AU-STREAM-010 (groups) | SC-RT-009, SC-RT-010, SC-RT-011, SC-CAP-001 (`SC_CAP_RT_GROUP`) |
| AU-STREAM-016 (transition) | SC-RT-019 |
| AU-MIX-001 (system share) | SC-RT-001, SC-RT-004, SC-SEC-002 |
| AU-MIX-006 (re-admission) | SC-RT-019 |
| AU-MIX-007 (mixer notices) | SC-OVR-003, SC-OVR-004, SC-OVR-006 |
| AU-MIX-008, AU-MIX-004, AU-T2-003, AU-PERF-006 (late-wake deadline) | SC-RT-022 |
| AU-PERF-007 (wake latency) | SC-PERF-002 |
| AU §4.4.2 (doorbell in the one wait) | SC-WAIT-001 |
| AU §6 (no audio capability for real time) | SC-SEC-001, SC-SEC-002, SC-RT-004 |
| AU §4.14 S1–S8 | the table in AU §4.14 (= SC §4.9) |

Every SC id cited in WP and AU was checked mechanically against the ids SC defines. So was every WP and AU id that SC cites.

## 3. General scan (known issue 7)

- **Timestamp units.** All three use ns for durations and interface times. AU's `*_host` fields are SC-clock ticks, which SC-TIME-002 allows. WP's `Deskconfig.refresh` and `Deskoutput.refresh` are u32 ns, which is enough for a 1 Hz throttled interval. Consistent.
- **Handles.** SC's `sc_ticket` is index in bits 0–31 and generation in bits 32–63. `AUhandle` is kind in 56–63, generation in 32–55 and index in 0–31. Both are u64 index plus generation with accessor macros (conventions §5), but the layouts differ. `au_stream_rt_group` returns a raw `sc_ticket`, which AU-STREAM-010 and the header now say explicitly. WP's window ids are u32 wire identifiers, not C-API handles; gamepads are slot plus generation. This is not a contradiction; see §4.
- **Errors.** SC has `enum sc_error` with `nd_sched_last_error()` and `nd_sched_error_detail()`. AU has `AUerror` with `au_error_detail()`. WP uses the 9P string `code: detail` with `Deskerr`. All meet conventions §5. The 9P error spellings differ between WP and AU (AU open issue 13).
- **Version macros.** The macros are `SC_API_VERSION 1u`, `AU_API_VERSION 1` and `DESK_API_VERSION 2`. The alias `WP_API_VERSION` was added. Capability queries: `nd_sched_capabilities`, `au_query` plus the `caps` file, and the WP `caps` file.
- **Performance.** WP-PERF-004 and SC-PERF-005 share the S7 game-loop criterion (1 ms). WP-PERF-005 now contains SC-PERF-004. AU-PERF-007 sits beside SC-PERF-002 (both 100 µs p99). No contradictions remain.

## 4. Not resolved here

1. **Multi-open streams** (SC open issue 9, WP open issue 5). A second open of one window stream can have a stale readiness estimate, so an `O_NONBLOCK` read can then park. This needs a server or nd9p rule (P5-02).
2. **Re-admission after demotion** (AU open issue 11). Re-admission is a system call that the T2 paths forbid, so when to re-admit is open.
3. **Namespace vocabulary.** These are not in namespaces-agents.md:
   - `/n/sys/audio`, `/n/sys/sched`;
   - `cap:audio:*`, `cap:sched:admin`, `cap:wsys:input:background`;
   - the entitlement `sched.rt.system`.

   Its §7 names `/n/wsys/wins`. Recorded as AU 12, SC 10 and WP 8 and 13.
4. **9P error-string spelling** differs between WP and AU (AU open issue 13).
5. **Handle bit layouts** differ between `sc_ticket` and `AUhandle`. Both conform. Unifying them would change a published layout, so they were left as they are.
