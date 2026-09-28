<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Kernel scheduling contract

| | |
|---|---|
| **Status** | draft |
| **Version** | 1 |
| **Epic** | P4-16 |
| **Evidence** | study repository `../NeoDarwin-api-study` at `77e9b08`: `friction/F-201-main-thread-and-loop-ownership.md`, `friction/F-203-precise-sleep-and-timer-resolution.md`, `friction/F-204-thread-intent-and-heterogeneous-cores.md`, `friction/F-215-realtime-audio-thread.md`, `friction/F-217-async-asset-io.md`, `reports/heritage.md` §4, `reports/s7-prototypes.md`, `prototypes/ndtk/SHIM-NOTES.md` (W8, W11, "Idle and one wait"), `prototypes/ndtk/Sources/NDTK/Time.swift:62-110`, `prototypes/ndtk/Sources/ndtk-selftest/main.swift`. XNU at `xnu-12377.1.9` (`f6217f8`). 9front at `e4873f075` (`sys/src/9/port/edf.c`). |
| **Header** | [`nd_sched.h`](nd_sched.h), checked as C23 by [`nd_sched_layout.c`](nd_sched_layout.c) and as C++20 by [`nd_sched_cxx.cpp`](nd_sched_cxx.cpp) (`bazel test //docs/kernel/...`) |
| **Supersedes** | none |

This document fixes what a NeoDarwin thread can rely on when it waits, sleeps until a deadline, states what its work is, and asks for real-time service. There are three parts:
- **The one wait.** A single `kevent` wait covers 9P-served files, a coalesced cross-thread wake and absolute-deadline timers, and the precision of those timers depends on the thread's intent. No timer-resolution setting exists.
- **Thread intent.** Four intents map onto XNU QoS, core placement and timer policy.
- **Real-time admission.** An admission test is modelled on Plan 9's EDF `admit` and enforced through XNU's time-constraint policy.

It deliberately leaves open:
- the internal design of `nd9p` (P5-02), beyond the observable readiness semantics;
- the record format of any 9P-served file: the window protocol owns `events`;
- the audio service's own periods and budgets (P4-18);
- placement on x86 hybrid parts (Phase 6a).

## Published names (for P4-18 and P4-12)

The audio service and toolkit specifications, written at the same time as this one, cite these names. They are fixed at version 1:

| Area | C names (`nd_sched.h`) |
|---|---|
| Wait | `nd_wait`, `nd_sched_now_ns`, `SC_DEADLINE_POLL`, `SC_DEADLINE_NONE`, `SC_WAKE_IDENT`, `SC_DEADLINE_IDENT`, `SC_TIMER_FFLAGS` |
| Intent | `nd_thread_set_intent`, `nd_thread_get_intent`, `nd_thread_intent_info`, `SC_INTENT_INTERACTIVE`, `SC_INTENT_THROUGHPUT`, `SC_INTENT_BACKGROUND`, `SC_INTENT_AUDIO` |
| Admission | `nd_rt_admit(period_ns, computation_ns, constraint_ns, refusal)` (admits and joins the calling thread; re-admits when already joined), `nd_rt_admit_ex`, `nd_rt_join`, `nd_rt_leave`, `nd_rt_release`, `nd_rt_status`, `nd_rt_set_deadline`, `nd_rt_watch`, `nd_rt_notice_take`, `SC_RT_F_SYSTEM`, `SC_RT_F_RESERVE_ONLY` |
| Records | `sc_ticket` (u64 handle), `sc_rt_request` (64 B), `sc_rt_refusal` (48 B), `sc_rt_status` (96 B), `sc_rt_notice` (64 B), `sc_intent_info` (56 B) |
| 9P extension | `SC_9P_VERSION` (`"9P2000.L.nd1"`), `SC_9P_TREADY` (160), `SC_9P_RREADY` (161), `SC_9P_READY_READ`, `SC_9P_READY_WRITE`, `SC_9P_READY_END` |
| Clock | the SC clock = `mach_absolute_time` in ns (§4.0, SC-TIME-001 to SC-TIME-003) |
| Common | `nd_sched_api_version`, `nd_sched_capabilities`, `nd_sched_last_error`, `nd_sched_error_detail`, `enum sc_error`, `enum sc_cap`, `enum sc_rt_reason` |

**The Swift wrapper** is the `NDSched` module in `libs/libnd`, Bazel target `//libs/libnd:NDSched`, built with `nd_swift_library`:
- `Deadline`, `Duration` and `wait(until:leeway:)` over `nd_wait`, which is a T2 path;
- `ThreadIntent` and `Thread.setIntent(_:)`;
- a `~Copyable` `RealtimeTicket` whose `deinit` calls `nd_rt_release`, with `admit(period:computation:constraint:) throws(RealtimeRefusal)`.

The toolkit's `Loop` (P4-12) and the audio library (P4-18) build on `NDSched`. They do not call the C functions directly.

## 1. Scope and non-goals

**In scope:**
- the kernel and libnd behaviour of `kevent` for nd9p files, `EVFILT_USER` and `EVFILT_TIMER`;
- the timer slop policy;
- thread intent and its mapping to XNU QoS, placement and timers;
- real-time admission, enforcement, overrun handling and notices;
- the `/n/sys/sched` files;
- the performance targets and their tests.

**Non-goals:**
- **A new wait primitive.** `kevent` is the one wait. `nd_wait` is a convenience over it (§4.5).
- **An affinity, pinning or priority API for applications** (F-204).
- **A timer-resolution setting of any scope** (P10, F-203).
- **The asynchronous I/O queue of F-217.** Its completions will be one more kqueue source, and the batched queue itself is not yet scoped into an epic (friction-register.md, "By owning layer"). This contract only requires that the queue's completion source be waitable in the one wait (SC-WAIT-001).
- **Mixing, audio periods and device clocks.** These belong to P4-18.
- **Frame-clock semantics.** These belong to window protocol revision 2 (P4-17).

## 2. Terms

