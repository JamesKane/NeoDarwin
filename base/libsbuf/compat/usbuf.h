// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: macOS's private <usbuf.h>, which shell_cmds' apply and w include after <sys/sbuf.h>.
//
// FreeBSD's userland sbuf API under the usbuf_ names libsbuf exports
// (usbuf_names.h). The commands include xnu's <sys/sbuf.h> first, whose
// struct sbuf is the kernel's; after this header, struct sbuf names
// FreeBSD's (struct usbuf), declared by FreeBSD's <sys/sbuf.h>, installed
// as usbuf_sbuf.h. Its SBUF_* flags match xnu's where both define them.
#ifndef ND_USBUF_H
#define ND_USBUF_H
#include <usbuf_names.h>
#pragma push_macro("_SYS_SBUF_H_")
#undef _SYS_SBUF_H_
#include <usbuf_sbuf.h>
#pragma pop_macro("_SYS_SBUF_H_")
#endif
