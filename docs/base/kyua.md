<!-- SPDX-License-Identifier: BSD-2-Clause -->
# kyua, ATF and /usr/tests (P4-21 checkpoint 7)

**Goal.** FreeBSD's test framework in the base, and FreeBSD's tests for the base's programs run on QEMU and held to a reviewed result per test case, so that the base can only get better (`docs/architecture/freebsd-parity.md` §2.1, cp7). The design is the OpenZFS suite's (`docs/architecture/filesystems.md` §7.2).

## 1. The stack: `//base:kyua_commands`

Built by `base/kyua/build.sh` from FreeBSD's source drop at `050683bb8e13` (`base/kyua/freebsd.lock`, `@freebsd_kyua`), as FreeBSD's Makefiles build it. Everything links statically into the programs; nothing new goes into `/usr/lib`.

| Piece | Source | FreeBSD builds it as | Here |
|---|---|---|---|
| libatf-c, libatf-c++ | contrib/atf (ATF 0.26) | `PRIVATELIB`s (`libprivateatf-c.so.1`, `libprivateatf-c++.so.2`) | static archives, build-only (`usr/local/lib` of the install tree), for the test programs |
| `atf-sh`, `atf-check` | contrib/atf/atf-sh | `/usr/libexec` (libexec/atf) | `/usr/libexec/atf-sh`, `/usr/libexec/atf-check`, `/usr/share/atf/libatf-sh.subr` |
| lutok | contrib/lutok | an `INTERNALLIB` | linked into kyua |
| SQLite | contrib/sqlite3 (3.53.3) | a `PRIVATELIB` (lib/libsqlite3) | linked into kyua |
| Lua | `@puc_rio_lua`, 5.4.9 (base/vis's pin; FreeBSD's contrib/lua is 5.4.8, which lutok and kyua equally accept) | an `INTERNALLIB` (lib/liblua) | linked into kyua, `LUA_USE_POSIX` |
| `kyua` | contrib/kyua (0.13) with usr.bin/kyua's `config.h` | `/usr/bin/kyua` | `/usr/bin/kyua` (2.4 MB), its 14 pages, `/etc/kyua/kyua.conf`, `/usr/share/kyua/{misc,store}`, `/usr/share/examples/kyua`, `/usr/share/doc/kyua` |

`atf-sh` and `atf-check` stay in `/usr/libexec`, where FreeBSD has them: every FreeBSD shell test starts with `#! /usr/libexec/atf-sh` (share/mk/atf.test.mk), and `atf_check` runs `/usr/libexec/atf-check`.

