// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the usbuf_ names macOS's closed libsbuf exports for FreeBSD's sbuf(9) API.
//
// macOS's /usr/lib/libsbuf.dylib exports FreeBSD's userland sbuf functions
// as usbuf_* (its libsbuf.tbd), and its private <usbuf.h> maps the sbuf_*
// names onto them, so they don't clash with xnu's struct sbuf in
// <sys/sbuf.h>, which is the kernel's older layout. Neither is published;
// this is that mapping, for libsbuf's build (force-included) and <usbuf.h>.
#ifndef ND_USBUF_NAMES_H
#define ND_USBUF_NAMES_H
#define sbuf usbuf
#define sbuf_new usbuf_new
#define sbuf_get_flags usbuf_get_flags
#define sbuf_clear_flags usbuf_clear_flags
#define sbuf_set_flags usbuf_set_flags
#define sbuf_clear usbuf_clear
#define sbuf_setpos usbuf_setpos
#define sbuf_bcat usbuf_bcat
#define sbuf_bcpy usbuf_bcpy
#define sbuf_cat usbuf_cat
#define sbuf_cpy usbuf_cpy
#define sbuf_printf usbuf_printf
#define sbuf_vprintf usbuf_vprintf
#define sbuf_nl_terminate usbuf_nl_terminate
#define sbuf_putc usbuf_putc
#define sbuf_set_drain usbuf_set_drain
#define sbuf_drain usbuf_drain
#define sbuf_trim usbuf_trim
#define sbuf_error usbuf_error
#define sbuf_finish usbuf_finish
#define sbuf_data usbuf_data
#define sbuf_len usbuf_len
#define sbuf_done usbuf_done
#define sbuf_delete usbuf_delete
#define sbuf_start_section usbuf_start_section
#define sbuf_end_section usbuf_end_section
#define sbuf_hexdump usbuf_hexdump
#define sbuf_count_drain usbuf_count_drain
#define sbuf_printf_drain usbuf_printf_drain
#define sbuf_putbuf usbuf_putbuf

// FreeBSD <sys/_types.h>, <sys/cdefs.h> and <sys/param.h> names that
// FreeBSD's <sys/sbuf.h> and subr_sbuf.c use and Darwin's headers lack.
#include <sys/_types.h>
#ifndef __va_list
#define __va_list __darwin_va_list
#endif
#ifndef __predict_false
#define __predict_false(exp) __builtin_expect((exp), 0)
#define __predict_true(exp) __builtin_expect((exp), 1)
#endif
#ifndef roundup2
#define roundup2(x, y) (((x) + ((y) - 1)) & (~((__typeof(x))(y) - 1)))
#endif
#endif
