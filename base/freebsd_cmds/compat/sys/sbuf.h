// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: FreeBSD's userland <sys/sbuf.h>, which is libsbuf's API under macOS's usbuf_ names.
//
// xnu's <sys/sbuf.h> is the kernel's older sbuf. FreeBSD's programs mean
// libsbuf's (base/libsbuf), which <usbuf.h> declares; link -lsbuf.
#ifndef ND_SYS_SBUF_H
#define ND_SYS_SBUF_H
#include <usbuf.h>
#endif
