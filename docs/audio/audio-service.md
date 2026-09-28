<!-- SPDX-License-Identifier: BSD-2-Clause -->
# The audio service and stream contract

| | |
|---|---|
| **Status** | draft |
| **Version** | 1 |
| **Epic** | P4-18 (`roadmap/backlog.yaml`) |
| **Evidence** | study repository `../NeoDarwin-api-study` at commit `77e9b08`: `friction/F-215-realtime-audio-thread.md`, `friction/F-216-audio-stream-contract.md`, `reports/q2-shapes.md` (C10, R10), `reports/q5-compat.md` (C1, §5, §7 item 5), `reports/heritage.md` §4 and §7 (`AUD.voice`), `reports/s7-prototypes.md`, `prototypes/ndtk/API.md` §4, `prototypes/ndtk/Sources/NDTK/Audio.swift`, `prototypes/ndtk/SHIM-NOTES.md` (Audio, W7, W8, W9), `prototypes/synth-ui/sdl3/NOTES.md` |
| **Supersedes** | none |
| **Header** | `docs/audio/nd_audio.h` (`AU_API_VERSION` 1), checked as C23 by `nd_audio_check.c` and as C++20 by `nd_audio_cxx.cpp` (`bazel test //docs/audio/...`) |

This document fixes how NeoDarwin programs make and capture sound. It covers the audio daemon `audiod` and its 9P tree at `/n/sys/audio`, and the client library `libndaudio` with its C ABI. It fixes the shared-memory ring and doorbell that carry samples without crossing 9P each period. It fixes the stream contract record, and how changes to that record reach the application's one wait. It defines the two tiers: streams, and voices on a system mixer that runs on a fixed period. It also covers capture, devices and hotplug, a small routing policy, the PulseAudio-protocol door, the capabilities that grant each of these, the performance targets, and the rules for the client's real-time render callback.

It deliberately leaves three things to other documents:
- **The Swift surface.** The toolkit API specification (P4-12) owns it and cites the requirements here.
- **Real-time admission, the one wait and the clock.** These belong to `docs/kernel/scheduling-contract.md` (P4-16, "SC"). This document cites SC's requirement ids and does not restate them; §4.14 maps each need to the SC ids that meet it.
- **The driver family interface.** This belongs to the audio-driver epic. §4.15 states only what `audiod` requires from it.

## 1. Scope and non-goals

**In scope:**
- `audiod`: its namespace, the system mixer, devices, routing and security.
- `libndaudio`: sessions, streams, voices, conversion, the render thread, and events.
- The shared-memory layouts.
- The PulseAudio-protocol door (`pulsedoor`), together with the ALSA path that runs through it.
- Performance and conformance.

**Non-goals for version 1** (each is either reserved as a capability, §5, or listed in §10):
- **Exclusive device access** (`AUD.exclusive`). The charter makes it at most a flag on open. Version 1 has none.
- **Duplex callbacks,** where input and output arrive in one call (`AU_CAP_DUPLEX` is reserved).
- **Per-voice pitch, pan and effects,** and any bus or effect graph. Version 1 voices have gain and looping only.
- **Plug-in standards and MIDI** (charter §3: `AUD.plugin` and `AUD.midi` are excluded).
- **Per-application routing matrices.** There is one output role and one input role (§4.12).
- **Compressed and passthrough formats** (AC-3, DTS over HDMI). The service handles PCM only.
- **Network audio.** A remote 9P client can use `AU_MODE_FILE`, but nothing is guaranteed about its timing.

## 2. Terms

| Term | Meaning |
|---|---|
| **audiod** | The audio daemon. It is the only process that talks to audio device drivers, and it serves `/n/sys/audio`. |
| **libndaudio** | The client library. Its C ABI is `nd_audio.h`. Every client uses it: the toolkit, SDL3's NeoDarwin backend, and the Pulse door. |
| **Session** | One 9P attach to `audiod`, carrying the client's capability token. It owns streams, buffers and voices. |
| **Device** | An audio endpoint that a driver exposes (output, input, or both), identified by a stable 16-byte **uid**. |
| **Mixer** | The per-output-device real-time loop inside `audiod` that sums streams and voices once per **mixer period**. |
| **Mixer period** | The mixer's fixed tick, in device frames. It is a power of two (§4.9). |
| **Tick** | One run of the mixer, at the start of a mixer period. |
| **Stream** | Tier 1: a continuous flow of PCM between a client and one device, carried by a **ring**. |
| **Callback mode (pull)** | The library's admitted real-time **stream thread** calls the application's render function once per period. |
| **Queue mode (push)** | The application writes (output) or reads (capture) frames from any thread. |
| **File mode** | PCM moves through the stream's 9P `data` file. For scripts and remote clients. |
| **Ring** | The per-stream shared-memory region: a header, a copy of the contract, and sample data (§4.4). |
| **Doorbell** | The per-stream Mach port on which `audiod` announces that a period is due (§4.4). |
| **Lead** | How many periods ahead of the mixer the client writes (output) or may lag behind it (capture). |
| **Contract** | The `AUcontract` record: everything a client needs to know about timing and identity (§4.6). |
| **Generation** | A 64-bit counter in the contract. It increases on every contract change. |
| **Voice** | Tier 2: one playback of an immutable **buffer** on the system mixer. |
| **Host time** | `mach_absolute_time()` ticks: the SC clock in ticks (SC-TIME-001, SC-TIME-002). `*_host` fields are in host ticks; `*_ns` fields are in nanoseconds. `au_host_to_ns` converts a host time to the SC clock's nanoseconds, which `nd_wait` and `nd_rt_set_deadline` take without further conversion. Host time does not advance while the system sleeps (SC-TIME-003). |
| **Position** | A monotonic 64-bit frame count. Ring positions are in device frames; callback positions are in client frames. |
| **Terminal** | The analogue or digital edge of the device: the DAC output, or the ADC input. |
| **T2** | Allocation-free, lock-free Swift (language-policy.md §2), or C held to the same rules. |
| **SC** | `docs/kernel/scheduling-contract.md`. Its `nd_rt_admit` admits a reservation (T, C, D) and joins the calling thread to the resulting ticket (SC-RT-001). |

## 3. Model

### 3.1 Objects and ownership

```
 client process                                         audiod
 ┌───────────────────────────────┐                     ┌──────────────────────────────────┐
 │ app ─▶ libndaudio             │   9P /n/sys/audio   │ 9P service threads (T1)          │
 │        session ────────────────┼────────────────────▶│  sessions, ctl, contract, events │
 │         ├ stream ─ ring  ◀══════ shared memory ═════▶│                                  │
 │         │   stream thread (RT)◀── doorbell (Mach) ───│ mixer thread per output (RT, T2) │
 │         ├ buffers (in audiod) │                     │  rings ─▶ Σ ◀─ voices            │
 │         └ voices ─ session region ═══ shared memory ═▶│  ─▶ device ring ─▶ audio dext    │
 └───────────────────────────────┘                     └──────────────────────────────────┘
```

| Object | Created by | Owned by | Destroyed when | Handle |
|---|---|---|---|---|
| Session | `au_open` (9P attach) | the client | `au_close`, or the attach's connection closes | `AU_KIND_SESSION` |
| Stream | `au_stream_open` (`streams/clone`) | its session | `au_stream_close`, or the session ends | `AU_KIND_STREAM` |
| Ring | `audiod`, at stream open and at each layout transition | `audiod`; mapped by the client | the stream is closed, or the ring is retired | none (memory entry) |
| Buffer | `au_buffer_create` (`mixer/buffers/clone`) | its session; `audiod` holds the samples | released **and** no voice plays it | `AU_KIND_BUFFER` |
| Voice | `au_voice_play` (slot allocated by the client) | its session | it ends, is stopped, or the session ends | `AU_KIND_VOICE` |
| Device | the driver family registers an engine | `audiod` | the driver goes away (the uid persists) | `AU_KIND_DEVICE` |

### 3.2 Stream state machine

```
            au_stream_open
                 │
                 ▼          start            drain (output)        ring empty
            ┌─────────┐ ─────────▶ ┌─────────┐ ─────────▶ ┌──────────┐ ─────────▶ STOPPED
            │ STOPPED │            │ RUNNING │            │ DRAINING │   (AU_EV_DRAINED)
            └─────────┘ ◀───────── └─────────┘            └──────────┘
                 ▲          stop     │     ▲
                 │                   │     │ pinned device returns
                 │   pinned device   ▼     │
                 │   leaves      ┌──────────┐
                 │               │ DETACHED │
                 │               └──────────┘
                 │   layout change: the old ring becomes RETIRED; the stream keeps its handle
                 │   and its state, on a new ring (§4.5.6)
          close ─┴─▶ CLOSED (handle invalid)
```

### 3.3 Timing model (output, callback mode, lead 1)

```
 tick n-1            tick n              tick n+1            tick n+2
   │ mixer mixes n-1  │ mixer mixes n     │ DMA plays n       │
   │ doorbell ───────▶│                   │                   │
   │  client renders n│                   │                   │
   │  (deadline = tick n)                 │                   │
   ├─── lat_client ───┼─── lat_mixer ─────┼─ lat_device ─┼ lat_hw ─▶ terminal
```

- At tick *n−1* the mixer consumes stream period *n−1* and rings the doorbell.
- The stream thread renders period *n* into the ring, and must finish before tick *n*, when the mixer consumes it.
- The mixed period is written to the device ring, which the DMA engine plays one mixer period later.

In a steady state, the stream thread makes one system call per period: the doorbell receive.

## 4. Interfaces

### 4.1 The namespace: `/n/sys/audio`

**Serving and mounting.**
- `audiod` MUST serve its tree over 9P2000.L and post it in the `nsd` registry as `audio`, so that `nsd` mounts it at `/n/sys/audio` in namespaces that were given it (namespaces-agents.md §2, §4). [AU-NS-001]
- A process whose namespace does not contain `/n/sys/audio` has no audio. `libndaudio` MUST then fail `au_open` with `AU_ERR_PERMISSION`, not fall back to anything. [AU-NS-002]

**Sessions.** Each 9P attach is one session. Everything created under an attach belongs to it, and `audiod` MUST destroy a session's streams, voices and buffers within one second of its last fid being clunked or its connection closing. [AU-NS-003]

**The tree:**

```
/n/sys/audio/
    ctl          rw  service control; read: one state line (§4.1.1)
    caps         r   the capability names of §5, one per line: `name value`
    events       r   AUevent records for this session (32 bytes each); blocks; length 0, readable per SC-9P-006 (AU-NS-009)
    routes       rw  routing policy, text (§4.12); write needs cap:audio:device
    schema       r   agent protocol (agent-protocol.md): verbs and state shape
    state        r   JSON snapshot: devices, defaults, sessions, streams, active capture
    actions      rw  agent verbs: set-default, set-gain, mute, unmute (same capability gates)
    log          r   JSON event lines: device, default and capture start/stop
    devices/
        index    r   one line per device: UID HANDLE DIR KIND PRESENT DEFAULT NAME
        UID/                                       (32 lowercase hex digits)
            info     r   AUdevinfo record (256 bytes)
            ctl      rw  write: `period-max=N`, `rate=R`, `gain=G`, `mute=0|1`; read: state line
            stats    r   text counters: ticks, late ticks, xruns, driver resets
    streams/
        clone    r   opening allocates a stream directory; read returns its number N
        N/
            ctl      rw  the stream control language (§4.1.2); read: one state line
            contract r   AUcontract (256 bytes); each read is a consistent snapshot
            status   r   the same contract as `key value` text, one per line
            ring     r   the ring memory entry and the doorbell receive right (§4.4.1)
            events   r   AUevent records for this stream only (for tools and agents)
            data     rw  file mode PCM; blocking; not real-time
            stats    r   text counters (AUstream_stats fields)
    mixer/
        ctl      rw  master gain and mute of the default output; read: state line
        contract r   the mixer's AUcontract for this session (voice path)
        region   r   the session region memory entry (§4.9.3)
        buffers/
            clone    r   allocates a buffer directory N
            N/ctl    rw  `format=… channels=… rate=… frames=… mask=…`, then `seal`; `release`
            N/data   w   PCM, written before `seal`
        voices/
            clone    r   allocates a voice slot N
            N/ctl    rw  `play buffer=B gain=G loop=0|1 at=T start=S end=E`, `stop`,
                         `gain=G`, `loop=0|1`; read: state line
```