Differences from FreeBSD's build:
- *SQLite:* no `HAVE_POSIX_FALLOCATE` (xnu has no `posix_fallocate`), and `SQLITE_ENABLE_LOCKING_STYLE=0`: plain POSIX advisory locks, as on FreeBSD (sqlite3.c turns Apple's AFP and proxy locking on for `__APPLE__`).
- *kyua's `config.h`:* `LAST_SIGNO` is 31 (xnu's `NSIG` is 32; configure probes it, and FreeBSD's 128 makes kyua's signal setup fail), and the memory query is `hw.memsize` (FreeBSD's `hw.usermem`).
- *kyua's os/freebsd:* the kmods requirement checker and `kyua prepare`'s kmods handler need kld(2) and are registered only `#ifdef __FreeBSD__`; they aren't built. Jails: `execenv_jail_stub.cpp`, as in a `WITHOUT_JAIL` build (`execenv="jail"` tests fail to run, saying so).
- *`/etc/kyua/kyua.conf`:* FreeBSD's `kyua.conf-default` with `unprivileged_user = 'nobody'`. FreeBSD names its `tests` account, which NeoDarwin's `/etc/master.passwd` doesn't have yet (accounts are P4-22), and kyua refuses any configuration that names a missing user. Tests with `required_user="unprivileged"` run as `nobody`.

## 2. The tests: `//base:freebsd_tests`

`base/freebsd_tests/build.sh` installs `/usr/tests` from `@freebsd_tests` (`base/freebsd_tests/freebsd.lock`): one line per program's `tests/Makefile`, in the words of share/mk's `atf.test.mk`, `tap.test.mk` and `netbsd-tests.test.mk` (`atf_sh`, `netbsd_sh`, `tap_sh`, `plain_sh`, `atf_c`, `plain_c`, `prog`, `script`, `files`, `mkfiles` (a Makefile's `${PACKAGE}FILES`), `tree_files`, `meta`). It writes each directory's `Kyuafile` as `suite.test.mk` generates it (a `TESTS_SUBDIRS` parent's, as `bin/sh`'s, `include()`s its subdirectories'), and installs `tests/Kyuafile` (auto-discovery) in `/usr/tests`, `/usr/tests/bin`, `/usr/tests/sbin`, `/usr/tests/usr.bin` and `/usr/tests/usr.sbin`, as FreeBSD's `bin/tests` and friends do (`KYUAFILE=yes`), with `usr.bin/tests`' `regress.m4`. C tests are compiled with `TARGET_FLAGS` and base/freebsd_cmds' compat layer (`nd_freebsd.h`, which now has `pipe2()`), linked with `libatf-c.a`. `base/freebsd_tests/compat` has the tests' own shims: `nd_pipe_socketpair.h` (`pipe()` as a socketpair, for tests that use both ends of a pipe, which FreeBSD's pipes allow: pfctl_test, pwait_reap), an empty `<sys/module.h>`, and `libarchive/config.h` (FreeBSD's `config_freebsd.h` less what Darwin lacks, for the libarchive test drivers `bsdcat_test`, `bsdcpio_test`, `bsdtar_test` and `bsdunzip_test`, which link the base's libarchive). A program that can't run here (a Perl or pytest test, a test of code the base doesn't build, as sockstat's and ping's unit tests) is left out and listed in build.sh's header and freebsd-parity.md §2.1. A program whose cases hang gets a shorter `timeout` with `meta`, as FreeBSD's `TIMEOUT`.

The tree isn't in `system_root`; `//images:freebsd_test_disk` (the session disk plus `/usr/tests`, a 256 MB volume) installs it. `//base:base_isa_audit` audits its programs too.

**Adding a program's tests:** add its `tests/` files (and any contrib/netbsd-tests or other sources its Makefile names) to `base/freebsd_tests/freebsd.lock` (`git show 050683bb8e13:PATH | shasum -a 256` in a freebsd-src checkout), write its lines in `build.sh` from its Makefile, add its directory to `groups.txt` with a shard, run the suite (below) and record every case in `expected.tsv`. A case that isn't `PASS` or `XFAIL` gets its class before the reason's category: `structural:`, `fixable:` or (a `FLAKY` case that fails only under load) `timing:`, as defined in docs/architecture/freebsd-parity.md §2.1; `//base:freebsd_tests_exit_check` fails on an unmarked one.

## 3. The suite: `//kernel:sbsa_freebsd_tests_test`

`base/freebsd_tests/suite_test.sh`, sharded (15; `groups.txt` gives each directory its shard, each under about 7 minutes) like `//kernel:sbsa_zfs_suite_test`; tagged `manual`, `kernel`, `qemu`:
- boots the test disk on QEMU virt (2 GB, 2 CPUs), logs in as root, sets `vm.shared_region_trace_level=0` (xnu reported every exec's failed shared-cache check on the console; kernel patch 0045 traces that at INFO now, and the sysctl stays, harmless) and runs `kyua test -r /tmp/kyua.db -k /usr/tests/Kyuafile DIR...` for the shard's directories (`groups.txt`);
- prints `kyua report` with every result (passed too) between `KYUA-RESULTS-BEGIN` and `KYUA-RESULTS-END`, which the host parses (falling back on kyua test's progress lines);
- holds every case, `PROGRAM:CASE`, to `expected.tsv` (`PROGRAM:CASE`, `RESULT`, `REASON`; `RESULT` is `PASS`, `XFAIL`, `FAIL`, `BROKEN`, `SKIP`, `KILLED` or `FLAKY`). A regression (an expected `PASS` or `XFAIL` that isn't), an unexpected pass (raise the list) and drift (a case missing from either side) fail the test; a change between two failing results is reported;
- a hanging case is killed by kyua at its timeout (ATF's default, 300 s) and reported broken, "timed out": `KILLED`;
- writes `results.tsv` (`expected.tsv`'s form) and `serial.log` to the test's outputs.

```
bazel test //kernel:sbsa_freebsd_tests_test --test_output=errors
KYUA_GROUPS="usr.bin/wc bin/cat" bazel test //kernel:sbsa_freebsd_tests_test --test_env=KYUA_GROUPS --test_sharding_strategy=disabled
bazel test //kernel:sbsa_freebsd_tests_test --test_env=KYUA_VERBOSE=1   # serial.log gets every failing case's output
```
