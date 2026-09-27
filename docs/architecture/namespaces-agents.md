<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Namespaces, application state as files, and agent-era security

## 1. What Plan 9 got right, and what NeoDarwin takes

| Plan 9 / 9front idea | NeoDarwin realisation |
|---|---|
| Everything is a file server; 9P is the one protocol | `/n` is a 9P namespace served by `nsd`; every daemon and app can serve a 9P tree with `libns` |
| Per-process namespaces built with `bind` and `mount` | per-process mount tables inside `nsd` plus a kernel `nd9p` mount at `/n`; `ns bind`, `ns mount`, `ns union` manipulate the calling process's table |
| Control files (`ctl`), event files, `clone` allocation | the same conventions, documented as the **state file protocol** (§3) |
| `factotum` holds keys, programs never see secrets | `keyd` holds keys and mints capability tokens; processes present tokens, never keys |
| No superuser; authority flows from namespace | NeoDarwin keeps POSIX uids for compatibility, but new services grant nothing on uid alone; they check capability tokens carried in the 9P attach |

The kernel is XNU, not Plan 9, so the realisation is layered on top of POSIX rather than replacing it: `/n` is where the new world lives; `/usr`, `/etc`, sockets and Mach ports keep working.

## 2. Components

| Component | Role |
|---|---|
| `nd9p.kext` | 9P2000.L VFS client (ported from FreeBSD `p9fs`), mounts `/n` once at boot |
| `nsd` | the namespace multiplexer: holds one mount table per process (keyed by audit token), unions and binds, forwards 9P to the owning servers over Mach ports or Unix sockets, enforces attach capabilities |
| `libns` (Swift) over `libagent` (C) | lets a program export its agent tree in a few lines: implement `schema`, `state`, `action` and call `agent_log`; `libns` adds Swift ergonomics and the `wsys` window export |
| `keyd` | key storage, token minting (`cap:pkg:write`, `cap:wsys:capture:<win>`, `cap:app:<id>:ctl`), expiry, revocation, audit |
| `ndsandbox.kext` | TrustedBSD MAC policy (`security/mac_policy.h`, in tree) that binds a process to its namespace table and manifest entitlements; denies `/n` paths outside the table and enforces file/Mach/IOKit rules from the manifest |
| `auditd` | receives MAC and `nsd` audit records; publishes `/n/sys/audit/` (read requires `cap:audit:read`) |

## 3. Application protocol: `/n/agent`

Specified in `docs/desktop/agent-protocol.md` (reference implementation `libagent`, 457 lines of freestanding C, from the plan-neo pilot):

```
/n/agent/
    index                r    one line per registered app: APPID PID NAME VERSION
    log                  r    registrations and departures, JSON lines
    APPID/
        schema           r    JSON: verbs, parameters, the shape of state and events
        state            r    JSON snapshot; opening takes the snapshot; carries seq
        actions          rw   write `verb key=value …`; read the JSON reply on the same fd
        log              r    append-only JSON events; blocks until there is one; 256-event ring
/n/sys/<service>/…       same four files for system daemons: pkg, input, net, power, proc, dev, srv, audit
/n/desktop, /n/theme     the window system and theme trees (graphics-desktop design §2), each with a schema
```

Rules: verbs are what a user could do, named as the user would say it; state is the model, not the view (byte offsets, paths, flags, never pixels or widget ids); every mutation is idempotent given `if_seq`; replies echo the new cursor or selection; text is UTF-8 with byte offsets; the log describes deltas. NeoDarwin adds: `agentd` (the registry serving `index` and `log`) as an `nsd` plug-in; capability tokens on attach (§4); audit of every `actions` write with the caller's token id; snapshots that contain secrets split into separately-gated files.

## 4. Capability model

- A **token** is an opaque, signed, expiring string minted by `keyd`: `{subject, caps[], expiry, nonce}` signed with the machine key. Tokens are carried in the 9P `Tattach` `aname`/`uname` fields and in a Mach message trailer for native protocols.
- **Ambient authority is removed at the namespace boundary.** A process's mount table (its view of `/n`) is set at spawn by `launchd`/`nsd` from the program's manifest; a process cannot see a tree it was not given, so most policy is "what is mounted", not "what is checked".
- **Attenuation.** A holder can mint a narrower token for a child or an agent (`cap:app:editor:ctl` → `cap:app:editor:ctl:save-only`), with a shorter expiry. `keyd` records the chain.
- **Revocation** is immediate: `nsd` re-validates tokens on each attach and on `ctl` writes; `keyd` publishes a revocation list at `/n/sys/keys/revoked`.
- POSIX compatibility: uid-based access still governs `/usr`, `/etc`, home directories. Manifests may *narrow* that (`ndsandbox` rules) but never widen it.

## 5. Agents as first-class principals

- An agent is a process with a manifest and a namespace like any other; it typically gets a **curated union**: `/n/app/*/state` read-only, `ctl` only for the apps the user granted, `/n/sys/pkg/plan` read, and never `/n/sys/keys`.
- User consent is a `ctl` write on `/n/sys/keys/grants` by the user's session, producing a token the agent reads from its own `/n/self/tokens`.
- Everything an agent did is reconstructible from `/n/sys/audit` plus app `events`, which is the accountability story.
- Agents get **batch reads** (`events` arrays, snapshot files) rather than chatty RPC, matching how models consume context.

## 6. Kernel work required

| Item | Size | Notes |
|---|---|---|
| `nd9p.kext` port | medium | FreeBSD `p9fs` → XNU VFS; transports: virtio-9p, Unix socket, Mach port |
| `ndsandbox.kext` | medium | MAC policy hooks: `mpo_vnode_check_open`, `mpo_mach_port_*`, `mpo_iokit_*`; manifest loaded at `exec` from the package |
| per-process table lookup | small | `nsd` keys tables by audit token (`audit_token_t`) obtained from the Mach message trailer, so no kernel change is needed for identity |
| union/bind mounts | none | `BINDFS`, `NULLFS`, `CONFIG_UNION_MOUNTS` are already in `config/MASTER` |

## 7. Roadmap hooks

P5-01 `libagent`/`lib9p` port + `libns` + `nsd` (userland only, testable on macOS host with `agentctl.py`) → P5-02 `nd9p.kext` → P5-03 `keyd` tokens → P5-04 `ndsandbox` policy → P5-05 system daemons publish `/n/sys/*` → P5-06 toolkit auto-export of `windows/` (mirrored from `/n/wsys/wins`) → P5-07 agent session flow and audit.