**Rules for the tree:**
- **Text.** Every text file MUST be UTF-8, with `key=value` tokens separated by single spaces and lines ended by `\n`. Values MUST contain no spaces; a stream name is percent-encoded. [AU-NS-004]
- **Unknown keys and verbs.** A `ctl` write with an unknown key or verb MUST fail with the 9P error string `unknown key KEY` or `unknown verb VERB` and change nothing. This is how a later client detects an older server. [AU-NS-005]
- **Binary records.** Binary files (`contract`, `info`, `events`) MUST carry the little-endian records of `nd_audio.h`, whole records only. A read shorter than one record fails with `short read`. [AU-NS-006]
- **Session scope.** `streams/`, `mixer/buffers/` and `mixer/voices/` MUST list only the reading session's own objects, except for a session holding `cap:audio:mixer` (§6), which sees every session's streams. [AU-NS-007]
- **ctl and the ring are equivalent.** Everything the shared-memory paths can do, a `ctl` write can also do, with the same semantics, so that scripts and agents need no shared memory (00-overview.md §3, "data is the interface"). [AU-NS-008]
- **Event files are stream files.** Every `events` file MUST report length 0, so that nd9p classifies it as a stream file (SC-9P-001), and MUST be readable in the sense of SC-9P-006 exactly when at least one whole `AUevent` record is queued for the reader, or the session has ended. [AU-NS-009]

#### 4.1.1 Service `ctl`

- **Read.** Reading `/n/sys/audio/ctl` returns one line, for example: `audiod 1 out=UID in=UID mixer-period=128 rate=48000 streams=3 voices=5 sessions=2`.
- **Writes** (`cap:audio:device`): `default-out=UID`, `default-in=UID`, `period-max=N` (the ceiling of §4.9.1), `gain=G`, `mute=0|1`.

#### 4.1.2 Stream `ctl`

| Write | Effect | Valid in |
|---|---|---|
| `open dir=out\|in mode=callback\|queue\|file channels=C mask=M rate=R format=F period=P lead=L device=default\|UID convdelay=D name=NAME [pin=1] [exact=1] [monitor=1]` | Configures the stream. `rate`, `format` and `convdelay` (resampler delay in client frames) describe the client side; the ring always carries F32 at the device rate (§4.5.4). | once, first |
| `start`, `stop`, `drain` | §3.2 | STOPPED, RUNNING |
| `gain=G` | stream gain, linear, 0 to 8, ramped over one mixer period | any |
| `pin=UID`, `follow` | §4.12 | any |
| `admitted period=NS computation=NS constraint=NS` | Written by the library after `nd_rt_admit` succeeds (SC-RT-001, SC-RT-019), so that the contract carries the admission; `admitted period=0 computation=0 constraint=0` after a demotion or revocation (AU-EVENT-006). | callback mode |
| `close` | Destroys the stream. | any |

Reading `ctl` returns, for example: `stream 3 out callback running gen=7 device=UID period=128 latency=8021333 underruns=0`.

### 4.2 The client library and the split between 9P and shared memory

**Implementation and layering.**
- `libndaudio` is first-party Swift (language-policy.md §2):
  - T1 for sessions, 9P and events;
  - T2 for the stream thread, the ring and voice-command paths, and conversion.
- It exports the C ABI of `nd_audio.h` through `@_cdecl`. The C ABI is the stable, cross-language surface: the toolkit, SDL3's NeoDarwin backend (q5-compat C1), `pulsedoor`, and out-of-tree bindings are all built on it.
- The Swift surface (`Audio` in the toolkit) is defined by the toolkit API specification over this ABI and cites these requirement ids (spec-conventions.md §7).
- Every client MUST reach `audiod` only through `libndaudio`, or through the 9P tree as specified in §4.1. The ring layouts are an ABI between `audiod` and `libndaudio` only. [AU-LIB-001]

**What goes where:**

| Traffic | Path | Frequency |
|---|---|---|
| attach, open, start/stop, gain, pin, drain, close | 9P `ctl` | per user action |
| contract snapshot | 9P `contract`, or the ring's contract copy | per change |
| contract changes, underruns, device hotplug, voice ends, revocation | 9P `events` → session descriptor | per event (rate-limited, §4.8) |
| buffer upload | 9P `mixer/buffers/N/data` | once per buffer |
| **samples** (callback and queue modes) | **stream ring** | continuous |
| **"period due"** | **doorbell** Mach message | once per period |
| clock anchor, positions, counters | stream ring header | per tick |
| voice play, stop, gain, loop | **session region** command ring (or `voices/N/ctl`) | per command |
| voice state | session region voice table | per tick |

- **No 9P on the period path.** In callback and queue modes, the library MUST NOT send any 9P message per period, and nor must `audiod`. Per-period traffic is limited to ring memory and the doorbell. [AU-LIB-002]
- **The session descriptor.** `au_session_fd` MUST return a descriptor that SC's kqueue readiness (SC-WAIT-001, SC-9P-006, SC-9P-008, SC-9P-009) reports readable exactly when `au_session_events` would return at least one event. This is how contract changes arrive in the application's one wait (charter P1). [AU-LIB-003]
- **Draining events.** `au_session_events` MUST NOT block. It returns up to `cap` events in the order `audiod` produced them. [AU-LIB-004]
- **Library state before an event is returned.** The library MUST apply each event to its own state before returning it. For example, `au_stream_contract` already returns the new generation when `AU_EV_CONTRACT` is returned. [AU-LIB-005]
- **Threading.**
  - Every `au_*` function MUST be safe to call from any thread.
  - The functions marked `[RT]` in the header MUST take no lock, make no allocation and make no system call. They may be called from a render callback.
  - All other functions MAY block, and MUST NOT be called from a render callback.
  - [AU-LIB-006]
- **Allocation.** The library MUST allocate the memory of a stream, voice slot or conversion scratch area at open or create time, never on a per-period path. [AU-LIB-007]
- **Several sessions.** A process MAY hold several sessions; a session's objects are independent of any other session's. [AU-LIB-008]

### 4.3 Handles, errors and records

**Handles** (charter P5):
- All handles are `uint64_t`, laid out as kind (bits 56–63), generation (bits 32–55, never 0) and slot index (bits 0–31), with the accessor macros `AU_HANDLE_*`.
- A handle that is stale, of the wrong kind, or `AU_HANDLE_NONE` MUST make the call return `AU_ERR_INVALID_HANDLE` and change nothing. The call counts it in the session's `stats` and does not crash. [AU-ABI-001]

**Errors** (charter P12):
- Functions return `AUerror`, or a count for the `[RT]` transfer calls.
- On every error, `au_error_detail()` MUST return a thread-local UTF-8 string naming the cause, valid until the next `au_*` call on that thread. For `AU_ERR_ADMISSION`, the string MUST contain the text of SC's `nd_sched_error_detail()` (SC-RT-020) verbatim. This is the same pattern as SC's: the code is authoritative and the string is for people and logs (spec-conventions.md §5). [AU-ABI-002]
- **The refusal record.** After `AU_ERR_ADMISSION`, `au_error_refusal()` MUST copy SC's `sc_rt_refusal` for that refusal (SC-RT-007) and return true, until the next `au_*` call on that thread; after any other result it MUST return false. A client uses `max_computation_ns` to choose a longer period or a smaller `compute_ns` instead of guessing. [AU-ABI-006]
- Asynchronous failures (a device lost, a service restart, a revoked capability) MUST be delivered as events, never as a signal, an abort or an unrecoverable error. [AU-ABI-003]

**Records.**
- Every record begins with `size`, and the version where it has one.
- The library MUST accept a caller-provided record whose `size` is at least the version 1 size, and MUST fill only the first `size` bytes of output records. [AU-ABI-004]
- Reserved fields MUST be written as zero and ignored on read. [AU-ABI-005]

### 4.4 The data path: ring and doorbell

#### 4.4.1 Establishing the ring

**Handing over the ring.** A read of `streams/N/ring` MUST return two Mach port names on one line, in the same way that window-protocol.md's `surface` returns its port:
- a send right to a memory entry for the stream's region;
- the receive right of the stream's doorbell port.

`audiod` MUST check the reader's audit token against the session that opened the stream, and refuse any other reader. [AU-RING-001]

**The doorbell port.** `audiod` MUST create the doorbell with a queue limit of 1, and MUST keep the only send right. [AU-RING-002]

**The region layout** (`nd_audio.h`, `AUring_hdr`):

| Offset | Size | Contents | Writer |
|---|---|---|---|
| 0 | 128 | line 0: magic `AUR1`, version, sizes, offsets, `capacity_frames`, `period_frames`, channels, direction, mode, lead, `ring_id` | `audiod`, before handing over; then immutable |
| 128 | 128 | line 1: `server_pos`, `state`, `doorbell_seq`, `contract_seq`, `anchor_seq`, `contract_gen`, anchor, `tick_host`, `deadline_host`, underrun and overrun counters | `audiod` only |
| 256 | 128 | line 2: `client_pos`, `watermark_frames`, `client_flags`, `client_callbacks`, `client_late`, `client_overbudget` | client only |
| 384 | 128 | reserved | — |
| 512 | 256 | `AUcontract` copy | `audiod`, under `contract_seq` |
| 1024 | capacity × channels × 4 | interleaved F32 samples at the device rate | the producer |

**Layout rules:**
- **Capacity.** `capacity_frames` MUST be `(lead_periods + 1) × period_frames`, where `period_frames` is the stream's period in device frames. A period therefore never wraps in the data area, and the callback can write straight into it. [AU-RING-003]
- **Positions.** Positions MUST be monotonic 64-bit frame counts that never wrap. The slot of position *p* is `p % capacity_frames`. [AU-RING-004]
- **Publishing.** The producer MUST write samples before it publishes its position with a release store, and the consumer MUST load the position with an acquire load before reading samples. Only the side named as writer writes each line. [AU-RING-005]
- **Seqlocks.**
  - The contract copy and the anchor MUST be published under their seqlocks: the writer makes the counter odd, writes, then makes it even with a release store.
  - Readers retry, at most 4 times, until they read the same even value before and after.
  - The single writer never waits.
  - [AU-RING-006]

#### 4.4.2 The doorbell

**What `audiod` sends.** At each tick where a stream period falls due, `audiod` MUST:
1. increment `doorbell_seq`;
2. then send one `AUdoorbell` message to the stream's doorbell with a send timeout of zero. A full queue means a doorbell is already pending, and it is not an error.

The mixer thread MUST NOT block on a doorbell send. [AU-RING-007]

**When a period falls due.** For output, a period is due when the mixer has consumed a period, freeing one period of space. For capture, it is due when the mixer has produced a period. A stream whose period is *k* mixer periods is due every *k* ticks.

**Extra doorbells.** `audiod` MUST also ring the doorbell:
- when the stream's state changes;
- when the contract generation changes;
- in queue mode with `AU_RCF_WANT_DOORBELL` set, when free space (output) or available frames (capture) first reaches `watermark_frames`, once per crossing.

[AU-RING-008]

**The fence** is the position, not the doorbell:
- The output client needs no message to tell `audiod` that a period is ready. The mixer reads `client_pos` at its tick, and the frames below it are ready.
- The client MUST treat the doorbell as a hint that coalesces, and MUST recompute its work from `server_pos`, `client_pos` and `state` after each receive, never from a count of messages. [AU-RING-009]

