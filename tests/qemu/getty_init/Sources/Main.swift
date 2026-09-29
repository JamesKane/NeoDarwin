// SPDX-License-Identifier: BSD-2-Clause
//
// getty_init (P1-08, checkpoint 1): a test-only PID 1 that proves the
// interactive path on the kernel before launchd does the job. It runs
// /usr/libexec/getty on the console, as launchd's com.apple.getty.plist
// does, and starts it again whenever it exits, as init does. getty takes
// the console as its controlling terminal (login_tty) and runs login, which
// runs root's shell, /bin/sh. //kernel:sbsa_shell_boot_test logs in and
// types commands.

import LibSystem

@main
struct GettyInit {
    static func main() {
        guard nd_attach_console() else { exit(1) }
        print("getty_init: PID \(getpid()) on /dev/console; starting getty")
        var arguments: [UnsafeMutablePointer<CChar>?] = [strdup("getty"), strdup("std.9600"), strdup("console"), nil]
        // PID 1 starts with an empty environment; launchd gives its jobs PATH.
        var environment: [UnsafeMutablePointer<CChar>?] = [strdup("PATH=/usr/bin:/bin:/usr/sbin:/sbin"), nil]
        while true {
            var child: pid_t = 0
            let error = posix_spawn(&child, "/usr/libexec/getty", nil, nil, &arguments, &environment)
            guard error == 0 else {
                print("getty_init: posix_spawn of getty failed: \(String(cString: strerror(error)))")
                sleep(5)
                continue
            }
            var status: Int32 = 0
            while waitpid(child, &status, 0) < 0 && __error().pointee == EINTR {}
            print("getty_init: getty exited (status \(status)); starting it again")
        }
    }
}
