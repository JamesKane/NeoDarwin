// SPDX-License-Identifier: BSD-2-Clause
//
// pid1 (P1-07): the first NeoDarwin userland program. mockfs roots the
// kernel on a memory device holding this one static executable and runs it
// as /sbin/launchd (docs/kernel/arm64-sbsa-bringup.md §2.4). There is no
// libSystem and no dyld: it enters the kernel through the Darwin arm64 trap
// ABI (Darwin/traps.h) and checks, in order, that
//
//   1. it is process 1;
//   2. /dev/console (devfs, mounted by bsd_init over mockfs's /dev) opens and
//      takes a write;
//   3. anonymous memory maps, holds a pattern, and unmaps;
//   4. Mach traps answer: task_self_trap and mach_reply_port return names;
//   5. a Mach message sent to its own receive right comes back intact
//      through mach_msg2_trap, the path libsystem_kernel's mach_msg() takes.
//
// A check that fails prints why and exits with its number; the kernel then
// panics with "initproc exited", which carries that status. On success it
// says so and blocks in a Mach receive for good, as PID 1 must not exit.

import Darwin_Traps

// BSD system calls (bsd/kern/syscalls.master).
let sysExit: Int64 = 1
let sysWrite: Int64 = 4
let sysOpen: Int64 = 5
let sysGetpid: Int64 = 20
let sysMunmap: Int64 = 73
let sysMmap: Int64 = 197

// Mach traps (osfmk/kern/syscall_sw.c).
let trapMachReplyPort: Int64 = 26
let trapTaskSelf: Int64 = 28
let trapMachMsg2: Int64 = 47

let oRDWR: UInt64 = 2
let protRW: UInt64 = 3  // PROT_READ | PROT_WRITE
let mapPrivateAnon: UInt64 = 0x1002  // MAP_ANON | MAP_PRIVATE
let page: UInt64 = 0x4000

// <mach/message.h>
let machMsgTypeMakeSend: UInt32 = 20
let mach64SendMsg: UInt64 = 0x1
let mach64RcvMsg: UInt64 = 0x2
let mach64SendMQCall: UInt64 = 0x4_0000_0000  // mach_msg2's call kind: a message queue
let headerSize: UInt32 = 24  // mach_msg_header_t
let messageSize: UInt32 = headerSize + 8
let messageID: UInt32 = 0x4e44_0001
let payload: UInt64 = 0x6e65_6f64_6172_7769  // "neodarwi"

// Single-threaded: PID 1 never creates a second thread.
nonisolated(unsafe) var console: UInt64 = 0

func syscall(_ n: Int64, _ a: UInt64 = 0, _ b: UInt64 = 0, _ c: UInt64 = 0,
             _ d: UInt64 = 0, _ e: UInt64 = 0, _ f: UInt64 = 0) -> Int64 {
    nd_syscall(n, a, b, c, d, e, f)
}

func trap(_ n: Int64, _ a: UInt64 = 0, _ b: UInt64 = 0, _ c: UInt64 = 0, _ d: UInt64 = 0,
          _ e: UInt64 = 0, _ f: UInt64 = 0, _ g: UInt64 = 0, _ h: UInt64 = 0) -> UInt64 {
    nd_mach_trap(n, a, b, c, d, e, f, g, h)
}

func put(_ s: StaticString) {
    _ = syscall(sysWrite, console, UInt64(UInt(bitPattern: s.utf8Start)), UInt64(s.utf8CodeUnitCount))
}

/// Writes `v` in hex, formatted in the 16 bytes at `scratch`.
func putHex(_ v: UInt64, _ scratch: UnsafeMutableRawPointer) {
    let digits: StaticString = "0123456789abcdef"
    for i in 0..<16 {
        let nibble = Int((v >> UInt64(60 - 4 * i)) & 0xf)
        scratch.storeBytes(of: digits.utf8Start[nibble], toByteOffset: i, as: UInt8.self)
    }
    put("0x")
    _ = syscall(sysWrite, console, UInt64(UInt(bitPattern: scratch)), 16)
}

func fail(_ check: Int32, _ why: StaticString) -> Never {
    put("pid1: FAIL: ")
    put(why)
    put("\n")
    _ = syscall(sysExit, UInt64(check))
    while true {}
}

func pack(_ lo: UInt32, _ hi: UInt32) -> UInt64 { UInt64(hi) << 32 | UInt64(lo) }

