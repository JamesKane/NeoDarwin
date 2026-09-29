// SPDX-License-Identifier: BSD-2-Clause
//
// hello (P1-08b): the first dynamically linked NeoDarwin program. The kernel
// runs it as /sbin/launchd from the HFS+ root; it starts through dyld, which
// loads libSystem.B and the libsystem_* libraries under it, all built from
// Apple's source (docs/base/libsystem.md). Embedded Swift, as the static PID 1
// before it, but here the stdlib's runtime calls reach libSystem: print()
// is putchar and allocation is posix_memalign. It checks, in order, that
//
//   1. libSystem's stdio writes to the console, which PID 1 opens itself;
//   2. malloc, memset, strlen and free answer;
//   3. a pthread runs and is joined;
//   4. dyld answers: dlopen of libSystem.B returns a handle, dlsym finds
//      getpid at the address the executable was bound to, and the image list
//      holds the executable and its libraries;
//   5. libdispatch runs a function on a global queue, which exercises the
//      kernel's pthread workqueue.
//
// A failed check prints why and exits with its number (as PID 1, the kernel
// then panics with "initproc exited"). On success it says so; as PID 1 it
// then waits for good, as PID 1 must not exit.

import LibSystem

func fail(_ check: Int32, _ why: StaticString) -> Never {
    print("hello: FAIL: \(why)")
    exit(check)
}

@main
struct Hello {
    static func main() {
        // 1. The console. PID 1 starts without open files.
        let pid = getpid()
        if pid == 1 && !nd_attach_console() { exit(1) }
        nd_unbuffer_stdout()
        print("hello: hello, world from dyld and libSystem, as PID \(pid)")

        // 2. The allocator.
        guard let block = malloc(4096) else { fail(2, "malloc") }
        memset(block, 0x6e, 4095)
        block.storeBytes(of: 0, toByteOffset: 4095, as: UInt8.self)
        guard strlen(block.assumingMemoryBound(to: CChar.self)) == 4095 else { fail(2, "strlen of a malloc'd block") }
        free(block)
        print("hello: malloc: allocated, filled and freed")

        // 3. A thread.
        var thread: pthread_t? = nil
        var answer = 0
        guard pthread_create(&thread, nil, { arg in
            arg.storeBytes(of: 42, as: Int.self)
            return nil
        }, &answer) == 0, let thread else { fail(3, "pthread_create") }
        guard pthread_join(thread, nil) == 0, answer == 42 else { fail(3, "pthread_join") }
        print("hello: pthread: created and joined")

        // 4. dyld.
        guard let libSystem = dlopen("/usr/lib/libSystem.B.dylib", RTLD_NOW) else { fail(4, "dlopen of libSystem.B") }
        let bound = unsafeBitCast(getpid as @convention(c) () -> pid_t, to: UnsafeMutableRawPointer.self)
        guard dlsym(libSystem, "getpid") == bound else { fail(4, "dlsym of getpid") }
        let images = _dyld_image_count()
        guard images > 2 else { fail(4, "dyld's image list") }
        print("hello: dyld: dlopen and dlsym answer; \(images) images loaded")

        // 5. libdispatch.
        guard nd_dispatch_roundtrip() == 0 else { fail(5, "a function on a dispatch global queue") }
        print("hello: dispatch: ran a function on a global queue")

        print("hello: all checks passed")
        if pid == 1 {
            while true { pause() }
        }
    }
}
