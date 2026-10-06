/* SPDX-License-Identifier: BSD-2-Clause */
// NeoDarwin-Language: portability: a FreeBSD header the tests include and xnu doesn't have.
/* <sys/module.h> for FreeBSD tests that include it without using it
 * (sbin/pfctl's pfctl_test): xnu has no kld(2). */
#ifndef ND_TESTS_SYS_MODULE_H
#define ND_TESTS_SYS_MODULE_H
#endif
