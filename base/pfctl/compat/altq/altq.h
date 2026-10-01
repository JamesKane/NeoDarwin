// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the ALTQ constants OpenBSD's pfctl parser names, supplied in C because xnu has no ALTQ and its headers aren't built.
//
// pfctl's parser (parse.y) names ALTQ's scheduler types and class flags
// when it reads `altq` and `queue` rules. xnu has no ALTQ (it answers the
// requests ENODEV), so pfctl's queueing code isn't built
// (base/pfctl/src/nd_pfctl_noaltq.c); these are the names with OpenBSD's
// values, which nothing passes to the kernel.
#ifndef ND_PFCTL_ALTQ_H
#define ND_PFCTL_ALTQ_H

#define ALTQT_NONE              0
#define ALTQT_CBQ               1
#define ALTQT_HFSC              8
#define ALTQT_PRIQ              11

#define CBQCLF_RED              0x0001
#define CBQCLF_ECN              0x0002
#define CBQCLF_RIO              0x0004
#define CBQCLF_BORROW           0x0020
#define CBQCLF_DEFCLASS         0x2000

#define PRCF_RED                0x0001
#define PRCF_ECN                0x0002
#define PRCF_RIO                0x0004
#define PRCF_DEFAULTCLASS       0x1000

#define HFCF_RED                0x0001
#define HFCF_ECN                0x0002
#define HFCF_RIO                0x0004
#define HFCF_DEFAULTCLASS       0x1000

#endif