@_cdecl("pid1_main")
func pid1Main() -> Never {
    // 1. Process 1.
    guard syscall(sysGetpid) == 1 else { _ = syscall(sysExit, 1); while true {} }

    // 2. The console: nothing can be reported before it opens.
    let path: StaticString = "/dev/console"
    let fd = syscall(sysOpen, UInt64(UInt(bitPattern: path.utf8Start)), oRDWR)
    guard fd >= 0 else { _ = syscall(sysExit, 2); while true {} }
    console = UInt64(fd)
    put("pid1: hello from userland: PID 1 on /dev/console\n")

    // 3. Anonymous memory: one page kept as scratch and message buffer, one
    // mapped, filled and unmapped.
    let scratchAddress = syscall(sysMmap, 0, page, protRW, mapPrivateAnon, UInt64(bitPattern: -1), 0)
    guard scratchAddress > 0 else { fail(3, "mmap of an anonymous page") }
    let scratch = UnsafeMutableRawPointer(bitPattern: UInt(scratchAddress))!
    let probeAddress = syscall(sysMmap, 0, 4 * page, protRW, mapPrivateAnon, UInt64(bitPattern: -1), 0)
    guard probeAddress > 0 else { fail(3, "mmap of four anonymous pages") }
    let probe = UnsafeMutableRawPointer(bitPattern: UInt(probeAddress))!
    let words = Int(4 * page) / 8
    for i in 0..<words { probe.storeBytes(of: UInt64(i) &* 0x9e37_79b9_7f4a_7c15, toByteOffset: i * 8, as: UInt64.self) }
    for i in 0..<words where probe.load(fromByteOffset: i * 8, as: UInt64.self) != UInt64(i) &* 0x9e37_79b9_7f4a_7c15 {
        fail(3, "anonymous memory did not hold its pattern")
    }
    guard syscall(sysMunmap, UInt64(probeAddress), 4 * page) == 0 else { fail(3, "munmap") }
    put("pid1: vm: mmap, fault-in and munmap of anonymous memory\n")

    // 4. Mach traps.
    let task = UInt32(truncatingIfNeeded: trap(trapTaskSelf))
    guard task != 0 else { fail(4, "task_self_trap returned MACH_PORT_NULL") }
    let port = UInt32(truncatingIfNeeded: trap(trapMachReplyPort))
    guard port != 0 else { fail(4, "mach_reply_port returned MACH_PORT_NULL") }
    put("pid1: mach: task port ")
    putHex(UInt64(task), scratch + 0x2000)
    put(", reply port ")
    putHex(UInt64(port), scratch + 0x2000)
    put("\n")

    // 5. A message to our own receive right, sent and received in one
    // mach_msg2 call, into and out of the scratch page.
    let msg = scratch
    let bits = machMsgTypeMakeSend  // MACH_MSGH_BITS(MAKE_SEND, 0)
    msg.storeBytes(of: bits, toByteOffset: 0, as: UInt32.self)
    msg.storeBytes(of: messageSize, toByteOffset: 4, as: UInt32.self)
    msg.storeBytes(of: port, toByteOffset: 8, as: UInt32.self)  // remote
    msg.storeBytes(of: UInt32(0), toByteOffset: 12, as: UInt32.self)  // local
    msg.storeBytes(of: UInt32(0), toByteOffset: 16, as: UInt32.self)  // voucher
    msg.storeBytes(of: messageID, toByteOffset: 20, as: UInt32.self)
    msg.storeBytes(of: payload, toByteOffset: 24, as: UInt64.self)
    let receiveLimit: UInt32 = 0x100
    let kr = trap(trapMachMsg2, UInt64(UInt(bitPattern: msg)),
                  mach64SendMsg | mach64RcvMsg | mach64SendMQCall,
                  pack(bits, messageSize), pack(port, 0), pack(0, messageID),
                  pack(0, port), pack(receiveLimit, 0), 0)
    guard kr == 0 else {
        put("pid1: mach_msg2 returned ")
        putHex(kr, scratch + 0x2000)
        put("\n")
        fail(5, "Mach message round trip")
    }
    guard msg.load(fromByteOffset: 20, as: UInt32.self) == messageID,
          msg.load(fromByteOffset: 4, as: UInt32.self) == messageSize,
          msg.load(fromByteOffset: 24, as: UInt64.self) == payload else {
        fail(5, "the received Mach message differs from the one sent")
    }
    put("pid1: mach: message round trip through mach_msg2\n")

    put("pid1: all checks passed\n")

    // PID 1 must not exit: wait on a port no message will reach.
    while true {
        _ = trap(trapMachMsg2, UInt64(UInt(bitPattern: msg)), mach64RcvMsg,
                 0, 0, 0, pack(0, port), pack(receiveLimit, 0), 0)
    }
}