| Term | Meaning |
|---|---|
| **SC clock** | `mach_absolute_time()` expressed in nanoseconds (via `mach_timebase_info`). It does not advance while the system sleeps. All deadlines and interface timestamps use it (§4.0). |
| **one wait** | A single `kevent64` (or `kevent`, `kevent_qos`) call on one kqueue that returns on any registered source. |
| **stream file** | An nd9p regular file whose length, as reported by the server when it is opened, is 0. Plan 9 synthetic files (`events`, `log`, `ctl`, `state`) report 0; the window protocol and the audio service require it of their `events` files (WP-EV-041, AU-NS-009). Every other nd9p regular file is a **sized file**. |
| **readiness request** | `Tready`/`Rready`, NeoDarwin's 9P2000.L extension that reports a fid readable or writable without consuming data or parking a read (§4.2.2). |
| **read-ahead** | The fallback for peers without the readiness request: one `Tread` kept outstanding on an open stream file, with its reply buffered in the kernel or in `nsd` (§4.2.3). |
| **slop** | The time the kernel may add to a timer's deadline for coalescing. XNU stores the soft deadline and arms the hardware at deadline + slop. |
| **leeway** | The slop a caller states explicitly (`NOTE_LEEWAY`, `ext[1]`). |
| **wake error** | The SC clock value read by the waiting thread immediately after `kevent` returns, minus the deadline. |
| **intent** | What a thread says its work is: `interactive`, `throughput`, `background` or `audio`. |
| **fallback intent** | The intent a real-time thread runs at when it is not being served as real-time: its own intent if that is `interactive`, `throughput` or `background`, otherwise `interactive`. |
| **RT domain** | The cores that run admitted real-time threads: every core of the performance cluster type on a heterogeneous (AMP) system, or every core on a homogeneous one. *m* is the number of online cores in it. |
| **ticket** | An admitted reservation `(T, C, D)`: a per-process `u64` handle, index plus generation (P5). |
| **T, C, D** | Period (minimum time between releases), computation (CPU time per release on the domain's cores), constraint (relative deadline, C ≤ D ≤ T). |
| **density** δ | C / D. Expressed in ppm of one core in records. |
| **release, job** | A release happens when a joined thread becomes runnable, subject to §4.7.5. The job is the work from a release until every joined thread blocks. |
| **overrun** | The joined threads use C before the job ends. |
| **miss** | A job ends after release + D. |
| **principal** | The holder of a budget: the uid of the admitting process, or `system` for `SC_RT_F_SYSTEM`. |

## 3. Model

**Objects and lifetimes:**

| Object | Created by | Owned by | Ends |
|---|---|---|---|
| kqueue | `kqueue()` | the process (a descriptor) | `close` |
| knote on an nd9p file | `EV_ADD` with `EVFILT_READ`/`EVFILT_WRITE` | the kqueue | `EV_DELETE`, `close` of the file or of the kq |
| readiness request (`Tready`) | nd9p, on readiness interest with a zero estimate (§4.2.2) | the open file description | `Rready`; `Tflush` when interest ends or on `close` |
| read-ahead (fallback) | nd9p or `nsd`, on readiness interest, for peers without the extension (§4.2.3) | the open file description (or the `nsd` fid) | reply consumed; `close` (`Tflush`) |
| wake knote | `EV_ADD` of `EVFILT_USER` with `SC_WAKE_IDENT` | the kqueue | `EV_DELETE` or `close` |
| deadline knote | `nd_wait`, or the caller directly | the kqueue | fires (`NOTE_ABSOLUTE` implies `EV_ONESHOT`), or is re-armed by the next `nd_wait` |
| intent | `nd_thread_set_intent` | the thread | thread exit; a later intent or QoS call |
| ticket | `nd_rt_admit` (which also joins the caller) or `nd_rt_admit_ex` | the process | `nd_rt_release`, `exec`, process exit |
| notice | the kernel, on overrun, miss, demotion or revocation | the ticket's notice queue | `nd_rt_notice_take` |

**Ticket state machine:**

```
                 nd_rt_admit (all tests pass)
   (none) ───────────────────────────────────▶ ADMITTED ──────────────┐
      ▲                                        │     │                │
      │ nd_rt_release, exec, exit               │     │ domain shrinks │ SC_RT_DEMOTE_OVERRUNS
      │ (from any state)                        │     ▼                │ overruns in
      └──────────────────────────── REVOKED ◀───┘   (admin revoke)     │ SC_RT_DEMOTE_WINDOW
                                         ▲                             ▼ releases
                                         └──────────── (none) ◀── DEMOTED
                                              nd_rt_release          (reservation dropped,
                                                                      threads at fallback)
```

**Joined-thread states within ADMITTED.** Per release:

```
  blocked ──wake (≥ last release + T)──▶ RT(deadline = release + D, budget = C)
     ▲                                      │                 │
     │ job ends (all joined threads block)   │ budget used up  │
     └──────────────────────────────────────┘                 ▼
     ▲                                             fallback intent until next release
     └───────────── next release ──────────────────────┘  (one overrun counted)
  wake before last release + T ──▶ fallback intent until last release + T
```

**One wait.** The loop thread owns one kqueue and registers these sources in it:
- the window `events` files (nd9p stream files, made ready through `Tready`);
- its own descriptors;
- `EVFILT_USER` `SC_WAKE_IDENT`;
- the notice ports of its tickets (`EVFILT_MACHPORT`).

Each wait passes a deadline and a leeway: `nd_wait` arms `SC_DEADLINE_IDENT` in the same system call. Nothing else in the process runs a wait loop.

## 4. Interfaces

### 4.0 The SC clock

**Decision: one clock, `mach_absolute_time`.** Every deadline and every timestamp that crosses a NeoDarwin interface uses it:
- this contract's deadlines;
- window-protocol event and frame times;
- audio host times;
- real-time notices.

It is the "deadline clock" of window protocol revision 2.

- `nd_sched_now_ns` MUST return `mach_absolute_time()` converted to nanoseconds with `mach_timebase_info`, and every deadline and time in this contract's calls and records MUST be on that clock [SC-TIME-001]. This covers `nd_wait`, the contract timer, `nd_rt_set_deadline`, real-time deadlines and `first_ns`.
- Other NeoDarwin interfaces MUST also carry absolute deadlines and event, frame and audio timestamps on the SC clock [SC-TIME-002]:
  - either as nanoseconds (fields named `*_ns` or `nsec`);
  - or as `mach_absolute_time` ticks (fields named `*_host`), converted with `mach_timebase_info`.

  A deadline taken from any such record can then be passed to `nd_wait` or `nd_rt_set_deadline` without a clock conversion.
- **Continuous time.** `mach_continuous_time` is not a second interface clock. A component that needs time that counts sleep MUST derive it at the edge [SC-TIME-003]:
  - it converts with the sleep offset o = `mach_continuous_time() − mach_absolute_time()`, which is constant while the system is awake and only grows across sleep;
  - so t<sub>continuous</sub> = t<sub>SC</sub> + o, exact for any time since the last wake;
  - a timer that must count sleep adds `NOTE_MACH_CONTINUOUS_TIME` (SC-TIMER-009).
- Window protocol revision 2 defines its deadline clock as this one (window-protocol.md §2), and the audio service's host time is this clock in ticks (audio-service.md §2). A `wsys` build that still stamps `mach_continuous_time`, as the pilot did, gives SC time + o: it differs from SC time only after the machine has slept, and it does not conform.

**Why `mach_absolute_time`:**
- XNU's real-time deadlines (`sched_prim.c:862-864`), `NOTE_MACHTIME`'s default epoch (`kern_event.c:1479-1535`), thread CPU accounting and audio device clocks already use it. It is the only clock in which a real-time deadline needs no conversion.
- Deadlines on it do not all expire at once on resume. A frame or audio deadline set before sleep is still in the future after wake, instead of being late by the length of the sleep.
- Within an awake period the two clocks differ by a constant, so ordering and intervals of events are identical in both. Only durations across sleep differ, and those are wall-clock questions that no interface in this family asks.
- The S7 shim used continuous time only because CFRunLoopTimer needed a conversion anyway (W11). The choice is not evidence for continuous time.

### 4.1 The one wait

**Types:** none beyond `struct kevent64_s` (`<sys/event.h>`).

**Semantics:**
- One `kevent64` call on one kqueue MUST be able to wait on any combination of these sources [SC-WAIT-001]:
  - readiness of nd9p files (§4.2);
  - every descriptor XNU's kqueue already supports (sockets, pipes, FIFOs, vnodes, devices);
  - `EVFILT_USER` (§4.3);
  - `EVFILT_TIMER` absolute deadlines (§4.4);
  - `EVFILT_MACHPORT`, which includes real-time notices (§4.8);
  - `EVFILT_PROC` and `EVFILT_SIGNAL`;
  - the completion source of any future kernel I/O queue (F-217).
- None of these sources may require the waiting process to run a helper thread, or to make a request to another process, before it becomes waitable [SC-WAIT-002].
- The kernel MUST NOT restrict which thread registers events on a kqueue or waits on it (P2) [SC-WAIT-003].
- The `timeout` argument of `kevent`, `kevent64` and `kevent_qos` MUST get the slop that the waiting thread's intent gives a timer registered without `NOTE_LEEWAY` (§4.4.3) [SC-WAIT-004].
  - Today XNU waits with `assert_wait_deadline`, which uses `TIMEOUT_URGENCY_SYS_NORMAL` and no leeway (`bsd/kern/kern_event.c:8039-8040`, `osfmk/kern/sched_prim.c:1249-1275`).
  - The kernel parameters therefore apply: Δ/8, capped at 1 ms (`osfmk/arm/arm_timer.c:283,288`).
- The same rule MUST apply to every other timeout a user thread gives a blocking call [SC-WAIT-005]:
  - `mach_msg` receive and send timeouts (`MACH_RCV_TIMEOUT`, `MACH_SEND_TIMEOUT`);
  - `semaphore_timedwait`, the psynch condition-variable waits behind `pthread_cond_timedwait`, `nanosleep` and `mach_wait_until`.

  A thread that waits on a Mach port with a timeout (the audio doorbell, P4-18) therefore gets the same precision as one waiting in `nd_wait`.

**Errors:** unchanged from XNU (`EBADF`, `EINTR`, `EINVAL`, and per-event `EV_ERROR`).

**Threading:**
- Registration and waiting are safe from any thread.
- Two threads waiting on the same kqueue each receive distinct events; that is XNU behaviour, and the toolkit does not rely on it.

### 4.2 Readiness of nd9p files

XNU today:
- `vn_kqfilter` accepts `EVFILT_READ`/`EVFILT_WRITE` only on regular files, FIFOs and character devices (`bsd/vfs/vfs_vnops.c:1983-2060`).
- For a regular file, readability is `ui_size − offset` (`vfs_vnops.c:2092-2124`), and under `poll()` every regular file is readable (`:2106-2108`).
- Writability is always 1 (`:2132-2150`).

So on today's XNU a 9P `events` file with length 0 is never readable, and an nd9p client cannot take part in the one wait. The filesystem hooks that exist do not close this gap:
- `VNOP_MONITOR` (`bsd/sys/vnode_if.h:1683-1705`) serves only `EVFILT_VNODE`-style notifications;
- `VNOP_KQFILT_ADD` is defined (`bsd/vfs/kpi_vfs.c:6385`), but `vn_kqfilter` never calls it.

9P itself has no readiness message: its only way to learn that data exists is a read that the server holds until data arrives (a "parked read").

**What NeoDarwin adds:**
- **Primary mechanism.** A readiness request, `Tready`/`Rready`, added to NeoDarwin's 9P2000.L profile. It reports readiness without consuming data and without parking a read (§4.2.2).
  - nd9p uses it on every `/n` mount.
  - `nsd` forwards it to servers that implement it: every `libns` server, which includes wsys, audiod, `/n/agent` and `/n/sys`.
  - `nsd` emulates it for servers that do not.
- **Fallback.** A single read-ahead, used only where the extension is absent (§4.2.3).

#### 4.2.1 Classification and per-open state

- When a regular file is opened, nd9p MUST classify it as a stream file or a sized file (§2), from the length the server reports at that open [SC-9P-001].
- For a sized file, `EVFILT_READ`, `EVFILT_WRITE`, `poll` and `select` MUST behave as XNU's vnode filter does for regular files (`vfs_vnops.c:2092-2150`) [SC-9P-002].
- nd9p MUST give every `open(2)` of a stream file its own fid (`Twalk` + `Tlopen`), and MUST keep a stream file's readiness state per open file description, not per vnode [SC-9P-003].
  - 9P servers attach stream state to the fid.
  - XNU's `VNOP_READ` carries no open-file context, so nd9p needs the file glob. XNU already keeps a per-open slot for directories (`fg_vn_data`, `bsd/sys/file_internal.h:188`). The mechanism is P5-02's.

#### 4.2.2 The readiness request (primary)

**Negotiation.**
- nd9p and `nsd` MUST offer the version string `9P2000.L.nd1` in `Tversion` [SC-9P-004].
- A peer that answers `9P2000.L.nd1` supports the readiness request.
- A peer that answers `unknown` or anything else is retried with `9P2000.L`, and is treated as not supporting it.

**Messages.** The wire form follows 9P conventions: little-endian, `size[4] type[1] tag[2]` first. The type numbers and mask bits are in `nd_sched.h`. The messages MUST have this form [SC-9P-005]:

```
size[4] Tready (160) tag[2] fid[4] mask[4]
size[4] Rready (161) tag[2] mask[4] count[4]
mask:  SC_9P_READY_READ 0x1 · SC_9P_READY_WRITE 0x2 · SC_9P_READY_END 0x4
```

**Server semantics** [SC-9P-006]:
- A server MUST answer a `Tready` as soon as at least one condition in `mask` holds for that fid, and at once if one holds already [SC-9P-006].
  - `READ` holds when a read on the fid would return without blocking. For wsys `events`, that is when at least one whole record is queued, per WP-EV-013.
  - `WRITE` holds when a write would be accepted without blocking.
  - `END` holds when the stream has ended, or the file has reached end of file.
- `Rready.mask` gives the conditions that hold.
- `Rready.count` gives the number of bytes a read would return without blocking: whole records, for record streams. It is at least 1 when `READ` holds. A server that cannot know the number reports 1.
- A `Tready` MUST NOT change any state of the file [SC-9P-006]:
  - no data is consumed;
  - it is not a read for WP-EV-016's single-outstanding-read rule;
  - it does not change the stream's format;
  - it does not stop coalescing.
- An `Rlerror` answers a `Tready` on a fid that cannot report readiness.

**Flush.** `Tflush` of a pending `Tready` MUST be answered with `Rflush`, and no `Rready` follows it [SC-9P-007].

**When nd9p asks** [SC-9P-008]:
- nd9p MUST issue a `Tready(fid, READ|END)` on an open stream file when [SC-9P-008]:
  - there is interest in its readability: an `EVFILT_READ` knote, a `poll`/`select` for reading, or a non-blocking read (below);
  - its readable estimate is 0;
  - no `Tready` is outstanding.
- At most one `Tready` MAY be outstanding per open file, and none is issued without interest [SC-9P-008]. Opening a file never asks anything speculatively.

**When the `Rready` arrives** [SC-9P-009]:
- nd9p MUST set the file's readable estimate to `count` [SC-9P-009].
- It MUST activate its `EVFILT_READ` knotes with `data` = `count`, before its transport receive path handles the next reply [SC-9P-009].
- `END` MUST activate them with `EV_EOF` set [SC-9P-009].
- An `Rlerror` MUST activate them with `EV_EOF` set and `fflags` = the error number, as XNU does for sockets, and the next `read(2)` returns that error [SC-9P-009].

**Relationship to a blocking read** [SC-9P-010]:
- `read(2)` MUST issue an ordinary `Tread`, exactly as it would without any readiness interest: a blocking read is still the server's parked read [SC-9P-010]. Readiness only tells the caller that the read will not block (WP-EV-014).
- After a read returns *n* bytes, nd9p MUST lower the estimate by *n*, to a minimum of 0 [SC-9P-010].
- When the estimate reaches 0 and interest remains, nd9p MUST issue the next `Tready` before that `read(2)` returns [SC-9P-010].
- While the estimate is above 0, the file MUST stay readable [SC-9P-010].

**Non-blocking reads** [SC-9P-011]:
- With `O_NONBLOCK` and an estimate above 0, `read(2)` MUST issue its `Tread`, which the server answers without blocking [SC-9P-011].
- With an estimate of 0, `read(2)` MUST issue a `Tready` if none is outstanding, and fail with `EAGAIN` [SC-9P-011].

**`poll`/`select`.** On stream files these MUST report readability from the estimate, not from XNU's `EV_POLL` rule for regular files (`vfs_vnops.c:2106-2108`) [SC-9P-012].

**Losing interest.**
- When the last interest goes away, nd9p MUST flush an outstanding `Tready` [SC-9P-013].
- `close` MUST flush any outstanding `Tready` and `Tread` before `Tclunk` [SC-9P-013].

**Writes** [SC-9P-014]:
- `EVFILT_WRITE` on a stream file MUST be reported through `Tready(WRITE)` where the peer supports it, and otherwise MUST be active whenever a `write(2)` of up to `iounit` bytes can be sent without waiting for transport credit [SC-9P-014].
- `data` = `iounit`.
- The write itself completes only when `Rwrite` arrives, because a `ctl` write reports its error in `Rwrite` (window protocol).

**No side effects.** On a peer that supports the readiness request, reporting readiness MUST NOT consume data from the file and MUST NOT leave a `Tread` outstanding that the application did not issue [SC-9P-015].

#### 4.2.3 Fallback: read-ahead

- **`nsd` emulates the extension.** `nsd` MUST offer the readiness request to nd9p for every backend server, including those that lack it, and emulate it for those servers with one read-ahead per fid under SC-9P-018 to SC-9P-021 [SC-9P-016]. nd9p therefore uses the primary mechanism on every `/n` mount.
- **nd9p's own read-ahead.** nd9p MUST use its own read-ahead only on direct mounts whose peer lacks the extension, such as a virtio-9p host share [SC-9P-017].
- **The read-ahead** is one `Tread` on the fid, issued under the same interest conditions as SC-9P-008 [SC-9P-018]:
  - at the open file's offset;
  - with count = `iounit`, or `msize − 24` when `iounit` is 0;
  - at most one at a time.

  Its reply is buffered, and readiness is reported from the buffer (`data` = buffered bytes; `EV_EOF` for count 0 or an error, as in SC-9P-009).
- **Reading from the buffer** [SC-9P-019]:
  - `read(2)` on a file with a read-ahead MUST NOT issue a `Tread` of its own: it returns buffered bytes, or waits for the outstanding read-ahead [SC-9P-019];
  - a short `read` MUST leave the remainder buffered [SC-9P-019];
  - the next read-ahead MUST be issued only once the buffer is empty and interest remains [SC-9P-019].

  So at most one server reply is ever held ahead of the application, and server-side coalescing (WP-EV-004) still applies to everything queued after it.
- **`pread(2)`.** At an offset other than the read-ahead's, `pread` MUST bypass the buffer with its own `Tread` [SC-9P-020].
- **Close.** `close` MUST flush an outstanding read-ahead before `Tclunk`. Buffered bytes, and bytes in a reply that crosses the flush, are discarded, as in Plan 9 [SC-9P-021].
- **Side effects of the fallback.** These are stated so that servers can judge them:
  - one reply leaves the server early;
  - while the application is idle, a `Tread` is parked at the server, so a second reader of the same stream gets `busy` (WP-EV-016);
  - a read-ahead issued before a `format` write returns the old format (WP-EV-021).

  No `libns` server sees any of this, because `nsd` gives them the primary mechanism. Emulation in `nsd` confines these effects to legacy servers.

**Latency:**
- SC-PERF-004 bounds the path from a record being queued at the server to `kevent` returning in the client, for the primary mechanism through `nsd`: p99 ≤ 500 µs in total, of which the server's `Rready` accounts for at most 250 µs and the kernel segment for at most 100 µs.
- The read that follows readiness is one more ordinary round trip. That round trip is not part of this bound.

**Threading:** readers, pollers and closers of the same open file are serialised by nd9p. The estimate and any buffer are shared by every thread using that file description.

**Lifetime:** readiness state belongs to the open file description (so it is shared across `dup` and `fork`) and ends at the last `close`.

### 4.3 Wake

- The cross-thread wake of a loop is an `EVFILT_USER` knote with ident `SC_WAKE_IDENT`, registered with `EV_ADD | EV_CLEAR`. A waker triggers it with `NOTE_TRIGGER`.
- libnd, the toolkit and every library built on this contract MUST use `SC_WAKE_IDENT` for that purpose and no other [SC-USER-001].
- Any number of `NOTE_TRIGGER`s between two waits MUST produce exactly one wake event in the next wait (coalesced) [SC-USER-002]. XNU already does this: `filt_usertouch` sets the knote active and `filt_userprocess` clears it under `EV_CLEAR` (`bsd/kern/kern_event.c:1936-2001`). NeoDarwin keeps it unchanged.
- A wake MUST NOT involve any process other than the waker and the waiter: no server round trip (F-201 item 3) [SC-USER-003].
- Triggering MUST NOT block the caller, so an admitted real-time thread may wake a loop [SC-USER-004]:
  - it is a `kevent64` call with a one-entry changelist, no eventlist and `KEVENT_FLAG_IMMEDIATE`;
  - it takes only the kqueue's spin lock.
- Payload-carrying posts (`post(to:)` in the charter) are a toolkit ring plus this wake. They are not kernel objects.

### 4.4 Timers

#### 4.4.1 What XNU does today

| Behaviour | Where |
|---|---|
| `NOTE_SECONDS/USECONDS/NSECONDS/MACHTIME` choose units (default ms); `NOTE_MACHTIME` means mach absolute-time ticks | `bsd/sys/event.h:304-326`; `bsd/kern/kern_event.c:1438-1458` |
| `NOTE_LEEWAY`: `ext[1]` is the leeway, in the same units | `kern_event.c:1460-1477` |
| `NOTE_ABSOLUTE` with `NOTE_MACHTIME`: an absolute `mach_absolute_time` deadline (`mach_continuous_time` with `NOTE_MACH_CONTINUOUS_TIME`). Without `NOTE_MACHTIME`: a calendar (`gettimeofday`) deadline, converted to an interval **once** at registration | `kern_event.c:1479-1535` (conversion and TODO at `:1497-1533`) |
| `NOTE_ABSOLUTE` forces `EV_ONESHOT`; a touch cannot toggle `NOTE_ABSOLUTE` | `kern_event.c:1740-1743`, `:1793-1797` |
| Urgency: `NOTE_CRITICAL` → `THREAD_CALL_DELAY_USER_CRITICAL`, `NOTE_BACKGROUND` → `…USER_BACKGROUND`, else `…USER_NORMAL` | `kern_event.c:1643-1649` |
| Expiry runs a `THREAD_CALL_PRIORITY_HIGH` thread call (4 worker threads at `BASEPRI_PREEMPT_HIGH`), which activates the knote, which wakes the waiter | `kern_event.c:1726-1728`; `osfmk/kern/thread_call.c:99-104` |
| Slop = `timer_call_slop(deadline, now, urgency, current_thread())`. **An explicit leeway is used only if it is larger**, so leeway 0 does not mean slop 0 | `osfmk/kern/thread_call.c:1300-1305` |
| Slop policy: real-time priority or `USER_CRITICAL` → 0; background → min(32Δ, 100 ms); otherwise by latency-QoS tier. `USER_INTERACTIVE` is tier 0: min(Δ/8, 1 ms). `USER_INITIATED`: min(Δ/4, 5 ms). Timeshare: min(Δ/8, 1 ms). The "user idle level" (0-128) inflates all of it | `osfmk/kern/timer_call.c:1827-1914`, `:1920-1943`; `osfmk/arm/arm_timer.c:276-301`; `osfmk/kern/thread_policy.c:117-123` |
| The delivered event's `ext[0]` is zeroed; a TODO wishes it reported the deadline | `kern_event.c:1851-1861` |

**Consequence.** A 16.7 ms frame deadline registered by a `USER_INTERACTIVE` thread may legally fire up to 1 ms late, and asking for zero leeway does not change that. This matches the S7 host result: a zero-leeway timer fired 0.5–1.7 ms late (`SHIM-NOTES.md`, "Idle and one wait"). CFRunLoop tolerance and the thread-call hop account for the rest.

#### 4.4.2 The contract timer

- **The form.** The contract timer is `EVFILT_TIMER` with:
  - `fflags` = `SC_TIMER_FFLAGS` (`NOTE_ABSOLUTE | NOTE_MACHTIME | NOTE_LEEWAY`);
  - `data` = the deadline in `mach_absolute_time` ticks;
  - `ext[1]` = the leeway in ticks.
- Every kqueue MUST support it [SC-TIMER-001].
- A timer MAY add `NOTE_MACH_CONTINUOUS_TIME` when it must count time asleep [SC-TIMER-009].
- Calendar-epoch absolute timers (`NOTE_ABSOLUTE` without `NOTE_MACHTIME`) keep XNU's meaning. libnd, the toolkit and the audio library MUST NOT use them for deadlines [SC-TIMER-002]. XNU converts them once, so they drift with calendar changes.
- A timer MUST NOT be delivered before its deadline: when the event is returned, the SC clock is at or after the deadline [SC-TIMER-003]. The S7 shim had to accept early fires of up to 20 µs from CFRunLoopTimer; NeoDarwin has none.
- **Fire time.** When `SC_CAP_TIMER_FIRETIME` is present, the `ext[0]` of a delivered `EVFILT_TIMER` `kevent64_s` MUST carry the `mach_absolute_time` at which the kernel found the timer expired [SC-TIMER-004].
  - This is additive: today the field is always 0 (`kern_event.c:1861`).
  - It lets a pacing loop measure its own wake error without a second clock read.

#### 4.4.3 Slop by intent

- **With `NOTE_LEEWAY`:**
  - For a timer registered by a thread whose intent is `interactive`, `audio`, `throughput` or none, the slop MUST be exactly the stated leeway [SC-TIMER-005].
  - For a `background` thread, it MUST be the larger of the leeway and the background default [SC-TIMER-005].
  - This replaces XNU's "larger of leeway and policy" rule (`thread_call.c:1303-1305`) everywhere except background. The caller's statement is the truth in both directions.
- **Without `NOTE_LEEWAY`:** the slop MUST be the default for the registering thread's intent, from this table [SC-TIMER-006]. `NOTE_CRITICAL` still means 0, and `NOTE_BACKGROUND` still means the background default.

| Intent | Default slop without `NOTE_LEEWAY` | XNU mechanism |
|---|---|---|
| `interactive` | **0** | treated as `…USER_CRITICAL` (rt parameters: shift 0, max 0) |
| `audio` (joined or not) | **0** | as above; a joined thread is also at real-time priority, which XNU already maps to 0 |
| `throughput` | min(Δ/4, 5 ms) | XNU's `USER_INITIATED` tier, unchanged |
| `background` | min(32Δ, 100 ms) | XNU's background parameters, unchanged |
| none | XNU's QoS-derived value, except effective QoS `USER_INTERACTIVE`, which gets 0 | this is how libdispatch's main queue and threads that set QoS themselves get interactive precision |

- **Whose intent counts.** The slop MUST be computed from the intent of the thread that registered or last modified the knote (`EV_ADD`), and recorded in the knote [SC-TIMER-007]. XNU computes it from whichever thread arms the call, which for repeating timers is the thread processing the event (`kern_event.c:1917`).
- **Idle states.** While a timer with slop below the exit latency of a CPU idle state is pending on that CPU, the kernel MUST do one of two things [SC-TIMER-008]:
  - choose a shallower idle state; or
  - program the hardware timer early by that exit latency.

  On SBSA systems, PSCI `CPU_SUSPEND` states can take hundreds of microseconds to exit, which alone would break SC-PERF-001.
- The thread-call hop on expiry (`kern_event.c:1726-1728`) MAY remain, provided the SC-PERF targets are met [SC-TIMER-010]. Activating the knote from the timer interrupt is an implementation option, not a requirement.

**Precision guarantee:**
- With leeway 0, `interactive` and `audio` threads get the wake-error bounds of SC-PERF-001 and SC-PERF-002.
- Other intents wake within their slop plus ordinary scheduling delay. There is no numeric bound for them.

#### 4.4.4 No timer-resolution setting

- NeoDarwin MUST NOT offer any interface usable by an application that sets a global, per-process or per-thread timer resolution, tick rate or minimum sleep granularity [SC-RES-001]. This covers calls, sysctls, files, entitlements and boot arguments.
- The existing administrator coalescing tunables MUST NOT change the slop of `interactive` or `audio` timers, or of any timer a non-background thread registers with `NOTE_LEEWAY` [SC-RES-002]. The tunables are:
  - `kern.timer.coalescing_enabled` (`bsd/kern/kern_sysctl.c:3262-3264`);
  - `kern.timer_coalesce_bg_scale` and `kern.timer_coalesce_bg_ns_max` (`:5206-5219`);
  - `machdep.user_idle_level` (`:5325-5345`).

  They remain power policy for background work.
- Compatibility layers that receive foreign resolution calls MUST accept them and do nothing [SC-RES-003]. Examples: Wine's `NtSetTimerResolution` and `timeBeginPeriod`, and Linux `PR_SET_TIMERSLACK` in the Linux shims.

### 4.5 `nd_wait` (libnd)

```c
int32_t nd_wait(int32_t kq, uint64_t deadline_ns, uint64_t leeway_ns,
                struct kevent64_s *events, int32_t n);
```

**Decision: provide it.** A caller could do all of this with `kevent64`, but `nd_wait` adds four things `kevent64` does not:
1. **Leeway on the wait's own deadline.** `kevent`'s timeout takes no leeway.
2. **An absolute deadline.** The timeout is relative, so a loop that computes `deadline − now` also loses the time between that subtraction and entering the kernel.
3. **The contract timer, set up correctly.** It always arms `SC_DEADLINE_IDENT` with `NOTE_LEEWAY` set, so the caller's leeway is authoritative, including zero. This is the easy path to the precise one for code that does not use the toolkit: engines on the raw C ABI, and SDL3's NeoDarwin backend.
4. **No extra system call, and no internal event returned.**

It adds no state and no thread; a caller that prefers raw `kevent64` loses nothing but convenience.

**Semantics:**
- `nd_wait` MUST do its work in a single `kevent64` system call [SC-LIB-001]:
  - the changelist arms `SC_DEADLINE_IDENT` on `EVFILT_TIMER` with `SC_TIMER_FFLAGS`, `data` = the deadline and `ext[1]` = the leeway, both converted to ticks;
  - the eventlist receives up to *n* events.
- The leeway MUST always be passed with `NOTE_LEEWAY`, including a leeway of 0 [SC-LIB-002].
- `nd_wait` MUST NOT use `kevent64`'s timeout argument, except for `SC_DEADLINE_POLL`, which uses `KEVENT_FLAG_IMMEDIATE` [SC-LIB-003].
- The deadline event MUST NOT be returned to the caller [SC-LIB-004]:
  - the return value counts only caller events;
  - it is 0 when only the deadline fired.
- A deadline at or before the SC clock MUST make `nd_wait` return without blocking [SC-LIB-005]. XNU fires an expired absolute timer at attach (`kern_event.c:1745-1747`).
- `SC_DEADLINE_NONE` MUST disable the deadline knote (`EV_ADD | EV_DISABLE`) in the same system call and wait for caller events only [SC-LIB-006].
- `nd_wait` MUST NOT allocate memory, take user-space locks, or keep per-kqueue state in user space [SC-LIB-007]. It is on the T2 path.

**Errors** [SC-LIB-008]:
- `nd_wait` MUST return −1 and set `nd_sched_last_error()` [SC-LIB-008]:
  - `SC_E_BADF` when `kq` is not a kqueue;
  - `SC_E_INVAL` when *n* ≤ 0 or `events` is null;
  - `SC_E_INTR` on `EINTR`.
- It MUST NOT retry after `EINTR`, so signals stay observable [SC-LIB-008].
- A per-event `EV_ERROR` on a caller's own registration is passed through as an event.

**Threading:**
- `nd_wait` is safe from any thread.
- Two threads calling `nd_wait` on one kqueue with different deadlines race on the one deadline knote, and the later call wins. One loop is owned by one thread at a time (charter §4).

### 4.6 Thread intent

```c
enum sc_error nd_thread_set_intent(uint32_t intent);
uint32_t      nd_thread_get_intent(void);
enum sc_error nd_thread_intent_info(sc_intent_info *out);
```

**Semantics:**
- `nd_thread_set_intent` applies to the calling thread only, and MUST take effect before it returns: QoS, placement preference and the slop of later timer registrations [SC-INT-001].
  - There is no call that sets another thread's intent. An intent is a thread's statement about its own work, and a cross-thread setter would race with that thread's own statements.
- The kernel MUST apply this mapping [SC-INT-002]:

| Intent | XNU QoS (`thread_policy.c:78-123`) | Scheduling mode | Timer default (§4.4.3) | Placement (§4.6.1) | I/O tier |
|---|---|---|---|---|---|
| `interactive` | `THREAD_QOS_USER_INTERACTIVE`, relprio 0 | timeshare | 0 | P preferred, spills to E only when every P core is busy | 0 |
| `throughput` | `THREAD_QOS_USER_INITIATED` | timeshare | min(Δ/4, 5 ms) | a process's `throughput` threads on one core type at a time | 0 |
| `background` | `THREAD_QOS_BACKGROUND` (DARWIN_BG) | timeshare, throttled | min(32Δ, 100 ms) | E preferred; never displaces `interactive` on P | 2 |
| `audio` | `THREAD_QOS_USER_INTERACTIVE` | real-time **only** while joined to an admitted ticket, otherwise as `interactive` | 0 | RT domain when joined, otherwise as `interactive` | 0 |

- `SC_INTENT_AUDIO` on its own MUST NOT grant real-time scheduling. Only a ticket does (§4.7) [SC-INT-003].
- Setting an intent MUST replace the thread's requested QoS, as `pthread_set_qos_class_self_np` would [SC-INT-004]:
  - a later QoS call by the thread resets its intent to `SC_INTENT_NONE`, so the last writer wins;
  - XNU's QoS overrides (priority-inversion avoidance, dispatch overrides) still apply.
- For a thread joined to a ticket, `nd_thread_set_intent` MUST change only its fallback intent. It stays real-time until it leaves [SC-INT-005].
- An unknown intent value MUST return `SC_E_INVAL` and leave the thread unchanged [SC-INT-006].
- A new thread MUST start with `SC_INTENT_NONE`: intent is not inherited [SC-INT-007].
- Intent MUST NOT raise a thread above the clamps of its task [SC-INT-008]:
  - the task QoS clamp;
  - role-based ceilings;
  - the `DARWIN_BG` state of a background task.

  The effective values are reported in `sc_intent_info`.

**Records:**
- `sc_intent_info` (56 bytes) reports:
  - the intent;
  - the QoS actually applied;
  - the preferred cluster;
  - the default slop for this thread's timers;
  - the joined ticket and its state.
- The caller sets `size`.

**Errors:** `SC_E_INVAL` for a bad value or record size. Intent calls cannot fail for lack of authority. They are clamped instead (SC-INT-008).

**Window state.** The toolkit lowers a render thread to `background` when wsys reports its window hidden, and raises it again on show (F-204 item 4). That is a toolkit rule (P4-12); this contract only makes the call cheap and allowed at any time.

#### 4.6.1 Placement

- **No affinity for applications.** No application-visible interface binds threads to CPUs, clusters or processor sets [SC-PLACE-001]:
  - `THREAD_AFFINITY_POLICY` MUST return `KERN_NOT_SUPPORTED` (the path already exists: `osfmk/kern/thread_policy.c:484-497`) [SC-PLACE-001];
  - NeoDarwin MUST NOT add a replacement [SC-PLACE-001];
  - processor-set control stays behind the `host_priv` port.
- **Edge on heterogeneous systems.** On a system with more than one core type, the kernel MUST run with the Edge scheduler [SC-PLACE-002]:
  - Edge is `CONFIG_SCHED_EDGE`, which requires `__AMP__` (`osfmk/kern/sched.h:195-207`, `osfmk/arm/smp.h:34`);
  - the cluster types come from firmware (the `cluster-type` property parsed at `osfmk/arm64/machine_routines.c:1209`, filled on SBSA from ACPI PPTT and core capacity; `arm64-sbsa-bringup.md` §2.2);
  - today's SBSA board configuration (`kernel/patches/0002-sbsa-board-config.patch`) defines no `__ARM_AMP__`, so it runs Clutch only. That is correct for homogeneous boards and must become conditional.
- **An open performance controller.** NeoDarwin MUST provide one, registered through `sched_perfcontrol_register_callbacks` (`osfmk/arm/machine_routines_common.c:178`) [SC-PLACE-003]:
  - it sets per-thread-group, per-bucket preferred clusters from intent, through `sched_perfcontrol_thread_group_preferred_clusters_set` (`osfmk/kern/thread_group.c:1463-1474`);
  - without it, every clutch bucket group prefers `pset0` (`osfmk/kern/sched_clutch.c:1408`), because on Apple hardware the closed CLPC supplies these recommendations;
  - the controller lives in `kernel/neodarwin/` as `ndperf`.
- The placement column of the §4.6 table MUST hold whenever `SC_CAP_PLACEMENT` is reported [SC-PLACE-004]:
  - for `throughput`, the controller keeps all `throughput` threads of one process on one cluster type at a time, and moves them together (the llama.cpp lockstep case, F-204);
  - on homogeneous systems `SC_CAP_PLACEMENT` is clear and `cluster` reads `SC_CLUSTER_ANY`.
- **Topology as data.** The kernel MUST publish its topology as read-only text in `/n/sys/sched/cpu`, one line per CPU [SC-PLACE-005]:

  ```
  cpu 0 cluster 0 type P capacity 1024 online 1
  ```

  Tools never probe by migrating threads (F-204 item 3).
- The RT domain MUST be published in `/n/sys/sched/rt/domain` as `cores <m> type <P|E|any> list <cpu,…>` [SC-PLACE-006].

### 4.7 Real-time admission

```c
sc_ticket     nd_rt_admit(uint64_t period_ns, uint64_t computation_ns,
                          uint64_t constraint_ns, sc_rt_refusal *refusal);
enum sc_error nd_rt_admit_ex(const sc_rt_request *req, sc_ticket *out, sc_rt_refusal *refusal);
enum sc_error nd_rt_join(sc_ticket ticket);
enum sc_error nd_rt_leave(void);
enum sc_error nd_rt_release(sc_ticket ticket);
enum sc_error nd_rt_status(sc_ticket ticket, sc_rt_status *out);
enum sc_error nd_rt_set_deadline(uint64_t deadline_ns);
const char   *nd_sched_error_detail(void);   /* thread-local reason */
```

**What XNU provides today:**
- **The policy.** `THREAD_TIME_CONSTRAINT_POLICY` takes period, computation and constraint (`thread_policy.c:391-445`). It checks only three things:
  - C ≤ D;
  - 50 µs ≤ C ≤ 50 ms (`osfmk/kern/sched_rt.c:103-112`);
  - it then raises C to D/2 when C is smaller (`thread_policy.c:415-420`).

  There is **no admission test**: any thread can make itself real-time.
- **Dispatch.** Real-time dispatch is deadline-ordered:
  - the deadline is wake time + constraint (`osfmk/kern/sched_prim.c:862-864`);
  - the per-cluster RT run queues compare deadlines with a 100 µs epsilon (`sched_rt.c:68-71`).
- **Quantum.** The quantum is C (`sched_prim.c:3932-3935`). When it expires, the deadline becomes `RT_DEADLINE_QUANTUM_EXPIRED` and the thread **stays in the real-time band** (`sched_prim.c:3480-3488`).
- **Runaway protection.** The only protection is the fail-safe:
  - it demotes a thread that ran 100 quanta without blocking, for twice that long (`osfmk/kern/priority.c:158-194`, `sched_prim.c:163, 518-525`);
  - it reports this in a log line (`sched_prim.c:6815-6875`).
- **An optional allow-list.** The "RT allow" policy demotes real-time threads that have not joined a work interval with `WORK_INTERVAL_WORKLOAD_ID_RT_ALLOWED`:
  - `thread_policy.c:3380-3500` and `bsd/sys/work_interval.h:307`;
  - the tunable is `-rt-allow_policy-enable`, off except on XR;
  - it adds a crude CPU limit (70% of 10 ms).
- **Helper threads.** Helpers join a deadline through work intervals (`os_workgroup`).

The S7 shim's audio thread asked for T = D = 2.667 ms and C = 0.5 ms (`Time.swift:89`). XNU silently raised C to 1.33 ms.

#### 4.7.1 Admission

- **Self-admission.** `nd_rt_admit(T, C, D, refusal)` MUST behave exactly as `nd_rt_admit_ex` with `flags` 0 and `udata` 0, which admits the request and joins the calling thread to the new ticket as one atomic step [SC-RT-001]:
  - it returns the ticket;
  - on refusal it returns `SC_TICKET_NONE`, sets `nd_sched_last_error()` to `SC_E_REFUSED`, fills `*refusal` if it is not null, and sets `nd_sched_error_detail()` (SC-RT-020) [SC-RT-001];
  - a `constraint_ns` of 0 means D = T.
- With `SC_RT_F_RESERVE_ONLY`, `nd_rt_admit_ex` MUST reserve without joining any thread [SC-RT-002]. Apart from joining the caller under SC-RT-001, admission MUST NOT change the scheduling of any thread: a ticket takes effect only for the threads that join it [SC-RT-002].
- **Re-admission.** When the calling thread is already joined to a ticket, `nd_rt_admit` (and `nd_rt_admit_ex` without `SC_RT_F_RESERVE_ONLY`) MUST re-admit that ticket instead of creating a new one [SC-RT-019]:
  - the tests below run with that ticket's current reservation excluded;
  - on success the same handle carries the new T, C and D, for every joined thread, from the next release;
  - on refusal the old reservation and parameters stay exactly as they were.

  This is how a stream changes its period after a device change (P4-18) without a window in which it holds no reservation.
- **Reason string.** After any failed call, `nd_sched_error_detail()` MUST return a non-empty, human-readable reason on the calling thread [SC-RT-020]. For a refusal it names the failed test and its numbers, in the manner of Plan 9's `edfadmit` strings, for example `not schedulable: domain density 2.81 + 0.25 > bound 3.00`.
  - The code (`enum sc_error`) and the refusal record are authoritative; the string is for people and logs.
  - This follows spec-conventions.md §5 (codes plus thread-local detail) and satisfies the audio specification's request for a reason string.
- The kernel MUST apply these tests in this order. The first test that fails gives the refusal reason [SC-RT-003]:

| # | Test | Reason on failure |
|---|---|---|
| 1 | T > 0, C > 0, C ≤ D ≤ T | `SC_RT_R_PARAMS` |
| 2 | `SC_RT_MIN_COMPUTATION_NS` (50 µs) ≤ C ≤ `SC_RT_MAX_COMPUTATION_NS` (50 ms), XNU's quantum bounds | `SC_RT_R_COMPUTATION` |
| 3 | T ≤ `SC_RT_MAX_PERIOD_NS` (1 s); larger values would not fit XNU's 32-bit tick fields on a 1 GHz counter for long | `SC_RT_R_PERIOD` |
| 4 | `SC_RT_F_SYSTEM` only with the `sched.rt.system` entitlement (§6) | `SC_RT_R_PERMISSION` |
| 5 | fewer than `SC_RT_TICKETS_PER_PROCESS` (16) tickets held by the process | `SC_RT_R_LIMIT` |
| 6 | an RT domain with m ≥ 1 online cores exists | `SC_RT_R_NO_DOMAIN` |
| 7 | δ = C/D ≤ `SC_RT_TICKET_DENSITY_PPM` (0.75) | `SC_RT_R_DENSITY` |
| 8 | the principal's admitted density + δ ≤ its budget (below) | `SC_RT_R_BUDGET` |
| 9 | **domain test**, over every admitted ticket in the domain plus this one: Σδ ≤ m − (m − 1)·δ<sub>max</sub> **and** Σδ ≤ ρ·m, where ρ = `SC_RT_DOMAIN_CAP_PPM` (0.75) | `SC_RT_R_UNSCHEDULABLE` |

- **Budgets** [SC-RT-004]:
  - the `system` principal MUST always be able to admit up to `SC_RT_SYSTEM_SHARE_PPM` (0.25 core), whatever users hold [SC-RT-004];
  - each uid's budget defaults to ρ·m − 0.25 cores.
- Admission within budget needs no daemon, D-Bus-style broker or entitlement (F-215) (§6).
- **Atomicity.** The tests and the reservation MUST be atomic with respect to every other admission, release, demotion and revocation in the domain, so two concurrent requests cannot both pass against the same headroom [SC-RT-005].
- **Refusal leaves no trace.** A refused request MUST leave no reservation and MUST NOT change any thread [SC-RT-006].
- **The refusal record** MUST be filled [SC-RT-007]:
  - the reason;
  - `max_computation_ns`: the largest C that would pass tests 7–9 now, for the same T and D, or 0 if none would;
  - `available_ppm`, the headroom left by test 9;
  - `budget_ppm`, the headroom left by test 8.

  The audio service uses `max_computation_ns` to choose a longer period rather than guess.
- The test is O(number of admitted tickets in the domain). ρ and the budgets live in `/n/sys/sched/rt/policy` (§6).

**Why this test.**
- Plan 9's `edfadmit` (`sys/src/9/port/edf.c:331-360`) checks T, C and D and then runs an exact processor-demand simulation (`:581-637`) on one processor. It can give up with "probably not schedulable".
- NeoDarwin dispatches real-time threads by global EDF across the cores of a domain. For constrained deadlines (D ≤ T) on *m* identical cores, the density bound Σδ ≤ m − (m − 1)·δ<sub>max</sub> is a sufficient test for global EDF. It is the Goossens–Funk–Baruah bound, extended to density.
- It is O(n), needs no simulation, and reduces to Plan 9's uniprocessor density condition when m = 1.
- The cap ρ = 0.75 keeps at least a quarter of the domain for `interactive` threads, because on AMP systems the domain is the P cluster where the UI also runs.

**Worked example.**
- The S7 synth's stream asks for T = D = 2.667 ms and C = 0.5 ms, so δ = 0.1875.
- On a four-core domain the test admits up to 16 such tickets.
- The system share admits the mixer first.

#### 4.7.2 Joining, leaving and releasing

- **Joining.** `nd_rt_join(t)` MUST make the calling thread a real-time thread (`TH_MODE_REALTIME`) with the ticket's T, C and D [SC-RT-008]. XNU raises C to D/2 (`thread_policy.c:415-420`); that inflation MUST NOT be applied to joined threads, because the admission test counted the declared C.
- **Budget is shared.** All threads joined to one ticket share its budget. The CPU time of every joined thread counts against the same C per release, as in the `os_workgroup` model [SC-RT-009]. This is how DSP helper threads join a stream's deadline (F-215, JUCE `AudioWorkgroup`).
- **Joining errors.** A thread may be joined to at most one ticket [SC-RT-010]:
  - `nd_rt_join` while joined MUST return `SC_E_BUSY` (to change parameters, the thread re-admits instead, SC-RT-019) [SC-RT-010];
  - joining a ticket that is not `ADMITTED` MUST return `SC_E_REFUSED` [SC-RT-010].
- **Handles are per process** [SC-RT-011]:
  - a ticket from another process, or with a stale generation, MUST return `SC_E_HANDLE` [SC-RT-011];
  - tickets MUST NOT be inherited across `fork` [SC-RT-011];
  - they MUST be released at `exec` and at process exit [SC-RT-011].
- **Leaving.** `nd_rt_leave()` MUST return the calling thread to its fallback intent before it returns. The reservation stays [SC-RT-012].
- **Thread exit.** A thread that exits leaves its ticket. The ticket stays admitted with no threads until it is released.
- **Releasing.** `nd_rt_release(t)` MUST do all of the following before it returns [SC-RT-013]:
  - return every joined thread to its fallback intent;
  - drop the reservation;
  - end the notice queue;
  - retire the ticket's generation.

  Later calls with the ticket return `SC_E_HANDLE`.
- **Status.** `nd_rt_status` MUST report the ticket's state, parameters and counters (`sc_rt_status`) [SC-RT-014]. It works in every state until the ticket is released.

#### 4.7.3 Only admitted threads are real-time

- **The allow-list.** NeoDarwin MUST enable XNU's RT-allow policy (`rt_allow_policy_enabled`, `thread_policy.c:3386-3398`) and implement tickets as work intervals carrying `WORK_INTERVAL_WORKLOAD_ID_RT_ALLOWED` [SC-RT-015]:
  - a user thread in real-time mode that has not joined a ticket is then demoted by the existing path (`TH_SFLAG_RT_DISALLOWED`, `thread_policy.c:3484-3491`);
  - the RT-allow CPU limit (`thread_rt_set_cpulimit`, 70% of 10 ms) MUST NOT be applied to ticket threads, whose budget is enforced per release (§4.8) [SC-RT-015].
- **Compatibility path.** `thread_policy_set` with `THREAD_TIME_CONSTRAINT_POLICY` or `THREAD_TIME_CONSTRAINT_WITH_PRIORITY_POLICY` from a user thread that has not joined a ticket MUST be treated as `nd_rt_admit` followed by `nd_rt_join` [SC-RT-016]:
  - the period, computation and constraint are converted from ticks;
  - a period of 0 means T = D;
  - on refusal the call MUST return `KERN_RESOURCE_SHORTAGE` and leave the thread's policy unchanged [SC-RT-016];
  - the implicit ticket is released when the thread exits or sets another policy;
  - the ports of zed, SDL, Chromium and the S7 shim (`Time.swift:97-110`) therefore get admission without code changes, and see a failure instead of a silent success.

#### 4.7.4 Dispatch and release model

- **Releases.** A joined thread's job is released when it becomes runnable, as in XNU, but never earlier than the previous release + T [SC-RT-017]:
  - a thread that wakes earlier MUST run at its fallback intent until then [SC-RT-017];
  - its budget C is replenished at each release;
  - this sporadic rule is what makes the admission test valid;
  - XNU gives every wake a fresh deadline and a fresh quantum, so a thread that wakes ten times per period gets ten budgets.
- **Dispatch.** Among real-time threads, dispatch MUST remain earliest-deadline-first on the absolute deadline release + D [SC-RT-018]. XNU already does this (`sched_prim.c:862-864`, `sched_rt.c`).
- **Blocking points.** A job ends when every joined thread is blocked, whatever it blocks in [SC-RT-021]:
  - a Mach message receive, with or without `MACH_RCV_TIMEOUT` (the audio doorbell on a queue-limit-1 port, P4-18);
  - `kevent` or `nd_wait`, a semaphore, a condition variable, or a sleep.

  The next wake is a release under SC-RT-017, whether it comes from a message, a timeout, an event or a signal. The admitted deadline semantics therefore hold across the doorbell receive.
- **A stated per-job deadline.** `nd_rt_set_deadline(d)` states the absolute deadline, on the SC clock, of the calling thread's ticket's current job (or of its next job, if the ticket has no running job). The kernel MUST then use the effective deadline max(d, the previous job's effective deadline + T) for that job, in place of release + D [SC-RT-022]:
  - the job's budget C is counted from its virtual release, effective deadline − D, or from its wake if that is later;
  - misses (SC-OVR-002) are measured against the effective deadline;
  - clamping to the previous deadline + T keeps each virtual release at least T after the last, so the admission test stays valid, while a thread woken late can still be ordered by the device's real deadline (`deadline_host` in `nd_audio.h`);
  - a caller that is not joined MUST get `SC_E_INVAL` [SC-RT-022].

### 4.8 Overrun, demotion and notices

```c
enum sc_error nd_rt_watch(int32_t kq, sc_ticket ticket, uint64_t udata);
enum sc_error nd_rt_notice_take(sc_ticket ticket, sc_rt_notice *out);
```

- **Overrun.** An overrun is when the joined threads' CPU time since the current release reaches C before the job ends. The kernel MUST then [SC-OVR-001]:
  - run them at their fallback intent in timeshare until the next release, immediately;
  - count one overrun.

  Today XNU leaves such a thread in the real-time band with `RT_DEADLINE_QUANTUM_EXPIRED` (`sched_prim.c:3480-3488`), where it still outranks every timeshare thread. This is Plan 9's "resources exhausted, deschedule" (`edf.c:296-304`), softened: the work continues at best effort instead of stopping.
- **Miss.** A job that ends after release + D MUST count one miss [SC-OVR-002].
- **Demotion.** `SC_RT_DEMOTE_OVERRUNS` (8) overruns within the last `SC_RT_DEMOTE_WINDOW` (64) releases MUST move the ticket to `DEMOTED` [SC-OVR-003]:
  - the reservation is dropped;
  - joined threads stay at their fallback intent;
  - the ticket stays valid for `nd_rt_status`, `nd_rt_notice_take` and `nd_rt_release`;
  - re-admission is a new `nd_rt_admit`, so the owner can ask for a larger C or a longer period.
- **Revocation.** When the domain shrinks and the domain test no longer holds, the kernel MUST revoke tickets, newest first, until it holds again [SC-OVR-004]:
  - the domain can shrink when a core goes offline, when a thermal cap removes cores, or when an administrator lowers ρ;
  - revoked tickets behave as demoted ones, in state `REVOKED`;
  - `system` tickets are revoked last.
- **Notices.** Every overrun, miss, demotion and revocation MUST produce a notice in the ticket's notice queue [SC-OVR-005]:
  - notices of a kind not yet taken MUST coalesce into one, with `count` and `first_ns`, so the queue never holds more than four notices [SC-OVR-005];
  - a misbehaving thread therefore cannot flood its owner.
- **Watching.** `nd_rt_watch(kq, t, udata)` MUST register the ticket's notice source in `kq`, so that a pending notice makes the one wait return an event carrying `udata` [SC-OVR-006]:
  - the source is the ticket's notice port, on `EVFILT_MACHPORT`;
  - `nd_rt_notice_take` removes the oldest pending notice, or returns `SC_E_AGAIN`.
- **Where notices are posted.** Notices MUST be posted outside scheduler context [SC-OVR-007]:
  - XNU already defers fail-safe reports this way (`sched_prim.c:6815-6875`);
  - posting never happens on the real-time thread's own time.
- **The ticket file.** `/n/sys/sched/rt/tickets` MUST list the reader's tickets as text, one line per ticket, with the admitted parameters, the counters and the ids of the joined threads (F-215, "data is the interface") [SC-OVR-008]:

  ```
  pid 412 ticket 3.1 state admitted T 2666667 C 500000 D 2666667 releases 22831 overruns 0 misses 0 max_used 181000 threads 1 tids 0x1a2f
  ```
- **Visible to the thread itself.** `nd_rt_status` and `nd_thread_intent_info` MUST be callable from a joined thread without blocking (no sleeping lock), and MUST reflect every overrun and miss counted up to that thread's current release [SC-OVR-009]. A real-time thread can therefore see its own overruns once per period, as P4-18's `client_overbudget` counter does, while its owner learns of them through the one wait (SC-OVR-006).
- XNU's fail-safe (`priority.c:158-194`) remains as a backstop. An admitted thread never reaches it, because C ≤ 50 ms and it is demoted at C.

### 4.9 How the audio service and the toolkit use it

This section is informative. The owning specifications (P4-18 `docs/audio/audio-service.md`, P4-12 toolkit) state their own requirements and cite the ids above.

- **The audio service's mixer (P4-18):**
  - The mixer thread calls `nd_rt_admit_ex` with `SC_RT_F_SYSTEM`, T = the mixer period, D = T minus its hand-off margin, and C from its own measured worst case. That admits it and joins it.
  - It registers the ticket's notices in its own one wait (`nd_rt_watch`).
  - If it is refused, it uses `sc_rt_refusal.max_computation_ns` to pick a longer period. The system share guarantees that a working mixer is admitted whatever users hold (SC-RT-004).
  - When it wakes late, it states the device deadline with `nd_rt_set_deadline` (AU-MIX-008); on time, release + D already orders it correctly and it makes no call.
  - Its notices are taken on a service thread and feed the service's `/n/sys/audio` statistics (AU-MIX-007).
- **Tier-1 callback streams (the toolkit audio stream, P4-12 with P4-18):**
  - The library creates the callback thread with `SC_INTENT_AUDIO`.
  - That thread calls `nd_rt_admit` itself, with T from the contract's period, D ≤ T and C from the stream contract (`AUcontract.rt_*` in `nd_audio.h`; AU-STREAM-004).
  - It then blocks each period in the doorbell receive (SC-RT-021), and after a late wake states `deadline_host` as its job deadline (SC-RT-022; AU-STREAM-007).
  - A refusal becomes a typed stream-open error, `AU_ERR_ADMISSION`, whose detail string carries `nd_sched_error_detail()` verbatim (AU-ABI-002) and whose `sc_rt_refusal` is returned by `au_error_refusal` (AU-ABI-006) (P12). It is never a silent fallback to an ordinary thread.
  - When the contract's period changes, the thread re-admits (SC-RT-019; AU-STREAM-016).
  - DSP helper threads join the same ticket; the stream exposes it as its group handle (`au_stream_rt_group`, AU-STREAM-010).
  - Notices arrive in the application's loop as `AU_EV_RT_NOTICE` audio events on the session descriptor, and a demotion is also a contract change (AU-EVENT-006).

**What P4-18 asked for (`audio-service.md` §4.14) and where it is specified:**

| AU item | Need | Specified by |
|---|---|---|
| S1 | self-admission without privilege, within a per-user budget; a synchronous reason on refusal; call again to change the period | SC-RT-001, SC-RT-003, SC-RT-004, SC-RT-007, SC-RT-019, SC-RT-020, SC-SEC-001. Decision: codes and the refusal record, plus a thread-local reason string. |
| S2 | the admitted thread blocks in a Mach receive (the doorbell, queue limit 1) with a timeout and is still periodic | SC-RT-017, SC-RT-021; the timeout's precision is SC-WAIT-005 |
| S3 | an absolute per-period deadline stated by the thread, for EDF between the mixer and client threads | SC-RT-018, SC-RT-022 |
| S4 | helper threads share one admission (the `os_workgroup` analogue) | SC-RT-009, SC-RT-010; the group handle is the `sc_ticket`, and helpers must be in the ticket's process (SC-RT-011) |
| S5 | kqueue readiness of nd9p fids and `EVFILT_MACHPORT` in the one wait | SC-WAIT-001, SC-9P-001 to SC-9P-021; timestamps and deadlines on one clock: SC-TIME-001 to SC-TIME-003 |
| S6 | a system share for audiod's mixer; client streams charged to the user | SC-RT-004, SC-SEC-001, SC-SEC-002 |
| S7 | exceeding C is stated and observable, never silent; visible to the thread and its owner through the one wait | SC-OVR-001 to SC-OVR-006, SC-OVR-009 |
| S8 | per-thread admitted parameters and misses readable as text | SC-OVR-008 |
- **Toolkit loops:**
  - Loops wait with `nd_wait(kq, deadline, leeway, …)`. A frame limiter is "wait for events or the next frame deadline" with leeway 0 and never spins (F-203).
  - The render thread is `interactive`, and drops to `background` while its window is hidden.
  - Job systems use `throughput`; asset streaming uses `background`.
  - Nothing in the toolkit is real-time except audio callbacks.

## 5. Versioning and capabilities

- `nd_sched_capabilities()` MUST report exactly the capabilities that the running kernel and libnd implement (`enum sc_cap`) [SC-CAP-001].
  - Version 1 defines bits 0–9.
  - New features add bits and never change the meaning of existing ones (P13).
  - `nd_sched_api_version()` returns the system's `SC_API_VERSION`.
- Every record starts with `size`, which the caller sets [SC-CAP-002]:
  - the kernel and libnd MUST accept any size at least as large as the version-1 size [SC-CAP-002];
  - they MUST reject smaller sizes with `SC_E_INVAL` [SC-CAP-002];
  - they MUST NOT write beyond the caller's size [SC-CAP-002];
  - fields added in later versions go at the end or in reserved space.
- Reserved fields MUST be zero on input (otherwise `SC_E_INVAL`) and MUST be written as zero [SC-CAP-003].
- Kernel behaviour that is not a call (§4.2 readiness, §4.4 slop, fire time, placement) is also reported through these bits. A program tests `SC_CAP_9P_READY` before relying on nd9p readiness, rather than testing a version number.

## 6. Security and capabilities

Authority follows `docs/architecture/namespaces-agents.md` §4: namespace paths and capability tokens, plus manifest entitlements enforced by `ndsandbox`.

| What | Grants | Rule |
|---|---|---|
| `nd_rt_admit` within the uid's budget | real-time service up to the budget | **No capability, entitlement or daemon is needed** (F-215: unprivileged, per-user budget) [SC-SEC-001]. |
| `SC_RT_F_SYSTEM` | the system share | The manifest entitlement `sched.rt.system`, bound at `exec` by `ndsandbox`; without it, `SC_RT_R_PERMISSION` [SC-SEC-002]. The audio service and wsys hold it. |
| `/n/sys/sched/cpu`, `/n/sys/sched/rt/domain`, `/n/sys/sched/rt/policy` | read topology and policy | Readable by any process whose namespace includes `/n/sys/sched`. |
| `/n/sys/sched/rt/tickets` | read own tickets | Lists only tickets of processes with the reader's uid; `/n/sys/sched/rt/all` lists every ticket and requires `cap:sched:admin` [SC-SEC-003]. |
| `/n/sys/sched/rt/ctl` | set ρ, budgets and the system share; revoke a ticket | A write requires `cap:sched:admin`, checked by `nsd` on attach and write [SC-SEC-004]. Lowering ρ below current use revokes tickets by SC-OVR-004. There is no timer-resolution control here or anywhere (SC-RES-001). |
| `nd_thread_set_intent` | QoS, placement, timer policy | No capability; clamped by the task (SC-INT-008). |
| `EVFILT_READ`/`EVFILT_WRITE` on nd9p | readiness | No new authority: an open descriptor is required, and `mac_vnode_check_kqfilter` still runs (`vfs_vnops.c:2022-2028`) [SC-SEC-005]. |

The `/n/sys/sched` tree is served from kernel sysctls (`kern.nd.sched.*`) by the `/n/sys` system server (P5-05).

## 7. Performance contract

These are requirements, each tied to a test in §8:
- **Hardware.** They hold on the NeoDarwin reference arm64 board (the `tests/hil` target). Runs on QEMU TCG report the numbers but cannot fail.
- **Wake error** is defined in §2.
- **Percentiles** are over at least 100,000 samples, unless stated otherwise.

| Id | Target | Condition |
|---|---|---|
| SC-PERF-001 | **Interactive wake error: p99 ≤ 100 µs, p99.9 ≤ 500 µs, 0 early.** | An `interactive` thread in `nd_wait` with leeway 0. Deadlines 1–17 ms ahead. The core is otherwise idle and deep idle states are enabled. [SC-PERF-001] |
| SC-PERF-002 | **Audio wake error: p99 ≤ 100 µs, 0 early.** | A thread joined to an admitted ticket, woken by a contract timer each period. All cores saturated by `throughput` and `background` load (2 threads per core). [SC-PERF-002] |
| SC-PERF-003 | **Cross-thread wake: p99 ≤ 100 µs.** | From `NOTE_TRIGGER` in one thread to the waiter returning from `nd_wait` on an idle core. [SC-PERF-003] |
| SC-PERF-004 | **nd9p readiness: p99 ≤ 500 µs** from a record being queued at a `libns` server to the client's `kevent` returning, through `nsd` with the readiness request. Within that, **≤ 250 µs** from queueing to the server sending `Rready`, and **≤ 100 µs** from `Rready` reaching nd9p to the waiter returning. | One record per wake; the client thread is `interactive` on an idle core; the server thread is `interactive`. Window protocol cites the total as the client-side part of its input latency (WP-PERF-005: 1 ms). [SC-PERF-004] |
| SC-PERF-005 | **Frame-pacing error: p99 ≤ 1 ms.** | The S7 game-loop prototype on NeoDarwin (wsys) at display rate. Error = \|actual presentation interval − target interval\|, taken from the frame event's actual presentation times. 10,000 frames at steady state. No timer-resolution call. No spin: the loop thread uses less than 2% CPU while waiting. [SC-PERF-005] |
| SC-PERF-006 | **Admitted deadlines are met: 0 misses.** | Total admitted density at the domain bound (test 9). Every job uses ≤ 0.9 C. All cores saturated by non-RT load, including a parallel compile. At least 10⁶ releases in total. In addition, the S7 synth-ui prototype on NeoDarwin at a 128-frame period has 0 underruns over a 60 s soak under the same load. [SC-PERF-006] |
| SC-PERF-007 | **Isolation: 0 misses for well-behaved tickets.** | As SC-PERF-006, with one ticket deliberately using 2 C per release. It is demoted to fallback each release and eventually `DEMOTED`. [SC-PERF-007] |
| SC-PERF-008 | **Notices in time; admission is fast.** A notice is takeable within max(2 T, 5 ms) of its cause. `nd_rt_admit` takes p99 ≤ 100 µs with 64 tickets admitted. | Any load. [SC-PERF-008] |

**Why 100 µs** (the charter proposed "p99 under 100 µs"; this contract keeps it):
- **It is small against every budget it serves:**
  - 1.2% of a 120 Hz frame;
  - 10% of the 1 ms pacing target;
  - 3.7% of a 128-frame audio period at 48 kHz.

  A tighter figure buys nothing that these budgets can use.
- **It is reachable.** With slop 0 the remaining costs are:
  - timer interrupt entry;
  - the thread-call hop (`kern_event.c:1726`);
  - one context switch;
  - idle exit.

  macOS already achieves tens of microseconds for `mach_wait_until` on an active thread (F-203, platform table).
- **Deep idle is the only large term.** SC-TIMER-008 removes it, only while a zero-slop timer is pending, so power is spent only when someone asked for precision.
- **Tighter is expensive.** Going below 100 µs at p99 with deep idle enabled would require keeping cores out of deep idle whenever any interactive thread exists.
- **Where the S7 host fell short.** It saw 0.5–1.7 ms for three reasons, all removed by §4.4:
  - XNU's 1 ms interactive slop cap;
  - leeway being unable to lower the slop;
  - CFRunLoop tolerance.

## 8. Conformance

Methods:
- **unit:** `bazel test` on the host;
- **kernel:** a kernel test (`tests/abi`, run on QEMU in CI);
- **protocol:** a 9P conformance run against a scripted `libns` server that counts `Tread`, `Tflush` and `Tclunk` per fid;
- **measure:** a measurement on NeoDarwin reference hardware (`tests/hil`);
- **S7:** an S7 prototype ported to NeoDarwin by replacing the shim.

| Test | Covers | Method | Pass criterion |
|---|---|---|---|
| SC-T-001 | header records; SC-CAP-003 layout; SC-9P-005 constants | unit (`//docs/kernel:nd_sched_layout_test`, `//docs/kernel:nd_sched_cxx`) | every record size and field offset matches §4 in C23 (freestanding, `-Wpedantic`) and in C++20; the enum underlying types hold; ticket accessors round-trip; `NOTE_*` values match `<sys/event.h>` on a Darwin host |
| SC-T-002 | SC-WAIT-001, SC-WAIT-002, SC-WAIT-003, SC-USER-001, SC-USER-002, SC-USER-003, SC-USER-004 | S7: `ndtk-selftest` on NeoDarwin | on a loop on the first thread and on a loop on a second thread, one wait returns: a one-shot timer, a repeating timer, pipe readiness, a wsys `events` record, a `post`, one coalesced wake for 100 triggers, and a real audio-contract change; no helper thread exists (`/n/sys/proc/<pid>` thread count is unchanged across the test); a trigger from a thread joined to a ticket returns without blocking (scheduler trace shows no block) |
| SC-T-003 | SC-9P-004, SC-9P-005, SC-9P-006, SC-9P-007, SC-9P-015 | protocol (`libns` server, a legacy `lib9p` server, and `nsd` between) | `Tversion 9P2000.L.nd1` is accepted by `nsd` and `libns`, and a legacy peer is retried with `9P2000.L`; message bytes match §4.2.2; `Rready` arrives at once when a record is queued and when the stream ends, with `count` = whole-record bytes; after 1,000 `Tready`s the server's queue, format, coalescing and single-reader state are unchanged (a concurrent `Tread` does not fail `busy`); a flushed `Tready` never answers; on a supporting peer, no `Tread` is outstanding while the client only waits |
| SC-T-004 | SC-9P-001, SC-9P-003, SC-9P-008, SC-9P-009, SC-9P-010, SC-9P-011, SC-9P-012, SC-9P-013, SC-9P-014 | protocol (counts `Tready`, `Tread`, `Tflush` and `Tclunk` per fid) | a length-0 file is classified stream; two opens use two fids; no request before interest; registering a knote issues exactly one `Tready`; `Rready` activates the knote with `data` = `count`; `END` and `Rlerror` give `EV_EOF` (with the errno); `read` issues an ordinary `Tread`, the estimate falls by the bytes read, and the next `Tready` is issued before the emptying `read` returns; `O_NONBLOCK` gives `EAGAIN` at estimate 0; `poll`/`select` follow the estimate; losing interest and `close` flush; `EVFILT_WRITE` reports iounit, and a `ctl` write error arrives in `write` |
| SC-T-005 | SC-9P-002, SC-9P-016, SC-9P-017, SC-9P-018, SC-9P-019, SC-9P-020, SC-9P-021 | protocol (virtio-9p host share; `nsd` in front of a legacy server) | a sized file matches XNU vnode readiness (size − offset); `nsd` offers `Tready` for a legacy backend and emulates it with one read-ahead; nd9p uses its own read-ahead only on the direct mount; the read-ahead is one `Tread` at iounit, `read` issues none of its own, a short `read` leaves the remainder, and the next read-ahead waits for an empty buffer; `pread` at another offset bypasses; `close` flushes before `Tclunk` |
| SC-T-006 | SC-TIME-001, SC-TIME-002, SC-TIME-003, SC-TIMER-001, SC-TIMER-002, SC-TIMER-003, SC-TIMER-004, SC-TIMER-009 | kernel + unit | `nd_sched_now_ns` tracks `mach_absolute_time`; the interface audit finds every deadline and timestamp field in `nd_sched.h`, `desktop.h` and `nd_audio.h` named `*_ns`/`nsec` or `*_host` and documented on the SC clock; a sleep/wake cycle leaves SC time unchanged while the offset o grows by the sleep length; contract timers fire at or after the deadline in 100,000 trials (0 early); calendar-epoch timers behave as upstream; `ext[0]` carries a fire time within [deadline, return time] when `SC_CAP_TIMER_FIRETIME` is set; a `NOTE_MACH_CONTINUOUS_TIME` contract timer counts time asleep |
| SC-T-007 | SC-TIMER-005, SC-TIMER-006, SC-TIMER-007, SC-WAIT-004, SC-WAIT-005, SC-RES-002 | kernel (DTrace `thread_callout__create` reports hard − soft deadline) | the slop equals the stated leeway for non-background intents (0 included) and max(leeway, default) for background; the defaults match the §4.4.3 table; a timer registered by an `interactive` thread keeps slop 0 when a `background` thread processes its events; the `kevent`, `mach_msg`, `semaphore_timedwait`, condition-variable, `nanosleep` and `mach_wait_until` timeouts of an `interactive` thread have slop 0; changing each tunable in SC-RES-002 leaves these results unchanged |
| SC-T-008 | SC-RES-001, SC-RES-003 | unit + kernel | an audit of sysctls, libSystem/libnd exports, `/n/sys` and boot-args finds no resolution control; Wine's `NtSetTimerResolution` and the `timeBeginPeriod` shim return success and change no measured slop |
| SC-T-009 | SC-TIMER-008 | measure | with deep PSCI idle states enabled, SC-PERF-001 holds; with the governor rule disabled (a debug boot-arg) the p99 is shown to exceed it, which proves the test is sensitive |
| SC-T-010 | SC-LIB-001, SC-LIB-002, SC-LIB-003, SC-LIB-004, SC-LIB-005, SC-LIB-006, SC-LIB-007, SC-LIB-008 | unit (libnd on host) + kernel | a DTrace syscall count gives exactly one `kevent64` per call with a null timeout (or `KEVENT_FLAG_IMMEDIATE` for POLL); the changelist carries `SC_TIMER_FFLAGS` with `ext[1]` = leeway (0 included); the deadline event is never returned; a past deadline returns at once; NONE disables the knote; 0 allocations (dyld interposition, as in S7); error codes as specified; `EINTR` is not retried |
| SC-T-011 | SC-INT-001, SC-INT-002, SC-INT-003, SC-INT-004, SC-INT-005, SC-INT-006, SC-INT-007, SC-INT-008 | kernel | `thread_policy_get` and `sc_intent_info` show the table's QoS, mode and slop for each intent; `audio` with no ticket equals `interactive`; a later `pthread_set_qos_class_self_np` resets the intent to none; a joined thread's intent change alters only its fallback; value 99 → `SC_E_INVAL`; a new pthread reports none; a task clamped to utility cannot exceed it |
| SC-T-012 | SC-PLACE-001, SC-PLACE-002, SC-PLACE-003, SC-PLACE-004, SC-PLACE-005, SC-PLACE-006 | measure (heterogeneous board) + kernel (QEMU, homogeneous) | `THREAD_AFFINITY_POLICY` → `KERN_NOT_SUPPORTED`; on AMP, `interactive` threads run on P at > 95% of samples while P has capacity; a 4-thread `throughput` group is never split across types in a 60 s run; `background` runs on E; `/n/sys/sched/cpu` and `rt/domain` match firmware; on QEMU `SC_CAP_PLACEMENT` is clear |
| SC-T-013 | SC-RT-001, SC-RT-002, SC-RT-003, SC-RT-004, SC-RT-005, SC-RT-006, SC-RT-007, SC-RT-020, SC-SEC-001, SC-SEC-002 | kernel (synthetic domains m = 1, 2, 4, 8) | each refusal reason is produced by a minimal request, in table order; `max_computation_ns` is the exact boundary (C passes, C + 1 tick fails); 32 threads admitting concurrently never exceed the bound; a refusal changes no thread or reservation; the system share stays admissible with users at budget; an unentitled `SC_RT_F_SYSTEM` → `SC_RT_R_PERMISSION`; `nd_rt_admit` leaves the caller joined, `SC_RT_F_RESERVE_ONLY` joins no thread; after each refusal `nd_sched_error_detail()` is non-empty and names the failed test |
| SC-T-014 | SC-RT-008, SC-RT-009, SC-RT-010, SC-RT-011, SC-RT-012, SC-RT-013, SC-RT-014, SC-RT-019 | kernel | a joined thread shows `TH_MODE_REALTIME` with the declared C (no D/2 inflation); two joined threads share one budget; double join → `SC_E_BUSY`; a foreign or stale ticket → `SC_E_HANDLE`; the child after `fork` holds no ticket; `exec` and exit release; leave and release restore the fallback before returning; status is correct in every state; re-admission keeps the handle and applies the new T, C and D from the next release, and a refused re-admission leaves the old reservation intact (the domain's headroom is unchanged) |
| SC-T-015 | SC-RT-015, SC-RT-016 | kernel | a raw time-constraint policy from an unticketed thread is admitted and joined, or returns `KERN_RESOURCE_SHORTAGE` with the policy unchanged; a real-time thread forced outside a ticket is demoted `RT_DISALLOWED`; no CPU-limit ledger is set on ticket threads |
| SC-T-016 | SC-RT-017, SC-RT-018, SC-RT-021, SC-RT-022 | kernel | a thread waking 10 times per period gets one budget per T and runs at fallback between; two tickets with different D are dispatched earliest-deadline-first on one core; blocking in `mach_msg`, `kevent`, a semaphore or a sleep each ends the job, and the next wake (message, timeout or event) is a release; a stated deadline earlier than the previous effective deadline + T is clamped to it; `nd_rt_set_deadline` from an unjoined thread → `SC_E_INVAL` |
| SC-T-017 | SC-OVR-001, SC-OVR-002, SC-OVR-003, SC-OVR-004, SC-OVR-005, SC-OVR-006, SC-OVR-007, SC-OVR-008, SC-OVR-009 | kernel | a job using 1.5 C runs at fallback after C (scheduler trace) and counts one overrun; a late job counts one miss; 8 overruns in 64 releases → `DEMOTED`, and the reservation is available to a new admission; offlining a core revokes the newest ticket first; 1,000 overruns yield one coalesced notice with count 1,000; the notice arrives through `nd_wait` via `nd_rt_watch`; no notice is posted from scheduler context (lock assertions); the `tickets` file matches `nd_rt_status` and lists the joined thread ids; the overrunning thread itself reads the overrun from `nd_rt_status` in its next release without blocking |
| SC-T-018 | SC-CAP-001, SC-CAP-002, SC-CAP-003 | kernel + unit | the capability bits match the features exercised by the other tests; a record size of 8 → `SC_E_INVAL`; a size larger than version 1 is accepted, and bytes past the known size are untouched; a non-zero reserved field → `SC_E_INVAL`; output reserved fields are zero |
| SC-T-019 | SC-SEC-003, SC-SEC-004, SC-SEC-005 | protocol (`nsd`) + kernel | another uid's tickets are absent from `tickets`; `rt/all` and `rt/ctl` are refused without `cap:sched:admin` and accepted with it; a MAC policy that denies `kqfilter` denies nd9p readiness |
| SC-T-020 | SC-PERF-001, SC-TIMER-010 | measure (`ndsched-wakeprobe` + S7 game-loop timer log) | p99 ≤ 100 µs, p99.9 ≤ 500 µs, 0 early over 100,000 waits |
| SC-T-021 | SC-PERF-002 | measure | p99 ≤ 100 µs, 0 early, under 2 × cores load threads |
| SC-T-022 | SC-PERF-003 | S7: `ndtk-selftest --latency` | wake p99 ≤ 100 µs over 100,000 triggers |
| SC-T-023 | SC-PERF-004 | measure (a loopback `libns` server through `nsd`; the server timestamps the queueing of each record) | record queued → client `kevent` return p99 ≤ 500 µs; queued → `Rready` sent p99 ≤ 250 µs; `Rready` at nd9p → waiter return p99 ≤ 100 µs |
| SC-T-024 | SC-PERF-005 | S7: game-loop on wsys | pacing error p99 ≤ 1 ms over 10,000 frames; the call audit shows no timer-resolution call; loop CPU < 2% while waiting |
| SC-T-025 | SC-PERF-006 | measure + S7: synth-ui | 0 misses over ≥ 10⁶ releases at the bound; synth-ui 0 underruns over 60 s at 128 frames under compile load |
| SC-T-026 | SC-PERF-007 | measure | 0 misses for well-behaved tickets while one ticket overruns until it is demoted |
| SC-T-027 | SC-PERF-008 | measure | notice latency ≤ max(2 T, 5 ms) at p100 over 10,000 notices; admission p99 ≤ 100 µs with 64 tickets |
| SC-T-028 | SC-RT-021, SC-RT-022, SC-WAIT-005, SC-PERF-006 | measure (the P4-18 doorbell pattern) | an admitted thread blocks each period in `mach_msg` on a queue-limit-1 port with `MACH_RCV_TIMEOUT` of 2 T; a producer sends one message per T; the thread states the producer's deadline with `nd_rt_set_deadline`; over 10⁶ periods under the SC-PERF-006 load: 0 misses, and every timeout wake is accounted as a release |

Every requirement id in §4–§7 appears in the "Covers" column, so every MUST is covered: SC-TIME, SC-WAIT, SC-9P, SC-USER, SC-TIMER, SC-RES, SC-LIB, SC-INT, SC-PLACE, SC-RT, SC-OVR, SC-CAP, SC-SEC and SC-PERF.

## 9. Rationale and evidence

**One `kevent` wait, with nothing new beside it (P1; F-201; heritage §4).**
- Exec `Wait(sigmask)`, GEM `evnt_multi` and Horizon's handle wait all made the application's loop a single call over every source.
- XNU's kqueue already covers descriptors, Mach ports, processes, signals, user events and timers.
- What was missing is the one source NeoDarwin adds: 9P-served files, which carry all of wsys, `/n/agent` and `/n/sys`. Without it every client would fall back to the pilot's "one proc reads events into a channel" pattern, which is the helper thread F-201 documents in GTK, SDL, Godot and mpv.
- The S7 shim spent about 200 lines building this wait from CFRunLoop parts (W11). On NeoDarwin that becomes a table in the toolkit.

**Readiness through a readiness request, with read-ahead only as a fallback.**
- 9P has no readiness message. Plan 9's idiom for waiting on several files is a helper proc parked in `read` for each (libthread `ioproc`, F-217's platform table). A kernel read-ahead would move that helper into nd9p, but it keeps the helper's side effects:
  - data leaves the server before the application asks for it;
  - a read stays parked while the application is idle, which collides with window protocol's single outstanding read per stream (WP-EV-016) and its format switch (WP-EV-021);
  - motion coalescing (WP-EV-004) stops applying to the record already fetched.
- The window protocol asked for readiness "without consuming data and without parking a read". `Tready` gives exactly that:
  - it is one small extension message, implemented once in `libns`;
  - it is invisible to servers that do not know it, because `nsd` emulates it;
  - it costs one round trip per wake, which is inside the 500 µs budget.
- The read-ahead remains as the fallback for direct mounts of foreign servers, where a file that is readable for the one wait is worth more than exact queue semantics.

**Leeway replaces slop, instead of raising it (F-203, P10).**
- XNU's rule "slop = max(policy, leeway)" means a program cannot ask for more precision than its QoS allows, and a `USER_INTERACTIVE` thread's policy allows 1 ms (§4.4.1). That is the whole gap between the kernel's accuracy and what games need, and it is why the S7 host timer was 0.5–1.7 ms late.
- Letting the stated leeway win in both directions makes "deadline D, leeway L" a complete statement.
- Background keeps XNU's floor, so power policy still holds where precision was never asked for.

**No resolution knob (F-203: 13 projects, 48 call sites of `timeBeginPeriod`-class calls).**
- A tickless kernel has no resolution to set. The friction is programs raising a global setting because the precise primitive is hidden.
- SC-RES-001 forbids the setting.
- SC-RES-003 makes foreign calls harmless, as Wine's stubs already are on Unix.

**Intent, not priority or affinity (F-204; heritage: Switch reserved cores).**
- XNU QoS is the best-expressed model in the study's platform table. The gaps on NeoDarwin are:
  - a portable vocabulary;
  - placement on non-Apple heterogeneous parts, where the closed CLPC is absent (SC-PLACE-003);
  - timer precision tied to intent rather than to QoS tiers tuned for battery life.
- `interactive-frame` (F-204's list) is folded into `interactive`, as the charter's four-intent list does. The frame deadline is expressed by the wait's deadline, not by a separate class.
- Affinity is refused because the evidence is of programs pinning threads to probe hardware (llama.cpp) or to fight throttling (dolphin, blender). The topology file and intent remove both reasons.

**Admission modelled on Plan 9 EDF, enforced by XNU (F-215; heritage §4).**
- Plan 9 is the only surveyed system that answers a real-time request with "yes" or "no, because". Every other system degrades silently: zed on Linux, SDL's rtkit path, JUCE without `avrt.dll`.
- XNU contributes the rest, and this contract enables or tightens it rather than replacing it:
  - deadline-ordered real-time dispatch;
  - time-constraint parameters;
  - workgroups;
  - an allow-list mechanism (SC-RT-015).
- The sporadic release rule (SC-RT-017) and demotion at budget exhaustion (SC-OVR-001) make the admission test true in practice. Without them, one overrunning thread can take other tickets' time, because XNU keeps an exhausted real-time thread above every timeshare thread and gives it a fresh quantum at every wake.
- The notice travels through the one wait (P1, P12: asynchronous failures are events). An audio service learns of overruns the way it learns of anything else.

**Budgets are stated (P11).**
- The refusal record reports the headroom and the largest admissible C, so a client can negotiate instead of guessing.
- `/n/sys/sched/rt/policy` and `tickets` make the policy and the use visible as data (F-215, observability).

**Clock choice.**
- The SC clock is `mach_absolute_time`, not the continuous clock the S7 shim used, because:
  - XNU's real-time deadlines use it;
  - `NOTE_MACHTIME`'s default epoch uses it;
  - the audio contract's host times use it (`nd_audio.h`, `*_host`);
  - deadlines on it do not all expire at once on resume from sleep.
- Timers that must count sleep add `NOTE_MACH_CONTINUOUS_TIME`.

## 10. Open issues

1. **The nd9p per-open context.** XNU's `VNOP_READ` and `vn_kqfilter` carry no per-open state for regular files. P5-02 must choose how to do it: nd9p-specific fileops for stream files, or a VFS extension alongside `fg_vn_data`. This contract fixes only the observable semantics.
2. **Stream classification by length 0.** This is a heuristic. A server-declared marker (a `qid.type` bit, or a `Rgetattr` flag in the `9P2000.L.nd1` profile) would be exact. Decide with P5-01 before v1 is normative. The message types 160 and 161 must also be confirmed free in every 9P dialect `nsd` bridges.
3. **wsys migration to the SC clock. Resolved in the specifications** (consistency pass, `docs/spec-consistency.md`). Window protocol revision 2 now defines its deadline clock as the SC clock (window-protocol.md §2; its open issue 4 is resolved). What remains is implementation: P4-03 must switch `wsys` from the pilot's `mach_continuous_time`. Until it does, the two differ by the sleep offset, and only after a sleep.
4. **x86 hybrid placement.** Feeding Intel Thread Director / HFI class data to Edge is Phase 6a work. Until then `SC_CAP_PLACEMENT` is clear on x86.
5. **The value of C across core types.** C is declared for the RT domain's core type. If a domain ever mixes types (it does not in version 1), C would need scaling by capacity.
6. **Global-EDF bound versus XNU's per-cluster run queues.** XNU's RT queues are per cluster, with stealing and a 100 µs deadline epsilon. The density bound assumes ideal global EDF. SC-T-025 measures whether the margin (ρ = 0.75, jobs ≤ 0.9 C) absorbs the difference. If it does not, the fallback is partitioned admission (first-fit by density per core), which is exact for EDF on each core.
7. **Idle-state governance on SBSA.** SC-TIMER-008 needs PSCI idle-state exit latencies from firmware (ACPI LPI `_LPI` or the DT `idle-states` properties). The platform expert does not read them yet: `PSCIPowerManager` idles with WFI only (`arm64-sbsa-bringup.md` §2.3). Until it does, only WFI is used, which trivially satisfies the rule.
8. **The kevent expiry hop.** If SC-T-020 shows that the thread-call hop dominates the wake error, activate critical knotes directly from the timer interrupt. This needs an audit of `kqlock` for interrupt context.
9. **Several opens of one shared stream.** A window's `events` is one stream however many fids read it (WP-EV-015). Each open file gets its own readiness (SC-9P-003), so when one open consumes the records, the other's estimate is stale: its next `read` parks at the server (WP-EV-014 allows this), and under SC-9P-011 an `O_NONBLOCK` read with a stale estimate above 0 issues a `Tread` that then blocks. The fix is either a server rule (a `Tread` on a stream fid whose readiness was reported but whose records another fid consumed fails at once with a retryable error) or a lower estimate on every `Rready` of the other fids. Decide with P5-02; single-reader clients, which WP-EV-016 already expects, are unaffected.
10. **Namespace and token vocabulary.** `docs/architecture/namespaces-agents.md` does not yet list the `/n/sys/sched` tree (its §3 names `pkg, input, net, power, proc, dev, srv, audit`), the capability token `cap:sched:admin` (SC-SEC-003, SC-SEC-004), or manifest entitlement names such as `sched.rt.system` (SC-SEC-002). They are to be added there when P5-03 fixes the token vocabulary and P5-05 the `/n/sys` services. _Resolved 2026-09-28: namespaces-agents.md §2 and §3 now list it (the tree in the `/n/sys/<service>` list; tokens and the `sched.rt.system` entitlement in the keyd and ndsandbox rows)._

## 11. Changelog

| Version | Date | Change |
|---|---|---|
| 1 (draft) | 2026-09-28 | First draft for P4-16: the one wait (nd9p readiness, `EVFILT_USER`, contract timers, no resolution setting, `nd_wait`), thread intent and placement, real-time admission with overrun handling and notices, performance contract, 28 conformance tests. Same day, before review: `nd_rt_admit` admits and joins the calling thread and re-admits a joined thread (SC-RT-001, SC-RT-019); thread-local reason strings (SC-RT-020); blocking points including the Mach doorbell (SC-RT-021, SC-WAIT-005); stated per-job deadlines (SC-RT-022); overruns visible to the thread (SC-OVR-009); mapping of P4-18's needs S1–S8 (§4.9). For P4-17: one interface clock, `mach_absolute_time`, with the continuous-time conversion rule (§4.0); nd9p readiness through the non-consuming `Tready`/`Rready` extension, with read-ahead only as a fallback (§4.2); a queued-to-`kevent` latency bound (SC-PERF-004); the header also compiles as C++20. |
| 1 (draft) | 2026-09-28 | Consistency pass with WP and AU (`docs/spec-consistency.md`); no requirement changed and no id renumbered or removed. §2 (stream file) and §4.0 cite the window protocol's and audio service's adoption of the SC clock and length-0 `events` files (WP-EV-041, AU-NS-009). §4.9 (informative) now matches AU: late-wake deadlines only (AU-STREAM-007, AU-MIX-008), `au_error_refusal` (AU-ABI-006), notices as `AU_EV_RT_NOTICE` (AU-EVENT-006). Open issue 3 resolved in the specifications; 9 and 10 added. |