**Waiting in the one wait.** In queue mode, the application MAY add the port that `au_stream_doorbell` returns to its kqueue (`EVFILT_MACHPORT`, one of the one wait's sources under SC-WAIT-001). The toolkit loop does this, so readiness to write or read arrives in the one wait.

#### 4.4.3 Validation and isolation

`audiod` treats the whole region as untrusted:
- **Validating client fields.** At every use, `audiod` MUST validate `client_pos`, `watermark_frames` and `client_flags`:
  - a `client_pos` more than `capacity_frames` ahead of `server_pos` (output) or behind it (capture) is clamped;
  - the event is counted in `stats` as `ring_invalid`.
  
  [AU-RING-010]
- **No client offsets.** `audiod` MUST NOT read an offset, a size or a pointer from any client-written field. [AU-RING-011]
- **Sanitising samples.** Before mixing, `audiod` MUST replace NaN and infinite samples from a client ring with 0 and clamp finite samples to ±8.0, so that one client cannot poison the mix. [AU-RING-012]
- **Only the client's own data.** A ring MUST contain only its own stream's samples. The mix of other clients MUST never be mapped into a client, except for a monitor stream (§4.10), which needs `cap:audio:monitor`. [AU-RING-013]

### 4.5 Tier 1: streams

#### 4.5.1 Opening

`au_stream_open(session, params, &stream)` takes an `AUstream_params` record:
- direction, and mode (`AU_MODE_CALLBACK` or `AU_MODE_QUEUE`);
- the client format, rate, channels and channel mask;
- the requested `period_frames` (0 means the service default) and `lead_periods` (0 means 1 in callback mode, 8 in queue mode; at most 64);
- the declared `compute_ns` (0 means half the period);
- `device_uid` (all zero means the default);
- flags, render function, context and name.

**The negotiated period.**
- The period in device frames MUST be the smallest power of two that is at least the request converted to device frames, clamped to the device's `[min_period_frames, max_period_frames]`.
- Other streams never change a stream's period; §4.9.1 keeps the mixer period at or below it.
- With `AU_OPEN_EXACT_PERIOD`, a result different from the request MUST fail with `AU_ERR_PERIOD`. Otherwise the contract states the period that was granted.

[AU-STREAM-001]

**What open returns.** It MUST complete the 9P `open`, map the ring, and (in callback mode) create and admit the stream thread (§4.5.2) before it returns. When open returns `AU_OK`, the contract is readable and the stream is STOPPED. [AU-STREAM-002]

**Open in one piece.** When any step fails, open MUST release everything it created and return the error. Nothing of a half-opened stream remains. [AU-STREAM-003]

#### 4.5.2 Callback mode (pull)

**The stream thread.**
- The library MUST create one stream thread per callback stream, with intent `SC_INTENT_AUDIO` (SC-INT-002, SC-INT-003).
- The thread MUST call SC's `nd_rt_admit` on itself (SC-RT-001), which admits the reservation and joins the thread, with:
  - period = `period_ns`;
  - computation = `compute_ns` plus the library's measured per-period overhead;
  - constraint = `period_ns` less the mixer's safety margin. SC admits only C ≤ D ≤ T (SC-RT-003, test 1), so a lead of more than one period does not lengthen D; the later device deadline is stated per job instead (AU-STREAM-007, SC-RT-022).
- It MUST then report the admission with the `admitted` ctl message.

[AU-STREAM-004]

**Admission refused.** If `nd_rt_admit` refuses (SC-RT-001, SC-RT-006), `au_stream_open` MUST fail with `AU_ERR_ADMISSION`, with SC's reason in `au_error_detail()` (AU-ABI-002) and SC's refusal record in `au_error_refusal()` (AU-ABI-006). It MUST NOT run the callback at a lower scheduling class (F-215: never degrade silently). [AU-STREAM-005]

**The warm-up call.** Unless `AU_OPEN_NO_PRIME` is set, the stream thread MUST call the render function once with `AU_RF_PRIME` before the first real period, discarding its output and imposing no deadline. First use of classes, lazy globals and page faults then happen outside the deadline (S7 W8). [AU-STREAM-006]

**The loop.** In each period, the stream thread MUST:
1. receive the doorbell, with a timeout of four periods. The receive is a blocking point that ends the job, and the next wake is a release (SC-RT-021, SC-RT-017); the timeout gets the precision of SC-WAIT-005;
2. read `state`, `server_pos` and `deadline_host`;
3. if the thread woke later than `deadline_host` less `rt_constraint_ns` (compared on the SC clock), state `deadline_host`, converted with `au_host_to_ns`, as the job's deadline with `nd_rt_set_deadline` (SC-RT-022). A thread that woke on time makes no such call, because release + D already falls at or before `deadline_host`;
4. if `client_pos < server_pos` (output), set `client_pos = server_pos` and flag the next call `AU_RF_DISCONTINUITY`;
5. call the render function once per missing period, until `client_pos = server_pos + lead_periods × period`, passing `AUrender_info`: the position, `present_host`, `deadline_host`, the generation, the frame count, flags, and underruns;
6. publish `client_pos` after each call.

[AU-STREAM-007]

**Where the callback writes.** When no conversion is needed, the render function MUST receive a pointer into the ring's data area: zero copy. Otherwise it receives a pointer to scratch memory that was preallocated at open. [AU-STREAM-008]

**Counting the callback's timing.** When the callback returns after `deadline_host`, the library MUST increment `client_late`. When it takes longer than the admitted computation, the library MUST increment `client_overbudget`. It MUST NOT abort, skip or shorten later calls because of either. [AU-STREAM-009] The kernel's own accounting is separate and authoritative: at C the joined threads run at their fallback intent until the next release (SC-OVR-001), the thread can read its overruns and misses without blocking (SC-OVR-009), and persistent overruns demote the ticket (SC-OVR-003), which reaches the application as AU-EVENT-006 says.

**Helper threads.** SC's group handle is the stream thread's ticket: every thread joined to it shares its budget C per release (SC-RT-009), a thread joins at most one ticket (SC-RT-010), and only threads of the ticket's process can join (SC-RT-011). `au_stream_rt_group` MUST return that `sc_ticket` in `*out_group` (a raw `sc_ticket`, not an `AUhandle`), which DSP helper threads of the same process pass to `nd_rt_join` to share the stream's deadline (F-215, JUCE `AudioWorkgroup`). `AU_CAP_RT_GROUP` MUST be true exactly when `nd_sched_capabilities()` reports `SC_CAP_RT_GROUP`; when it is false, `au_stream_rt_group` MUST return `AU_ERR_UNSUPPORTED`. [AU-STREAM-010]

**Starting and stopping.**
- `au_stream_start` MUST make the stream RUNNING at the next tick.
- `au_stream_stop` MUST stop render calls within one period and make the stream STOPPED, keeping its ring and admission.

[AU-STREAM-011]

#### 4.5.3 Queue mode (push)

**The transfer calls.**
- `au_stream_write` (output) and `au_stream_read` (capture) MUST be non-blocking. They move as many whole frames as the ring allows, converting as they go, and return the count.
- `au_stream_write_all` and `au_stream_read_all` block until all `n` frames have moved or `deadline_host` has passed (`AU_ERR_TIMEOUT`). They wait on the doorbell, not by polling.
- [AU-STREAM-012]

**Arming.** A queue-mode output stream counts underruns only from the first frame written after `start`, and not after `drain` has begun. A queue stream that is started empty therefore does not count an underrun per tick. [AU-STREAM-013]

**Draining.** `au_stream_drain` MUST move the stream to DRAINING. When `server_pos` passes the last written frame, the library delivers `AU_EV_DRAINED` and the stream becomes STOPPED. [AU-STREAM-014]

**File mode** is the 9P-only variant for scripts and remote clients: `cat sound.raw > /n/sys/audio/streams/N/data`. `audiod` converts file-mode PCM on a 9P service thread, never on the mixer thread. It makes no timing guarantee beyond the contract's latency figures, and the performance targets of §7 do not apply to it.

#### 4.5.4 Format and rate conversion

**The ring format is fixed.** The ring MUST carry interleaved F32 at the device's mixer rate, with the stream's channel count. [AU-CONV-001]

**Conversion happens in the client.** `libndaudio` MUST convert sample format (`AU_FMT_S16`, `S32`, `S24_32`, `U8` ↔ `F32`) and rate on the client side:
- in callback mode, on the stream thread;
- in queue mode, in the calling thread.

A client's conversion cost is therefore charged to its own admitted budget, not to the mixer's. [AU-CONV-002]

**Channel mapping happens in `audiod`.** It MUST map channels between the stream's `channel_mask` and the device's with a fixed matrix:
- mono is duplicated to FL and FR;
- a missing channel is silent;
- LFE is not folded down.

[AU-CONV-003]

**Rate conversion.**
- It MUST use a fixed-cost resampler with a constant group delay, reported as `lat_convert_ns`.
- With rate conversion, callback frame counts MAY vary by ±1 around the nominal `period_frames`, and the contract flag `AU_CF_CONVERTING` MUST be set.
- With `AU_OPEN_NO_CONVERT`, a rate mismatch MUST fail with `AU_ERR_FORMAT`.

[AU-CONV-004]

#### 4.5.5 Stream gain

Stream gain (`au_stream_set_gain`, or `gain=` in ctl) is applied by the mixer, ramped linearly over one mixer period.

#### 4.5.6 Transitions (layout changes)

Some contract changes alter the ring's layout: a change of device rate, of period, or of channels, for example after the default device changes. On such a change, `audiod` MUST:
1. create the new ring;
2. set the old ring's `state` to RETIRED, and ring its doorbell.

[AU-STREAM-015]

**The transition on the client side.** The stream thread MUST perform the transition itself, outside the callback:
1. read `ring`;
2. map the new region;
3. call `nd_rt_admit` again with the new period, which re-admits the same ticket (SC-RT-019);
4. resume.

If the re-admission is refused, the old reservation stays in force (SC-RT-019) but no longer fits the new period, so the stream MUST end with `AU_EV_STREAM_LOST` carrying `AU_ERR_ADMISSION`. The library MUST deliver `AU_EV_CONTRACT` with `AU_CHG_LAYOUT`, and the transition MUST NOT depend on the application dispatching events. [AU-STREAM-016]

**No stale samples.** During a transition, the device MUST output silence for the stream, never stale ring content. A transition that has not finished within 50 ms of the RETIRED doorbell MUST end with `AU_EV_STREAM_LOST`. [AU-STREAM-017]

### 4.6 The contract record

`AUcontract` is 256 bytes, version 1. It is the single statement of a stream's timing and identity (F-216, charter P11).

| Field | Meaning |
|---|---|
| `size`, `version`, `flags` | record size (256), 1, and `AU_CF_*` |
| `stream`, `generation` | the stream handle; the change counter (never 0) |
| `device`, `device_uid` | the device handle (volatile), and the stable identity (§4.11) |
| `rate`, `device_rate` | client frames per second; the mixer rate of the device |
| `period_frames`, `mixer_period_frames`, `period_ns` | the callback period in client frames; the mixer period in device frames; the callback period in ns, which is the admitted period |
| `channel_mask`, `channels`, `format`, `direction`, `mode`, `lead_periods`, `ring_frames` | the stream's shape |
| `change` | the `AU_CHG_*` bits that changed from the previous generation |
| `latency_ns` | the total latency from the callback's buffer to the terminal (output), or from the terminal to the buffer (capture) |
| `lat_client_ns` | `lead_periods × period_ns`: ring lead |
| `lat_convert_ns` | the resampler's group delay |
| `lat_mixer_ns` | one mixer period: mixing to device ring (output), or the capture tick (input) |
| `lat_device_ns` | driver and DMA buffering, including the safety offset the driver reports |
| `lat_hw_ns` | codec, converter and transport, for example Bluetooth, as the driver reports it |
| `anchor_pos`, `anchor_host`, `ticks_per_frame_q32` | client frame `anchor_pos` is at the terminal at host time `anchor_host`; the rate is in host ticks per client frame, Q32.32, corrected for drift |
| `timebase_numer`, `timebase_denom` | `mach_timebase_info`, copied, so the render path needs no lazy global (S7 W8) |
| `rt_period_ns`, `rt_computation_ns`, `rt_constraint_ns` | the stream thread's admission, as SC granted it (SC-RT-001); all 0 while the ticket is demoted or revoked (AU-EVENT-006) |
| `route`, `state` | `AU_ROUTE_*` and `AUstate` |

**Requirements on the record:**
- **The sum.** `latency_ns` MUST equal the sum of the five `lat_*` components. `audiod` is the only authority for this sum; the library and applications MUST NOT add their own terms (F-216: three projects summed three different subsets). [AU-CONTRACT-001]
- **The mapping from positions to host time.** The presentation time (output) or capture time (input) of client frame *p* MUST be `au_frame_to_host(c, p)`, computed from the anchor, within ±1 frame at the terminal. `AUrender_info.present_host` MUST equal `au_frame_to_host` of `info.pos`. [AU-CONTRACT-002]
- **Keeping the anchor fresh.** `audiod` MUST update the ring anchor at every tick, from the device position and host timestamp that the driver reports. `au_stream_contract` MUST return the most recent anchor, converted to client frames. Anchor updates are not contract changes and MUST NOT change `generation`. [AU-CONTRACT-003]
- **What counts as a change.** Every change to any other field, except `state` changes caused by start and stop, MUST increase `generation`, set `change`, update the ring's copy and the `contract` file, and produce one `AU_EV_CONTRACT`. [AU-CONTRACT-004]
- **Consistent reads.** A read of `contract`, and `au_stream_contract`, MUST return one consistent generation, never a mix of fields from two generations. [AU-CONTRACT-005]
- **The mixer contract.** `au_mixer_contract` returns the mixer's contract for the session's voice path, covering the default output. `lat_client_ns` is then the command latency: one mixer period. [AU-CONTRACT-006]
- **A migrating stream keeps its handle.** A stream following the default device MUST keep its handle across device changes. The change is a contract change (`AU_CHG_DEVICE`), never an error returned from a later call (F-216). [AU-CONTRACT-007]

### 4.7 Events and the one wait

`AUevent` is a 32-byte record: type, flags, `arg32`, object, value, and host time. The version 1 types are listed in `nd_audio.h`.

- **Event order.** `audiod` MUST write each session's events to its `events` file in the order the changes happened. For any one object, the order MUST match its generations. [AU-EVENT-001]
- **Coalescing contract changes.** Under a burst, `AU_EV_CONTRACT` MAY be coalesced, but the last generation MUST always be delivered. [AU-EVENT-002]
- **Delivery time.** A contract change MUST become readable on the session descriptor within one mixer period plus 1 ms of `audiod` applying it. [AU-EVENT-003]
- **Device events.** A session MUST receive `AU_EV_DEVICE_ADDED`, `AU_EV_DEVICE_REMOVED` and `AU_EV_DEFAULT_CHANGED` for every device it may use (`cap:audio:play` for outputs, `cap:audio:capture` for inputs). [AU-EVENT-004]
- **Readiness through SC.** The session descriptor MUST work with SC's one wait: kqueue readiness of nd9p stream files (SC-WAIT-001, SC-WAIT-002, SC-9P-001, SC-9P-006, SC-9P-008). The library MUST NOT require a thread of its own to deliver events. [AU-EVENT-005]
- **Real-time notices.** For each callback stream, the library MUST register the ticket's notice source (`nd_rt_watch`, SC-OVR-006) so that a pending notice makes the session descriptor readable, and `au_session_events` MUST take each notice (`nd_rt_notice_take`) and return it as one `AU_EV_RT_NOTICE` carrying the notice kind and its coalesced count (SC-OVR-005). A demotion or revocation (SC-OVR-003, SC-OVR-004) MUST also be a contract change: `AU_CHG_ADMISSION`, with the `rt_*` fields 0. The stream keeps running at the thread's fallback intent; it is never stopped silently. [AU-EVENT-006]

### 4.8 Underrun and overrun accounting

**Underruns (output).** At a tick that consumes stream period `[s, s+P)` while the stream is RUNNING (and, in queue mode, armed), `client_pos < s + P` is an underrun. `audiod` MUST then:
1. mix the available frames `[s, client_pos)` and silence for the rest;
2. increment `underruns` by 1 and `underrun_frames` by the number of frames missing;
3. advance `server_pos` to `s + P`.

Output positions stay linear in time. [AU-XRUN-001]

**Overruns (capture).** At a capture tick, when the ring has no room for the new period, `audiod` MUST:
1. discard the new period;
2. increment `overruns`;
3. leave `server_pos` unchanged.

It MUST then update the anchor so that it maps positions to time correctly across the gap. The library flags the next buffer `AU_RF_DISCONTINUITY`. [AU-XRUN-002]

**What an underrun is not.** The counters count ticks. A callback that returned late but was still written before its tick is not an underrun; it is counted as `client_late`. [AU-XRUN-003]

**Underrun events.** `AU_EV_UNDERRUN` and `AU_EV_OVERRUN` MUST be produced by `audiod`'s service thread from the counters, at most once per stream every 50 ms, carrying the cumulative count and the count since the last event. The mixer thread never produces events itself. [AU-XRUN-004]

**Where the counters appear.** `AUstream_stats`, `streams/N/stats` and `AUrender_info.underruns` MUST all report the same cumulative counters. [AU-XRUN-005]

### 4.9 Tier 2: the system mixer and voices

#### 4.9.1 The mixer

**One thread per output.** `audiod` MUST run one mixer thread for each active output device. The thread is admitted, and joined, through SC's `nd_rt_admit_ex` with `SC_RT_F_SYSTEM`, drawing on the system share (SC-RT-001, SC-RT-004, SC-SEC-002), with:
- period = the mixer period;
- computation = the calibrated worst case for the device's configured stream and voice limits;
- constraint = the mixer period less the driver's safety offset.

[AU-MIX-001]

**The mixer period.**
- The mixer period MUST be the smallest period among the device's RUNNING streams, capped at `period-max` (default 256 frames at 48 kHz), and never below the device minimum.
- Because stream periods are powers of two (AU-STREAM-001), every stream's period is a whole number of mixer periods.
- The mixer period changes only when a stream starts or stops, and only at a tick boundary, with no frame dropped or repeated.
- A voices-only mixer runs at `period-max`.

[AU-MIX-002]

**Order of work at each tick.** At each tick, the mixer MUST, in this order:
1. read the device position and timestamp;
2. drain the command rings of the sessions (at most `cmd_capacity` commands per session);
3. consume the stream periods that are due;
4. mix the voices;
5. apply the master gain;
6. write the device period;
7. publish positions, anchors and counters;
8. ring the doorbells.

[AU-MIX-003]

**T2 discipline.** The mixer thread MUST take no lock, make no allocation, and make no system call other than the doorbell sends, the driver family's period handshake (§4.15) and the late-wake `nd_rt_set_deadline` of AU-MIX-008. This is checked at compile time with the T2 annotations (language-policy.md §2). [AU-MIX-004]

**Limits instead of degradation.** `audiod` MUST refuse a stream start or a voice beyond the configured limits (`AU_ERR_LIMIT`, or `AU_VEND_LIMIT`), rather than exceed its admitted computation. [AU-MIX-005]

**Re-admission.** When the mixer period changes, `audiod` MUST re-admit the mixer thread (SC-RT-019) before the new period takes effect. If re-admission fails, the old period stays in force and the start that triggered the change fails with `AU_ERR_ADMISSION`. [AU-MIX-006]

**Mixer notices.** `audiod` MUST register each mixer ticket's notices in the one wait of a service thread (`nd_rt_watch`, SC-OVR-006), never on the mixer thread, and MUST count overruns and misses in `devices/UID/stats` (`rt_overruns`, `rt_misses`). A demotion or revocation (SC-OVR-003, SC-OVR-004) MUST produce a `log` line and show as `rt_state=demoted` or `rt_state=revoked` in those `stats`; the mixer keeps mixing at its fallback intent until it is re-admitted (§10, item 11). [AU-MIX-007]

**Mixer deadline.** When the mixer thread wakes later than the device period's deadline less its admitted constraint, it MUST state the device deadline as its job deadline with `nd_rt_set_deadline` (SC-RT-022), so that EDF orders it against the client stream threads by the device's real deadline. On time, it makes no such call. [AU-MIX-008]

#### 4.9.2 Buffers

`au_buffer_create` copies PCM into `audiod`, through `mixer/buffers/N/data` followed by `seal`.
- **Storage.** `audiod` stores the samples as F32 at the buffer's own rate, and converts the format at `seal` on a service thread.
- **Immutability.** A buffer MUST be immutable after `seal`. [AU-VOICE-001]
- **Release.** `au_buffer_release` MUST NOT cut off voices that are playing it. The samples are freed when the last voice ends. [AU-VOICE-002]
- **Quota.** A session's buffer memory is limited by a quota (default 64 MiB); `au_buffer_create` beyond it returns `AU_ERR_LIMIT`.

#### 4.9.3 Voices

**The session region** (`AUsession_hdr`) is mapped from `mixer/region`. It holds:
- the mixer contract copy;
- a multi-producer command ring of `cmd_capacity` 64-byte `AUvcmd` records;
- `voice_slots` 32-byte `AUvstate` records.

**Voice calls without a round trip.**
- `au_voice_play`, `au_voice_stop`, `au_voice_set_gain` and `au_voice_set_loop` MUST complete without any 9P message or system call.
- The library MUST allocate the voice slot, claim a command slot with an atomic fetch-add on `cmd_tail`, fill it, and publish it by storing `seq = claim + 1` with a release store.
- When the ring is full, the call returns `AU_ERR_BUSY`.
- [AU-VOICE-003]

**When a voice starts.**
- Without `AU_VF_AT`, a voice MUST start on the first frame of the tick that drains its command.
- With `AU_VF_AT`, it MUST start at the device frame whose host time is `at_host`, exact to the sample. It starts at once if `at_host` has passed, and it never skips ahead to catch up.
- [AU-VOICE-004]

**Gain and stop.** Gain changes MUST ramp linearly over one mixer period, and `stop` MUST ramp to zero over one mixer period before the voice ends (no clicks). [AU-VOICE-005]

**Looping.**
- With `AU_VF_LOOP`, the voice plays from 0 to `loop_end` (0 means the end of the buffer), then repeats `[loop_start, loop_end)` until it is stopped or looping is turned off.
- Turning looping off lets the voice play on to the end of the buffer.
- `loop_start ≥ loop_end`, or a region outside the buffer, MUST be rejected: `AU_ERR_INVALID_ARG` from the library, and `cmd_rejected` in `audiod`.
- [AU-VOICE-006]

**Resampling and channels.** Voice buffers whose rate differs from the device's MUST be resampled by the mixer at a fixed cost per voice. Channels are mapped as in AU-CONV-003. [AU-VOICE-007]

**Voice state.**
- `audiod` MUST keep each slot's `AUvstate` current at every tick: `gen`, `state`, `pos`, `buffer` and `end_reason`, with `gen_end` written last.
- `au_voice_state` returns a snapshot for which `gen == gen_end`, and returns `AU_ERR_INVALID_HANDLE` if the generation has moved on.
- [AU-VOICE-008]

**Voice end.** When a voice ends, whether it finished, was stopped, lost its buffer, hit the limit, or the service restarted, `audiod` MUST deliver one `AU_EV_VOICE_END` with the reason. The library MUST NOT reuse the slot until it has observed the ended state. [AU-VOICE-009]

**Stalled producers.** A command slot claimed but not published within 2 ticks MUST be skipped and counted in `cmd_rejected`. A stalled or malformed producer can only affect its own session. [AU-VOICE-010]

**The same through 9P.** AU-NS-008 applies: the 9P `voices/N/ctl` verbs have the same effect as the ring commands.

Limits: each session has `voice_slots` voice slots (default 64, reported as `AU_CAP_VOICES`), and each device has a system-wide mixing limit (default 256).

### 4.10 Capture streams

**Opening.** Capture streams open with `direction = AU_DIR_INPUT`, in callback or queue mode, and follow the rules of §4.5, with producer and consumer swapped.

**Where capture data comes from.**
- For each active input device, `audiod` MUST run one capture tick per mixer period. The capture tick copies the device period into every RUNNING capture stream's ring and rings the doorbells.
- Each capture stream receives its own copy.
- [AU-CAPTURE-001]

**Capture latency.** For capture, `latency_ns` is the time from the input terminal to the moment the frame is available to the callback. `present_host` is the time the buffer's first frame was at the input terminal. [AU-CAPTURE-002]

**Monitor streams.** A monitor stream (`AU_OPEN_MONITOR`, `device_uid` = an output device) captures that output's mix after the master gain. It MUST require `cap:audio:monitor`, and it is always pinned to that output. [AU-CAPTURE-003]

### 4.11 Devices, enumeration and hotplug

**Where devices come from.** `audiod` learns about devices from the audio driver family (§4.15).
- **Enumeration.** `au_devices` and `devices/index` MUST list every present device, plus the always-present virtual device `null`. `null` is an output that discards samples, clocked by the host timer, so that a default stream can always open (§4.12). [AU-DEV-001]
- **Stable identity.** A device's `uid` MUST be stable across reboots and replugging on the same port. It is derived from the bus kind, vendor, product and serial number, or, where there is no serial number, from the bus path. A device that returns MUST get its old uid and a new handle. [AU-DEV-002]
- **Hotplug timing.** A device MUST appear in the listing, with `AU_EV_DEVICE_ADDED` delivered, within 500 ms of the driver registering it. Removal is delivered with `AU_EV_DEVICE_REMOVED` within one tick of the driver reporting it. [AU-DEV-003]
- **The generation.** `AUdevinfo.generation` MUST increase whenever any field of the record changes. [AU-DEV-004]
- **Port changes.** A port change on the same device (a headphone jack inserted, say) MUST be reported as `AU_CHG_ROUTE` on the streams using that device, with the new `lat_hw_ns`. It is not a device change. [AU-DEV-005]

### 4.12 Routing policy

The routing policy is deliberately small. There is one role (F-216 found three roles on Windows, with projects choosing differently).

- **One default per direction.** There MUST be exactly one default output and one default input. No stream kind, category or role changes which device is chosen. [AU-ROUTE-001]
- **Following and pinning.**
  - A stream opened on the default (all-zero uid, without `AU_OPEN_PIN`) MUST follow the default, migrating on the server side within 100 ms of a change.
  - A stream opened on a uid, or pinned later with `au_stream_pin`, stays on that device.
  - [AU-ROUTE-002]
- **How the default is chosen.** Choosing it, at boot and after any device is added or removed, MUST use this order:
  1. the first present device in the user's `prefer` list;
  2. otherwise, the most recently added present device whose kind is in `autoswitch` (default: `usb bluetooth`);
  3. otherwise, a built-in device;
  4. otherwise, `null`.
  
  [AU-ROUTE-003]
- **A pinned device leaving.** When a stream's pinned device leaves, the stream MUST become DETACHED: `AU_CHG_STATE`, no callbacks, queue writes return 0. When the same uid returns, it MUST reattach and continue. [AU-ROUTE-004]
- **Changing the policy.** Writes to `routes`, `au_set_default` and the `default-*` ctl keys MUST need `cap:audio:device`. [AU-ROUTE-005]

The `routes` file holds these lines: `default out UID`, `default in UID`, `prefer out UID…`, `prefer in UID…`, and `autoswitch KIND…`. Reading it returns the policy in force.

### 4.13 The render callback: T2 rules

The render callback runs on the stream thread, inside an admitted deadline. It is on a T2 path (language-policy.md §2; charter §3 Audio, §4).

**Rules for every callback, in any language:**
- It MUST NOT take a lock, allocate, block, make a system call, perform file or 9P I/O, or wait for another thread. [AU-T2-001]
- It MUST communicate with other threads only through atomics and lock-free single-producer, single-consumer rings set up before `start` (charter §4). The only exceptions are the `[RT]` calls of `nd_audio.h`. [AU-T2-002]
- **The library's own path.** From the doorbell receive to the callback entry, the library's code on the stream thread MUST satisfy the rules above. In Swift, it MUST carry the no-lock and no-allocation annotations of the pinned toolchain (`@_noLocks`, `@_noAllocation`) and compile with the performance diagnostics enabled. The only exceptions are the documented `@convention(c)` entry thunk, the clock reads (S7 W8), and the late-wake `nd_rt_set_deadline` call of AU-STREAM-007 step 3. [AU-T2-003]

**The Swift subset.** S7 found what the pinned checker (Swift 6.4) accepts in a `@_noLocks` render. An application that writes its render in Swift and wants the compiler to check it SHOULD mark it `@_noLocks` and `@_noAllocation`, build it with optimisation, and keep to this subset:

| Rule | Why (S7 W8) | What to do instead |
|---|---|---|
| **No libm** (`sinf`, `powf`, `expf`, …) | any C call is opaque to the checker | compute coefficients off the audio thread and publish them as atomic bit patterns; use tables built at init; `squareRoot()`, arithmetic and `rounded()` are fine |
| **No first use of a class** | first use realises class metadata, which allocates and takes runtime locks (the probe registered 6 allocations) | instantiate everything before `start`; the `AU_RF_PRIME` call (AU-STREAM-006) is the place to touch every path |
| **No `&&` or `\|\|`** | the right operand is an autoclosure that captures `self`, which the checker rejects as reference counting; the diagnostic does not name the operator | comma conditions (`if a, b`), nested `if`, `guard … else { continue }` |
| No capturing closures, existentials, `Array`/`String` mutation, or lazy `static let` | allocation, reference counting, `swift_once` | `~Copyable` structs held inline, `MutableSpan`, `InlineArray`, values copied at open |
| Pass state borrowed | reaching a class-held ring retains it | rings as `~Copyable` stored properties; take state as a borrowed parameter |

These subset rows record the limitations of the pinned toolchain. Each row is withdrawn by a new version of this document once the checker accepts the construct, for example through an allow-list of C functions safe for real-time use. Their status is tracked in §10.

**The C trampoline.**
- `AUstream_params.render` is a plain C function pointer, and the stream thread calls it directly, with nothing in between. A render written in C, in a file carrying `NeoDarwin-Language: performance: real-time audio render`, therefore runs with no Swift frames.
- This is the documented option for renders that cannot meet the Swift subset, for example DSP code that needs libm.
- NeoDarwin's libm functions MUST be free of locks, allocation and `errno` writes on finite inputs, so that C renders may call them (§10, item 3).
- The toolkit's Swift layer MUST offer an `open` variant that takes such a C function and context, and installs it without a Swift thunk.
- [AU-T2-004]

**Runtime checking in debug builds.** In debug builds, `libndaudio` MUST count lock acquisitions and allocations on each stream thread through NeoDarwin's first-party allocator and lock hooks, and report them as `rt_violations` in `AUstream_stats`. Release builds report 0 without counting. [AU-T2-005]

### 4.14 What this document needs from the scheduling contract

These were proposals against SC (spec-conventions.md §7: the lower layer changes first). SC version 1 specifies all eight (SC §4.9). The requirements above cite the SC ids in the last column and do not restate them.

| # | Needed | Used by | Specified by (SC) |
|---|---|---|---|
| S1 | `nd_rt_admit(period, computation, constraint)` that a thread applies to itself. It is available without privilege, within a per-user budget. It refuses synchronously with a reason, and a thread can call it again to change its period. | AU-STREAM-004/005/016, AU-MIX-001/006, AU-ABI-002/006 | SC-RT-001, SC-RT-003, SC-RT-004, SC-RT-006, SC-RT-007, SC-RT-019, SC-RT-020, SC-SEC-001. Refusal is a code plus the `sc_rt_refusal` record, with a thread-local `nd_sched_error_detail()` string. |
| S2 | An admitted thread may block in a Mach message receive (the doorbell) with a timeout and still be treated as periodic: the wake is external and the deadline is stated. | AU-STREAM-007 | SC-RT-017, SC-RT-021; timeout precision SC-WAIT-005 |
| S3 | An absolute deadline per period, stated by the thread (`deadline_host` from the device clock), for EDF-style ordering between the mixer thread and client stream threads. | AU-STREAM-007, AU-MIX-008, §3.3 | SC-RT-018, SC-RT-022; one clock SC-TIME-001, SC-TIME-002 |
| S4 | A group handle that helper threads can join to share an admitted thread's deadline (the `os_workgroup` analogue). | AU-STREAM-010 | SC-RT-009, SC-RT-010, SC-RT-011; capability `SC_CAP_RT_GROUP` (SC-CAP-001) |
| S5 | kqueue readiness of 9P fids, and `EVFILT_MACHPORT`, in the one wait. | AU-LIB-003, AU-NS-009, AU-EVENT-005, AU-EVENT-006, §4.4.2 | SC-WAIT-001, SC-WAIT-002, SC-9P-001, SC-9P-006, SC-9P-008, SC-9P-009; latency SC-PERF-004 |
| S6 | `audiod`'s mixer admissions charged to a system budget, and client stream threads charged to the user's budget. | AU-MIX-001 | SC-RT-004, SC-SEC-001, SC-SEC-002 (`SC_RT_F_SYSTEM`, entitlement `sched.rt.system`) |
| S7 | What happens when a thread exceeds its admitted computation, stated and observable (for example demotion, with a counter), never silent. | AU-STREAM-009, AU-EVENT-006, AU-MIX-007 | SC-OVR-001 to SC-OVR-006, SC-OVR-009 |
| S8 | Per-thread admitted parameters and deadline misses, readable as text (F-215 observability). | §7 measurements | SC-OVR-008 (`/n/sys/sched/rt/tickets`) |

### 4.15 The driver boundary

Audio hardware drivers are dexts (drivers.md §1: audio is a dext family), written against an `NDAudioFamily` in `NDDriverKit` (P3-05). Candidate drivers are virtio-snd for QEMU, USB Audio Class 1 and 2, HD Audio, and I2S codecs on SBCs. The family interface is specified by its own epic. `audiod` requires the following of it:
- **Driver requirements.** Every audio driver MUST provide:
  - a device DMA ring that `audiod` can map;
  - a position and host-timestamp pair at least once per mixer period;
  - its safety offset and hardware latency, in frames;
  - its supported rates and period range;
  - port and jack changes as notifications.
  
  `audiod` is the only client of the family's user client. [AU-DRV-001]
- **Counters.** Drivers publish their counters under `/n/sys/dev/<path>/` (drivers.md §3, rule 4). `audiod` MUST NOT duplicate them. It publishes its own counters in `devices/UID/stats`. [AU-DRV-002]
- **Driver crashes.** A driver crash or restart (drivers.md §5) MUST appear to clients as the device leaving and returning. It follows AU-ROUTE-002 and AU-ROUTE-004, and never makes `audiod` exit. [AU-DRV-003]

### 4.16 The PulseAudio-protocol door

`pulsedoor` is a separate process. It serves the PulseAudio native protocol on `$XDG_RUNTIME_DIR/pulse/native`, and advertises that socket through `PULSE_SERVER`, so that unmodified `libpulse` clients work (q5-compat §7 item 5). It is an ordinary `libndaudio` client.

**Protocol version.** `pulsedoor` MUST announce protocol version 35 and accept clients from version 13 upward. It uses the socket transport only: SHM and memfd negotiation are declined, and srbchannel is not offered. [AU-PULSE-001]

**The command subset:**

| Area | Commands | Maps onto |
|---|---|---|
| Connection | `AUTH` (the cookie is ignored; the peer's audit token is used), `SET_CLIENT_NAME`, `GET_SERVER_INFO`, `SUBSCRIBE` (sink, source, sink_input, source_output, server) | a session opened for the peer; device events → subscription events |
| Introspection | `GET_SINK_INFO[_LIST]`, `GET_SOURCE_INFO[_LIST]`, `GET_SINK_INPUT_INFO[_LIST]`, `GET_SOURCE_OUTPUT_INFO[_LIST]`, `GET_CLIENT_INFO[_LIST]`, `GET_CARD_INFO_LIST` (one card per device, one profile, one port), `LOOKUP_SINK`, `LOOKUP_SOURCE` | sinks = output devices; sources = input devices plus one `.monitor` per sink (listed only with `cap:audio:monitor`); `@DEFAULT_SINK@` and `@DEFAULT_SOURCE@` = the defaults |
| Playback | `CREATE_PLAYBACK_STREAM`, `DELETE_PLAYBACK_STREAM`, data memblocks, `REQUEST`, `CORK_PLAYBACK_STREAM`, `FLUSH_PLAYBACK_STREAM`, `TRIGGER`, `PREBUF`, `DRAIN_PLAYBACK_STREAM`, `GET_PLAYBACK_LATENCY`, `SET_PLAYBACK_STREAM_BUFFER_ATTR`, `SET_PLAYBACK_STREAM_NAME`, `UPDATE_PLAYBACK_STREAM_SAMPLE_RATE`; server notices `STARTED`, `UNDERFLOW`, `OVERFLOW`, `PLAYBACK_STREAM_MOVED`, `PLAYBACK_STREAM_SUSPENDED` | a queue-mode output stream |
| Record | `CREATE_RECORD_STREAM`, `DELETE_RECORD_STREAM`, `CORK_RECORD_STREAM`, `FLUSH_RECORD_STREAM`, `GET_RECORD_LATENCY` | a queue-mode capture stream (or a monitor stream) |
| Volume | `SET_SINK_INPUT_VOLUME`, `SET_SINK_INPUT_MUTE`, `SET_SOURCE_OUTPUT_VOLUME`, `SET_SOURCE_OUTPUT_MUTE` | stream gain (the channel volumes are averaged in version 1) |
| Sample cache | `UPLOAD_STREAM`, `FINISH_UPLOAD_STREAM`, `PLAY_SAMPLE`, `REMOVE_SAMPLE`, `GET_SAMPLE_INFO[_LIST]` | Tier 2: a buffer, and a voice per `PLAY_SAMPLE` (libcanberra event sounds) |
| Defaults | `SET_DEFAULT_SINK`, `SET_DEFAULT_SOURCE` | routes; needs the peer's `cap:audio:device` |
| Move | `MOVE_SINK_INPUT`, `MOVE_SOURCE_OUTPUT` | `pin` on the stream; the peer must own it, or hold `cap:audio:mixer` |

**Unsupported commands.** Every other command, including module loading, `SUSPEND_*`, `SET_*_PORT`, `SET_CARD_PROFILE`, extensions, and passthrough or compressed formats, MUST get `PA_ERR_NOTSUPPORTED`, never a crash or a silent success. [AU-PULSE-002]

**Formats.** Accepted sample formats are `u8`, `s16le`, `s16be`, `s24le`, `s24-32le`, `s32le` and `float32le`, with up to 8 channels and the Pulse channel map translated to `AU_CH_*`. Any other format is refused with `PA_ERR_NOTSUPPORTED`. [AU-PULSE-003]

**Buffer attributes.** `pulsedoor` MUST map `buffer_attr` as follows:
- `tlength` becomes the door's target fill (ring lead plus its own queue);
- `minreq` becomes the doorbell watermark that drives `REQUEST`;
- `prebuf` becomes the start threshold before the stream is armed;
- `maxlength` becomes the door's queue bound.

It MUST report the attributes it granted. [AU-PULSE-004]

**Latency from the contract.** `GET_PLAYBACK_LATENCY` and `GET_RECORD_LATENCY` MUST be computed from the stream's contract and ring positions:
- the sink latency is the door's queued frames plus `latency_ns`;
- the read and write indices are the positions.

A Pulse client therefore gets the same truthful figure as a native one (F-216: mpv's `get_delay_hackfixed`). [AU-PULSE-005]

**Acting for the peer.** `pulsedoor` holds `cap:audio:door`, and MUST open each client's session on behalf of the peer's audit token. `audiod` MUST then grant that session the peer's audio capabilities, never the door's own. [AU-PULSE-006]

**ALSA** clients reach the door through the vendored `alsa-plugins` `pulse` PCM plugin, configured as ALSA's default device. There is no separate ALSA path.

## 5. Versioning and capabilities

**Version constants.** `AU_API_VERSION` is 1. `audiod` reports its protocol version as `AU_CAP_SERVICE_VERSION`, and in the first token of the service `ctl` line.

**Capability queries.**
- `au_query(session, cap, &value)` MUST return true, with a value, for every `AUcap` it knows, and false for unknown ids.
- The `caps` file MUST list the same capabilities by name (`stream.callback 1`, `voices 64`, …).
- [AU-VER-001]

**Version 1 capabilities.** `API_VERSION`, `SERVICE_VERSION`, `STREAM_CALLBACK`, `STREAM_QUEUE`, `CAPTURE`, `CONVERT_RATE`, `VOICES`, `VOICE_AT`, `HOTPLUG`, `FOLLOW_DEFAULT`, `MONITOR`, `MIN_PERIOD`, `MAX_STREAMS` and `DEVICE_CONTROL` are defined. `RT_GROUP` depends on SC (AU-STREAM-010).

**Reserved capabilities.** `DUPLEX` and `VOICE_PITCH` are reserved and MUST report false in version 1. [AU-VER-002]

**Additive evolution** (charter P13):
- Later versions add calls, event types, `AU_CHG_*` bits, capability ids and ctl keys.
- Records grow only into reserved space or through a larger `size`.
- The meaning of an existing field, event or verb is never changed.
- A client MUST ignore event types and `AU_CHG_*` bits it does not know. [AU-VER-003]

**Header copies.** When P4-18's implementation starts, `audiod` and `libndaudio` copy `nd_audio.h`, and a test compares each copy with this reference (spec-conventions.md §1).

## 6. Security and capabilities

**Tokens in the attach.** Capabilities are `keyd` tokens carried in the 9P attach (namespaces-agents.md §4). A namespace without `/n/sys/audio` has no audio (AU-NS-002). `audiod` MUST evaluate the capabilities once per attach and keep them for the session, so that no per-period check exists. [AU-SEC-001]

**Real-time service is not an audio capability.** A client's stream thread is admitted within its user's budget with no capability, entitlement or daemon (SC-SEC-001, SC-RT-004). `audiod` holds the manifest entitlement `sched.rt.system` for its mixers' `SC_RT_F_SYSTEM` admissions (SC-SEC-002). namespaces-agents.md does not yet list this entitlement, the `/n/sys/audio` tree or the `cap:audio:*` tokens (§10, item 12).

| Capability | Grants |
|---|---|
| `cap:audio:play` | output streams, buffers and voices on the mixer; listing output devices |
| `cap:audio:capture` | input streams; listing input devices |
| `cap:audio:monitor` | monitor streams: the mix of an output (loopback, screen recording) |
| `cap:audio:device` | the defaults, `routes`, device `ctl` (rate, `period-max`, gain, mute), and the master gain |
| `cap:audio:mixer` | seeing and setting gain and mute on other sessions' streams, and moving them (volume-control applications) |
| `cap:audio:door` | opening sessions on behalf of a peer's audit token (`pulsedoor` only) |

**Enforcement:**
- **Checks in `audiod`.** Every operation MUST be refused with `AU_ERR_PERMISSION` (9P error `permission denied`) unless the session holds the capability in the table. [AU-SEC-002]
- **Capture consent.** `cap:audio:capture` and `cap:audio:monitor` MUST NOT be granted by a manifest alone. They require a user grant through `/n/sys/keys/grants` (namespaces-agents.md §5). [AU-SEC-003]
- **Capture visibility.**
  - Every capture or monitor stream MUST appear in `state` while it runs, with its session's application identity.
  - Its start and stop MUST each produce a `log` line and an audit record carrying the token id.
  - The desktop's capture indicator reads this.
  - No capture can be hidden from `state`.
  - [AU-SEC-004]
- **Revocation.** `audiod` MUST watch `/n/sys/keys/revoked`. Streams opened under a revoked token MUST stop within 100 ms, and the session receives `AU_EV_REVOKED`. This matters because the ring path makes no 9P request that `nsd` could re-check. [AU-SEC-005]
- **Handing out shared memory.** Rings and session regions MUST be handed out only to the session that owns them (AU-RING-001). Doorbell send rights MUST never leave `audiod` (AU-RING-002), so no client can wake or starve another client's thread. [AU-SEC-006]
- **Isolation.** The isolation of §4.4.3 (AU-RING-010 to AU-RING-013) is part of the security contract: a malicious client can only corrupt its own stream.

## 7. Performance contract

**The reference system.** The measurements use the hardware-in-the-loop reference board of drivers.md §5, with a USB Audio Class 2 interface as the device, and a loopback cable from its output to its input for the latency tests.

**The stated load** runs for a 600 s soak:
- the S7 **synth-ui** prototype ported to NeoDarwin: 8 PolyBLEP voices and a filter, with the UI animating the scope and meter at the display rate, the window resized every 1.5 s, and the S7 chord pattern with a cutoff sweep;
- plus a parallel build at background intent on every core (`bazel build` of the kernel, `--jobs` = the core count);
- plus a second, unrelated queue-mode stream at 48 kHz, fed with 20 ms writes.

The S7 synth-ui prototype on NeoDarwin is the conformance vehicle for the targets below (roadmap P4-18 exit).

| Id | Target (requirement) | Test |
|---|---|---|
| **P1** | A callback stream opened with `period_frames = 128`, 48 kHz, F32 stereo MUST report `period_frames = 128`, `period_ns = 2 666 667 ± 1`, `mixer_period_frames = 128`, and MUST run the callback exactly once per 2.667 ms period (no bursts: the p99 gap between successive callback entries is ≤ 1.5 × the period). [AU-PERF-001] | AU-T-060 |
| **P2** | Under the stated load, the stream in P1 MUST record **0 underruns** over the 600 s soak (225 000 periods). The same holds for the mixer: 0 late ticks. [AU-PERF-002] | AU-T-061 |
| **P3** | Measured latency MUST equal reported latency. Using the loopback cable, the measured round trip (an impulse rendered at client position *p*, detected at capture position *q*, compared in host time with `au_frame_to_host`) MUST be within ±0.25 ms of the output `latency_ns` plus the input `latency_ns`. The presentation-time error of `present_host` MUST be within ±0.25 ms. [AU-PERF-003] | AU-T-062 |
| **P4** | At a 128-frame period and 48 kHz, `latency_ns − lat_hw_ns` of an output stream MUST be ≤ 3 periods (8.0 ms). For comparison, S7 measured 8.44 ms in total on macOS, hardware included. [AU-PERF-004] | AU-T-063 |
| **P5** | The render path MUST take no locks and make no allocations: the library's stream thread from receive to callback (AU-T2-003) and the mixer (AU-MIX-004) compile under the T2 annotations; `rt_violations` = 0 over the soak in a debug build; and a negative test (an allocation added to the synth render) fails to compile. [AU-PERF-005] | AU-T-064, AU-T-065 |
| **P6** | In a steady state the stream thread MUST make exactly one system call per period (the doorbell receive), and the mixer MUST make at most one doorbell send per due stream plus the driver handshake. A late wake adds one `nd_rt_set_deadline` (AU-STREAM-007, AU-MIX-008); a steady state has none. [AU-PERF-006] | AU-T-066 |
| **P7** | Wake latency, from the doorbell send to the callback entry (`max_wake_ns`, sampled per period), MUST have p99 ≤ 100 µs and a maximum ≤ 500 µs under the stated load. This is the audio service's end-to-end counterpart of SC's admitted wake bound (SC-PERF-002) and depends on it. [AU-PERF-007] | AU-T-067 |
| **P8** | A voice MUST start within one mixer period plus `lat_mixer_ns + lat_device_ns + lat_hw_ns` of `au_voice_play` returning. With `AU_VF_AT`, it starts at `at_host` ± 1 frame. [AU-PERF-008] | AU-T-068 |
| **P9** | The mixer's measured computation for 16 streams and 64 voices at a 128-frame period MUST be ≤ 25 % of the period at p99.9. [AU-PERF-009] | AU-T-069 |
| **P10** | After a default-device switch, a following stream MUST resume audio on the new device within 100 ms, and the contract event MUST arrive within the bound of AU-EVENT-003. [AU-PERF-010] | AU-T-070 |
| **P11** | Playing a sound from nothing MUST take no more than 3 `libndaudio` calls: `au_open`, `au_stream_open` and `au_stream_start`, or `au_open`, `au_buffer_create` and `au_voice_play` (charter §1 bar). [AU-PERF-011] | AU-T-071 |

## 8. Conformance

Methods:
- **unit**: a host test of `libndaudio` or `audiod` against a fake driver and a fake SC;
- **proto**: 9P protocol conformance through `audioconform`, the audio counterpart of `deskconform`, which drives the 9P tree and rings directly;
- **measure**: a measurement on NeoDarwin on the reference system of §7;
- **S7**: the synth-ui or game-loop prototype on NeoDarwin;
- **build**: a compile-time check in `bazel test`.

| Test | Requirements | Method | Pass criterion |
|---|---|---|---|
| AU-T-001 | AU-NS-001, AU-NS-002 | proto | tree served and mounted at `/n/sys/audio`; `au_open` in a namespace without it returns `AU_ERR_PERMISSION` |
| AU-T-002 | AU-NS-003 | proto | a killed client's streams, voices and buffers are gone from `state` within 1 s |
| AU-T-003 | AU-NS-004, AU-NS-005 | proto | every text file parses as `key=value` UTF-8; unknown keys and verbs return the stated errors and change nothing |
| AU-T-004 | AU-NS-006, AU-NS-009 | proto | `contract`, `info` and `events` reads return whole little-endian records; a short read fails; every `events` file reports length 0, and a `Tready` on it is answered exactly when a whole record is queued |
| AU-T-005 | AU-NS-007, AU-SEC-002 | proto | session A cannot list or open session B's objects; with `cap:audio:mixer` it lists B's streams |
| AU-T-006 | AU-NS-008, AU-VOICE-010 | proto | each ctl verb and its ring equivalent give identical contract, voice-state and event sequences; a stalled command slot is skipped after 2 ticks |
| AU-T-007 | AU-LIB-001 | build | a lint over the tree: no first-party target except `libndaudio` and `audiod` includes the ring layouts or maps `ring` |
| AU-T-008 | AU-LIB-002, AU-PERF-006 | measure | a 9P trace plus a syscall trace over 10 s of RUNNING streams: 0 9P messages per period; 1 receive per period on the stream thread |
| AU-T-009 | AU-LIB-003, AU-LIB-004, AU-EVENT-005 | unit | the session fd is readable in kqueue iff events are pending; `au_session_events` never blocks; no library thread exists for events |
| AU-T-010 | AU-LIB-005 | unit | on each `AU_EV_CONTRACT`, `au_stream_contract` already returns that generation |
| AU-T-011 | AU-LIB-006, AU-LIB-007 | unit | TSan run with every call from 8 threads; `[RT]` calls under the allocation and lock hook report 0; no per-period allocation |
| AU-T-012 | AU-LIB-008 | unit | two sessions in one process operate independently; closing one leaves the other's streams RUNNING |
| AU-T-013 | AU-ABI-001 | unit | stale, wrong-kind and NONE handles on every call return `AU_ERR_INVALID_HANDLE` with no state change |
| AU-T-014 | AU-ABI-002, AU-ABI-006, AU-STREAM-005 | unit | each error sets a detail string; with a fake SC refusing, open fails with `AU_ERR_ADMISSION`, the detail holds `nd_sched_error_detail()` verbatim, and `au_error_refusal` returns the fake's `sc_rt_refusal`; after a later successful call it returns false; no callback runs |
| AU-T-015 | AU-ABI-003 | unit | device loss, service restart and revocation each produce events, and the process keeps running |
| AU-T-016 | AU-ABI-004, AU-ABI-005, AU-VER-003 | unit | records with a larger `size` are accepted; only `size` bytes are written; reserved fields are zero; unknown event types and change bits are ignored |
| AU-T-017 | AU-RING-001, AU-SEC-006 | proto | `ring` returns two port names to the owner; another session's read is refused; no client ever holds a doorbell send right |
| AU-T-018 | AU-RING-002, AU-RING-007 | unit | doorbell qlimit is 1; with the client stalled, the mixer tick time is unchanged and the send never blocks |
| AU-T-019 | AU-RING-003, AU-RING-004, AU-RING-005 | unit | capacity = (lead + 1) × period; positions monotonic over 2^33 frames (simulated); a model checker (C11 memory model) finds no torn period |
| AU-T-020 | AU-RING-006, AU-CONTRACT-005 | unit | concurrent contract and anchor updates at 10 kHz: every reader snapshot is one generation; readers retry at most 4 times |
| AU-T-021 | AU-RING-008, AU-RING-009 | unit | doorbells on state change, generation change and watermark crossing (once per crossing); a client counting positions, not messages, never misses a period with coalesced doorbells |
| AU-T-022 | AU-RING-010, AU-RING-011 | proto | fuzzing `client_pos`, `watermark_frames` and `client_flags` for 10^7 ticks: `audiod` does not crash, and other streams are unaffected; `ring_invalid` counts |
| AU-T-023 | AU-RING-012 | unit | NaN, Inf and ±1e9 samples in one ring leave other streams' output bit-exact and finite |
| AU-T-024 | AU-RING-013, AU-CAPTURE-003 | proto | a ring contains only its stream's samples; monitor open without `cap:audio:monitor` is refused, and with it returns the post-master mix |
| AU-T-025 | AU-STREAM-001 | unit | requests of 100, 128, 441, 480 and 4096 frames yield the power-of-two rule, clamped to the device range; `EXACT_PERIOD` fails with `AU_ERR_PERIOD` on mismatch |
| AU-T-026 | AU-STREAM-002, AU-STREAM-003 | unit | after open, the contract is readable and the state is STOPPED; failure injected at each open step leaves no fid, mapping or thread |
| AU-T-027 | AU-STREAM-004 | unit | the stream thread has intent `SC_INTENT_AUDIO`; the fake SC records a self-admission with the stated period, computation and a constraint ≤ the period at every lead from 1 to 64; the `admitted` ctl appears in the contract |
| AU-T-028 | AU-STREAM-006 | unit | exactly one `AU_RF_PRIME` call before the first period; none with `NO_PRIME` |
| AU-T-029 | AU-STREAM-007, AU-STREAM-008 | unit | the render sequence, positions and flags match the loop spec, including skip-ahead after an underrun; the fake SC sees `nd_rt_set_deadline(au_host_to_ns(deadline_host))` after a late wake and no call after an on-time wake; with no conversion, `buf` points into the ring data area |
| AU-T-030 | AU-STREAM-009 | unit | a render sleeping past the deadline increments `client_late`; one exceeding computation increments `client_overbudget`; later calls are unchanged |
| AU-T-031 | AU-STREAM-010, AU-VER-002 | unit | with the fake SC reporting `SC_CAP_RT_GROUP`, `RT_GROUP` is true and `au_stream_rt_group` returns the stream thread's ticket, which a helper thread joins; without it, `AU_ERR_UNSUPPORTED` and `RT_GROUP` false; `DUPLEX` and `VOICE_PITCH` are false |
| AU-T-032 | AU-STREAM-011 | unit | RUNNING at the next tick after start; no render call more than one period after stop |
| AU-T-033 | AU-STREAM-012, AU-STREAM-013 | unit | write and read never block and return counts; `write_all` returns `AU_ERR_TIMEOUT` at the deadline; an idle started queue stream counts 0 underruns until the first write |
| AU-T-034 | AU-STREAM-014 | unit | drain delivers `AU_EV_DRAINED` after the last frame is consumed, and the stream becomes STOPPED |
| AU-T-035 | AU-STREAM-015, AU-STREAM-016, AU-STREAM-017 | unit | a default switch 48 → 44.1 kHz: old ring RETIRED; the thread re-admits the same ticket and remaps with no app dispatch; silence during the switch; a stalled transition yields `AU_EV_STREAM_LOST` at 50 ms; a refused re-admission yields `AU_EV_STREAM_LOST` with `AU_ERR_ADMISSION` |
| AU-T-036 | AU-CONV-001, AU-CONV-002 | unit | ring header shows F32 at the device rate; conversion runs on the stream thread (thread id checked); S16/S32/S24_32/U8 round-trip within 1 LSB |
| AU-T-037 | AU-CONV-003 | unit | mono → FL+FR; 5.1 → stereo drops LFE and missing channels are silent, per the matrix |
| AU-T-038 | AU-CONV-004 | unit | 44.1 → 48 kHz: frame counts within ±1; `CONVERTING` set; group delay equals `lat_convert_ns` ± 1 frame; `NO_CONVERT` fails with `AU_ERR_FORMAT` |
| AU-T-039 | AU-CONTRACT-001 | unit | `latency_ns` = the sum of the components in every generation; the library adds nothing |
| AU-T-040 | AU-CONTRACT-002, AU-CONTRACT-003 | unit | `present_host` = `au_frame_to_host(info.pos)`; anchor updated every tick with the generation unchanged; ±1-frame accuracy against the fake device clock with 100 ppm drift |
| AU-T-041 | AU-CONTRACT-004 | unit | each field change bumps the generation, sets the right `change` bits, updates the file and the copy, and yields one event |
| AU-T-042 | AU-CONTRACT-006 | unit | the mixer contract reports the mixer period, and `lat_client_ns` = one mixer period |
| AU-T-043 | AU-CONTRACT-007, AU-ROUTE-002 | S7 | game loop: default switched from speakers to headphones mid-run; stream handle unchanged; `AU_CHG_DEVICE`; audio resumes; no error returned from any call |
| AU-T-044 | AU-EVENT-001, AU-EVENT-002 | proto | event order matches change order; a burst of 1000 changes delivers the last generation |
| AU-T-045 | AU-EVENT-003 | measure | change applied → session fd readable: ≤ 1 mixer period + 1 ms at p100 over 1000 changes |
| AU-T-046 | AU-EVENT-004, AU-DEV-003 | proto | virtio-snd hot-add and hot-remove in QEMU: events to sessions holding the matching capability only; added ≤ 500 ms, removed ≤ 1 tick |
| AU-T-047 | AU-XRUN-001, AU-XRUN-003 | unit | a withheld period gives 1 underrun, the right `underrun_frames`, a partial mix plus silence, `server_pos` advanced; a late but in-time render is not an underrun |
| AU-T-048 | AU-XRUN-002 | unit | capture overrun: period discarded, `overruns` incremented, `server_pos` held, anchor correct across the gap, next buffer flagged `DISCONTINUITY` |
| AU-T-049 | AU-XRUN-004, AU-XRUN-005 | unit | ≤ 1 underrun event per 50 ms per stream with cumulative and delta counts; stats, file and render info agree |
| AU-T-050 | AU-MIX-001, AU-MIX-006, AU-MIX-007, AU-MIX-008 | unit | the mixer self-admits through `nd_rt_admit_ex` with `SC_RT_F_SYSTEM` and the stated parameters; a failed re-admission keeps the old period and fails the start with `AU_ERR_ADMISSION`; fake overrun and miss notices are counted in `devices/UID/stats` by a service thread, and a fake demotion logs and shows `rt_state=demoted`; the mixer calls `nd_rt_set_deadline` after a late wake only |
| AU-T-051 | AU-MIX-002 | unit | mixer period tracks the minimum running stream period and `period-max`; changes only on start and stop, at tick boundaries; a sine through the change has no discontinuity |
| AU-T-052 | AU-MIX-003, AU-MIX-004, AU-T2-003 | build + unit | mixer and stream-thread modules compile with `@_noLocks`/`@_noAllocation` and performance diagnostics; the tick order is verified by trace |
| AU-T-053 | AU-MIX-005 | unit | a stream or voice beyond the limits returns `AU_ERR_LIMIT` or `AU_VEND_LIMIT`; admitted computation unchanged |
| AU-T-054 | AU-VOICE-001, AU-VOICE-002 | proto | writes to `data` after `seal` fail; releasing a playing buffer lets its voices finish |
| AU-T-055 | AU-VOICE-003 | unit | play, stop, gain and loop make 0 system calls and 0 9P messages (syscall trace); a full ring returns `AU_ERR_BUSY` |
| AU-T-056 | AU-VOICE-004, AU-VOICE-005 | unit | start on the draining tick; `AU_VF_AT` sample-exact; a past `at_host` starts at once; gain and stop ramps linear over one mixer period |
| AU-T-057 | AU-VOICE-006, AU-VOICE-007 | unit | loop region repeats exactly; unsetting the loop plays to the end; bad regions rejected; a 22.05 kHz buffer plays at the right pitch on a 48 kHz device |
| AU-T-058 | AU-VOICE-008, AU-VOICE-009 | unit | `au_voice_state` is consistent under concurrent ticks; one `VOICE_END` per voice with the right reason; no slot reused before ENDED is observed |
| AU-T-059 | AU-CAPTURE-001, AU-CAPTURE-002 | measure | two capture streams on one input receive identical data; `present_host` is within ±0.25 ms of loopback-measured capture time |
| AU-T-060 | AU-PERF-001 | S7 | synth-ui contract reports 128, 2 666 667 ns and a mixer period of 128; callback gap p99 ≤ 4.0 ms |
| AU-T-061 | AU-PERF-002 | S7 | 600 s soak under the stated load: 0 underruns, 0 late ticks |
| AU-T-062 | AU-PERF-003 | measure | loopback round trip within ±0.25 ms of the reported output plus input latency; `present_host` error within ±0.25 ms |
| AU-T-063 | AU-PERF-004 | measure | `latency_ns − lat_hw_ns` ≤ 8.0 ms at 128/48 kHz |
| AU-T-064 | AU-PERF-005, AU-T2-005 | S7 | debug build soak: `rt_violations` = 0; the probe allocation in a test build is counted (the hook sees the thread) |
| AU-T-065 | AU-PERF-005, AU-T2-001 | build | the synth render marked `@_noLocks` compiles; the variant with an `Array.append` fails to compile |
| AU-T-066 | AU-PERF-006 | measure | syscall trace: 1 per period on the stream thread; mixer sends ≤ the due streams |
| AU-T-067 | AU-PERF-007 | measure | `max_wake_ns` distribution under load: p99 ≤ 100 µs, max ≤ 500 µs |
| AU-T-068 | AU-PERF-008 | measure | voice onset detected by loopback within the bound; `AU_VF_AT` onsets ± 1 frame |
| AU-T-069 | AU-PERF-009 | measure | mixer tick duration p99.9 ≤ 25 % of the period with 16 streams and 64 voices |
| AU-T-070 | AU-PERF-010 | measure | default switch → audio on the new device ≤ 100 ms (loopback) |
| AU-T-071 | AU-PERF-011 | S7 | the minimal program plays a tone in 3 calls by each route |
| AU-T-072 | AU-T2-002 | S7 | synth-ui's UI↔render traffic uses only atomics and SPSC rings (review plus the `@_noLocks` build) |
| AU-T-073 | AU-T2-004 | unit + measure | a C render calling `sinf`/`powf` runs with no Swift frames (stack sample) and 0 `rt_violations`; NeoDarwin libm allow-list functions pass a lock and allocation hook test; the toolkit C-render `open` installs no thunk |
| AU-T-074 | AU-DEV-001, AU-DEV-002, AU-DEV-004 | proto | `null` is always listed; the same USB device on the same port across reboot and replug keeps its uid with a new handle; the generation bumps on each field change |
| AU-T-075 | AU-DEV-005 | measure | a jack insert yields `AU_CHG_ROUTE` with the new `lat_hw_ns`, and no device change |
| AU-T-076 | AU-ROUTE-001, AU-ROUTE-003 | unit | the default-selection order over a table of add and remove sequences; exactly one default per direction |
| AU-T-077 | AU-ROUTE-004 | unit | pinned device removed → DETACHED, no callbacks, writes return 0; same uid back → RUNNING |
| AU-T-078 | AU-ROUTE-005 | proto | routes, `au_set_default` and `default-*` without `cap:audio:device` are refused |
| AU-T-079 | AU-DRV-001, AU-DRV-002, AU-DRV-003 | measure | virtio-snd and USB Audio Class 2 dexts provide the stated data; `audiod` counters are not duplicated under `/n/sys/dev`; killing the dext shows as remove then add, and `audiod` survives |
| AU-T-080 | AU-PULSE-001, AU-PULSE-002 | proto | libpulse clients of versions 13 and 35 connect; every unsupported command returns `PA_ERR_NOTSUPPORTED`; SHM is declined |
| AU-T-081 | AU-PULSE-003, AU-PULSE-004 | proto | each listed format plays correctly; other formats are refused; the granted `buffer_attr` is reported and drives `REQUEST` |
| AU-T-082 | AU-PULSE-005 | measure | mpv `--ao=pulse` A/V offset ≤ 1 ms against the loopback measurement; Pulse latency = queued plus `latency_ns` |
| AU-T-083 | AU-PULSE-006, AU-SEC-001 | proto | a Pulse peer without `cap:audio:capture` cannot record through the door, although the door holds `cap:audio:door`; capabilities are fixed at attach |
| AU-T-084 | AU-SEC-003, AU-SEC-004 | proto | a manifest-only capture grant is refused; with a user grant, capture appears in `state`, `log` and audit with the token id |
| AU-T-085 | AU-SEC-005 | proto | revoking the token of a RUNNING capture stops it within 100 ms and delivers `AU_EV_REVOKED` |
| AU-T-086 | AU-VER-001 | unit | `au_query` true for all v1 ids with values, false for unknown ids; `caps` lists the same |
| AU-T-087 | (header) | build | `nd_audio.h` compiles freestanding as C23 with `-Wpedantic` (`nd_audio_check.c`) and as C++20 with `-Wall -Wextra -Werror -pedantic` (`nd_audio_cxx.cpp`, with C linkage); all layout `static_assert`s hold in both; the Swift import type-checks (`bazel test //docs/audio/...`) |
| AU-T-088 | (P4-18 exit) | S7 | `paplay` and `pacat --record` through `pulsedoor` play and record; the synth-ui exit of AU-T-061 holds |
| AU-T-089 | AU-EVENT-006 | unit | with a fake SC, overrun, miss, demotion and revocation notices on a stream's ticket each make the session fd readable and come back from `au_session_events` as one `AU_EV_RT_NOTICE` with kind and count; demotion and revocation also give `AU_EV_CONTRACT` with `AU_CHG_ADMISSION` and zero `rt_*`; callbacks continue |

## 9. Rationale and evidence

**Why a daemon with a mixer, rather than direct device access** (F-215, heritage §4):
- The consoles took audio off the application's real-time path: Switch `audren` and 3DS NDSP run a DSP mixer on a fixed period. Heritage proposes `AUD.voice` from them.
- A daemon-owned mixer gives one fixed-period real-time thread per device, admitted once with a known worst case (AU-MIX-001, AU-MIX-005), and it makes system mixing, routing and default-following possible (heritage §5: "worth paying for").
- The price is one mixer period of latency (`lat_mixer_ns`). At 128 frames this still keeps the software latency at or under 3 periods (AU-PERF-004), below the 8.44 ms S7 measured on macOS.

**Why two tiers** (Q2 C10 and R10):
- Every layer with audio converged on a stream with pull and push (SDL, sokol, raylib, JUCE) plus voices over a mixer (Godot, LÖVE, bevy, raylib `Sound`).
- The voice tier also lets a game play sound effects with no real-time thread of its own, and it gives the Pulse sample cache a direct mapping.

**Why a shared ring and a doorbell, not 9P per period:**
- 9P is the control protocol (heritage §4: Exec IORequest ≈ 9P validates it).
- The Horizon HID and time rings show that fast state belongs in shared memory with one notification.
- The position is the fence (AU-RING-009). The output client therefore needs no system call to hand a period over, and the stream thread's steady state is exactly one receive (AU-PERF-006).
- A Mach port was chosen over a shared-address wait for the doorbell for three reasons:
  - it can be put in a kqueue (`EVFILT_MACHPORT`), so a queue-mode stream joins the one wait (P1);
  - its queue limit of 1 coalesces for free;
  - rights make it unforgeable (AU-SEC-006).
- A timer-driven client thread, phase-locked to the anchor as Core Audio's IO thread is, is the alternative. It is in §10.

**Why conversion runs in the client:**
- The mixer's per-stream cost stays constant (a copy, a gain and a channel matrix), so its admission is independent of odd client formats (AU-CONV-002).
- Each client pays for its own conversion from its own budget. This mirrors SDL's `SDL_AudioStream`, which converts in-process.
- The cost is a layout transition when the device rate changes (§4.5.6).

**Why one contract record** (F-216, charter P11, S7):
- The S7 shim assembled the contract from five Core Audio properties, set the period per device, and followed the default by hand: about 190 lines (SHIM-NOTES W9).
- Four projects summed three different subsets of the latency properties (mpv, Wine, JUCE). `audiod` is therefore the single authority (AU-CONTRACT-001), and the components are published so that nobody needs to re-derive them.
- SDL3 reported a 128-frame period while delivering 4×128 bursts every 10.67 ms, with at least 32 ms of latency it did not report (synth-ui/sdl3/NOTES.md). AU-PERF-001's no-burst clause and AU-PERF-003's measured-equals-reported clause test exactly this failure.
- The anchor, and the timebase copied into the record, come from the shim's per-callback seqlock anchor and its "no lazy statics on the IO thread" fix (W8).

**Why contract changes are events on one descriptor** (P1, F-201, F-216):
- S7's one-wait self-test delivered a real buffer-size change as an `.audio` event in the loop's wait.
- WASAPI's `AUDCLNT_E_DEVICE_INVALIDATED`, handled three ways by Godot, mpv and SDL, is the anti-pattern that AU-CONTRACT-007 forbids.

**Why the stream thread belongs to the library, admitted through SC** (F-215):
- Every project picks its own mechanism for its real-time thread and fails silently (zed, SDL, JUCE, mpv, Godot).
- Haiku's `BSoundPlayer` and Core Audio create the thread themselves, so applications never write magic numbers.
- Refusing without degrading (AU-STREAM-005) follows Plan 9's `admit`.

**Why the T2 subset and the C trampoline** (S7 W8, charter §3 Audio):
- The Swift 6.4 checker accepted synth-ui's render only without libm, without the first use of a class, and without `&&`/`||`. The subset records this.
- The warm-up call (AU-STREAM-006) deals with the first-use hazard that the probe exposed.
- S7 also noted that a C trampoline cannot close the gap for a closure that is itself Swift. The trampoline is therefore defined as a C render called with no Swift in between (AU-T2-004). It serves DSP code that needs libm; it is not a way to check Swift.

**Why a small routing policy** (F-216):
- The three WASAPI roles led Godot and mpv to follow different ones.
- Pulse and PipeWire move streams on the server side.
- One role, following by default, pinning on request, and the `null` device together mean that opening on the default never fails.

**Why a Pulse door** (Q5 §7 item 5, C1):
- ALSA is called directly by 7 corpus projects, Pulse by 6 and PipeWire by 3. cubeb, cpal and OpenAL sit on top of these.
- A door speaking the Pulse protocol in front of the service, plus the stock ALSA `pulse` plugin, reaches all of them without new client code.
- The sample cache maps onto voices, and latency comes from the contract, which fixes the Pulse latency bugs that F-216 documents.

**Why the capture consent and indicator** (namespaces-agents.md §4 and §5): tokens remove ambient authority, and capture is the audio capability whose misuse harms the user. The ring path bypasses `nsd`'s per-request checks, so `audiod` watches revocation itself (AU-SEC-005).

## 10. Open issues

1. **SC alignment. Resolved** (consistency pass, 2026-09-28; `docs/spec-consistency.md`). SC version 1 specifies S1–S8, mapped in §4.14. Refusal is SC's code plus `sc_rt_refusal` and the thread-local `nd_sched_error_detail()` (SC-RT-020); AU-ABI-002 carries that string verbatim and AU-ABI-006 exposes the record. A Mach receive is a blocking point (SC-RT-021), per-job deadlines are SC-RT-022, the group handle is the ticket (AU-STREAM-010), and notices, including demotion, reach the application as AU-EVENT-006 says.
2. **Doorbell or clock-driven wake.** A client thread that wakes on its own deadline, computed from the anchor, would remove one cross-process wake per period, but it needs phase correction. The decision should be made from AU-T-067's wake-latency data.
3. **Real-time-safe libm and a checker allow-list.** AU-T2-004 assumes that NeoDarwin's libm functions are lock-free and allocation-free. This needs an audit of the vendored Libm. The Swift checker needs an allow-list or annotated C headers before the subset rows in §4.13 can be withdrawn.
4. **Lead of 1 at 128 frames.** One period of client lead (2.67 ms) depends on the wake latency meeting AU-PERF-007. If it does not, the default lead becomes 2, and `lat_client_ns` rises to match.
5. **Recovering from a service restart.** Streams are re-established with `AU_CHG_SERVICE`, but buffers and voices are reported lost (`AU_EV_BUFFER_LOST`, `AU_VEND_SERVICE`). Whether the library should keep copies of buffers is open.
6. **Large voice buffers.** Upload is by 9P copy. Music-sized buffers may want a shared-memory upload; for now, streaming music uses Tier 1 queue mode.
7. **Voice latency in a voices-only mixer.** A voices-only mixer runs at `period-max` (256 frames). A game that wants 128-frame voices must also run a stream. A session hint for the mixer period may be added.
8. **The Pulse protocol version.** Version 35 is to be confirmed against the libpulse versions in the corpus. The memfd and srbchannel transports would need the epoll/memfd shim (Q5 C8).
9. **Duplex, pitch, pan and per-voice effects** are reserved as capabilities, pending demand from S7 and the corpus.
10. **The reference board's codec.** The targets in §7 assume a USB Audio Class 2 device. Onboard codecs on the reference board may report `lat_hw_ns` less precisely.
11. **After a demotion.** A demoted or revoked stream thread or mixer keeps running at its fallback intent (AU-EVENT-006, AU-MIX-007). Whether the library and `audiod` should re-admit automatically, and when (a re-admission is a system call that the T2 paths do not allow), is open. Until decided, re-admission happens only at the next layout transition (AU-STREAM-016) or mixer-period change (AU-MIX-006).
12. **Namespace and token vocabulary.** `docs/architecture/namespaces-agents.md` does not yet list `/n/sys/audio` among the `/n/sys/<service>` trees (§3), the `nsd` registry name `audio`, or the tokens `cap:audio:play`, `cap:audio:capture`, `cap:audio:monitor`, `cap:audio:device`, `cap:audio:mixer` and `cap:audio:door` (its §2 lists `keyd`'s example tokens only). Nor does it list manifest entitlement names such as SC's `sched.rt.system`, which `audiod` holds. They are to be added there when `keyd` (P5-03) fixes the token vocabulary. _Resolved 2026-09-28: namespaces-agents.md §2 and §3 now list it (the tree in the `/n/sys/<service>` list; tokens and the `sched.rt.system` entitlement in the keyd and ndsandbox rows)._
13. **9P error spelling.** `audiod`'s 9P error strings (`unknown key KEY`, `short read`, `permission denied`) follow Plan 9 usage, while the window protocol's begin with a code name and `: ` (WP-ACK-004: `short`, `perm`). Both meet spec-conventions.md §5 at the C level; whether the 9P spellings should be unified for the toolkit's error mapping is for P4-12 to propose.

## 11. Changelog

| Version | Date | Change |
|---|---|---|
| 1 (draft) | 2026-09-28 | First draft for epic P4-18. It defines the namespace, library, ring and doorbell, streams, contract, events, xrun accounting, mixer and voices, capture, devices, routing, T2 render rules, driver boundary, Pulse door, capabilities, security, performance targets and conformance list, with `nd_audio.h` and its compile checks. |
| 1 (draft) | 2026-09-28 | Consistency pass with SC and WP (`docs/spec-consistency.md`); no id renumbered or removed. The clock is SC's (host time = SC clock ticks, SC-TIME-002). §4.14 maps S1–S8 to SC ids, and requirements cite them. Added AU-NS-009 (events files are stream files), AU-ABI-006 (`au_error_refusal`), AU-EVENT-006 (`AU_EV_RT_NOTICE`, demotion as a contract change), AU-MIX-007 (mixer notices), AU-MIX-008 (late-wake mixer deadline), test AU-T-089. Fixed: the stream constraint is ≤ the period, as SC's test 1 requires (AU-STREAM-004); late-wake deadlines through `nd_rt_set_deadline` (AU-STREAM-007); the mixer admits with `SC_RT_F_SYSTEM` (AU-MIX-001); the group handle is the ticket (AU-STREAM-010); a refused re-admission ends the stream (AU-STREAM-016). `nd_audio.h` compiles as C++20, checked by `nd_audio_cxx.cpp`. Open issue 1 resolved; 11–13 added. |
