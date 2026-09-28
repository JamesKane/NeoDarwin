// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the toolkit's C ABI is the cross-language boundary (language-policy.md §3) for C, C++, SDL3's NeoDarwin backend and out-of-tree bindings
/*
 * ndtk.h - the NeoDarwin toolkit C ABI, API version 1.
 *
 * Normative text: docs/desktop/toolkit-api.md (requirement ids TK-*). Where
 * this header and the text differ, the text is normative and this header is
 * a defect (spec-conventions.md §5). The Swift surface is NDToolkit.swift.
 *
 * Rules that hold for every declaration below (toolkit-api.md §4.0):
 * - Handles are uint64_t: kind in bits 56-63, generation in bits 32-55
 *   (never 0), slot index in bits 0-31 (TK-HDL). 0 is never a handle. A
 *   stale, foreign or wrong-kind handle makes the call a diagnosed no-op.
 * - Calls that create an object return its handle, or 0. Other calls return
 *   bool, a count, or an ndtk_error. On failure ndtk_last_error() and
 *   ndtk_error_detail() hold the reason for the calling thread (TK-ERR).
 * - Every time and deadline is nanoseconds on the SC clock
 *   (mach_absolute_time scaled; scheduling-contract.md SC-TIME-001/002).
 * - Pointers returned in events (ndtk_event.data) and every
 *   ndtk_event array are valid until the next ndtk_wait or ndtk_poll on the
 *   same loop (TK-MEM).
 * - Calls marked [T2] take no lock and make no allocation; calls marked
 *   [RT] may also be made from an audio render callback (TK-T2).
 *
 * Audio records are the audio service's (docs/audio/nd_audio.h): this
 * header only forward-declares them. WebGPU objects are webgpu.h's opaque
 * pointer types (struct WGPU*Impl), also forward-declared. Neither header is
 * included, so ndtk.h stays freestanding: <stdint.h>, <stddef.h>,
 * <stdbool.h> only.
 *
 * LP64 only (event records carry pointers). C23, and valid C++20.
 */
#ifndef NDTK_H
#define NDTK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The header is C23. Importers that parse it in an older mode (the Swift
 * ClangImporter) get the C11 spelling of the keyword. */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L) && !defined(static_assert)
#define static_assert _Static_assert
#endif

#ifdef __cplusplus
extern "C" {
#endif

static_assert(sizeof(void *) == 8, "ndtk.h is LP64 only");

#define NDTK_API_VERSION 1u

/* ---------------------------------------------------------------------
 * Handles [TK-HDL-001]..[TK-HDL-006]
 */
typedef uint64_t ndtk_handle;
typedef ndtk_handle ndtk_loop;
typedef ndtk_handle ndtk_window;
typedef ndtk_handle ndtk_timer;
typedef ndtk_handle ndtk_source;
typedef ndtk_handle ndtk_output;
typedef ndtk_handle ndtk_gamepad;
typedef ndtk_handle ndtk_surface;
typedef ndtk_handle ndtk_gpu;
typedef ndtk_handle ndtk_ui;
typedef ndtk_handle ndtk_node;
typedef ndtk_handle ndtk_io;
typedef ndtk_handle ndtk_dialog;
typedef ndtk_handle ndtk_thread;
/* Audio streams, buffers and voices are the audio service's AUhandle
 * values, passed through unchanged (toolkit-api.md §4.10). */
typedef uint64_t ndtk_au_handle;

#define NDTK_HANDLE_NONE                ((ndtk_handle)0)
#define NDTK_HANDLE_KIND(h)             ((uint32_t)(((uint64_t)(h)) >> 56))
#define NDTK_HANDLE_GEN(h)              ((uint32_t)((((uint64_t)(h)) >> 32) & 0xffffffu))
#define NDTK_HANDLE_INDEX(h)            ((uint32_t)(((uint64_t)(h)) & 0xffffffffu))
#define NDTK_HANDLE_MAKE(kind, gen, idx) ((ndtk_handle)((((uint64_t)(kind) & 0xffu) << 56) | \
                                        (((uint64_t)(gen) & 0xffffffu) << 32) | (uint64_t)(uint32_t)(idx)))

/* Toolkit kinds are 0x40-0x7f; 0x01-0x3f belong to the audio service. */
enum ndtk_kind : uint8_t {
	NDTK_KIND_LOOP    = 0x40,
	NDTK_KIND_WINDOW  = 0x41,
	NDTK_KIND_TIMER   = 0x42,
	NDTK_KIND_SOURCE  = 0x43,
	NDTK_KIND_OUTPUT  = 0x44,
	NDTK_KIND_GAMEPAD = 0x45,
	NDTK_KIND_SURFACE = 0x46,
	NDTK_KIND_GPU     = 0x47,
	NDTK_KIND_UI      = 0x48,
	NDTK_KIND_NODE    = 0x49,
	NDTK_KIND_IO      = 0x4a,
	NDTK_KIND_DIALOG  = 0x4b,
	NDTK_KIND_THREAD  = 0x4c,
};

/* ---------------------------------------------------------------------
 * Errors [TK-ERR-001]..[TK-ERR-007]
 */
enum ndtk_error : int32_t {
	NDTK_OK            = 0,
	NDTK_E_HANDLE      = 1,   /* stale, foreign, wrong kind or 0: a diagnosed no-op */
	NDTK_E_ARG         = 2,   /* malformed argument or record size (WP badarg, SC_E_INVAL, AU_ERR_INVALID_ARG) */
	NDTK_E_STATE       = 3,   /* not valid in the object's state (AU_ERR_STATE) */
	NDTK_E_UNSUPPORTED = 4,   /* capability absent (WP unsupported, SC_E_NOTSUP, AU_ERR_UNSUPPORTED) */
	NDTK_E_PERMISSION  = 5,   /* namespace or capability token missing (WP perm, AU_ERR_PERMISSION) */
	NDTK_E_NOMEM       = 6,
	NDTK_E_LIMIT       = 7,   /* table, queue or service limit reached (AU_ERR_LIMIT) */
	NDTK_E_BUSY        = 8,   /* would block: latency bound, concurrent wait (WP busy, AU_ERR_BUSY) */
	NDTK_E_GONE        = 9,   /* the server object ended: window deleted, pad removed (WP gone) */
	NDTK_E_SERVICE     = 10,  /* wsys or audiod unreachable, or a protocol error */
	NDTK_E_ADMISSION   = 11,  /* real-time admission refused (AU_ERR_ADMISSION, SC_E_REFUSED) */
	NDTK_E_FORMAT      = 12,  /* format not convertible (AU_ERR_FORMAT, AU_ERR_PERIOD) */
	NDTK_E_DEVICE      = 13,  /* device absent or lost (AU_ERR_NO_DEVICE, AU_ERR_DEVICE_LOST) */
	NDTK_E_INTR        = 14,  /* a wait was interrupted by a signal (SC_E_INTR) */
	NDTK_E_TIMEOUT     = 15,
	NDTK_E_IO          = 16,  /* file or 9P I/O error; detail carries errno's text */
	NDTK_E_GPU         = 17,  /* webgpu.h reported an error; detail carries its message */
	NDTK_E_NOPRESS     = 18,  /* interactive move/resize without a press (WP nopress) */
	NDTK_E_NOFOCUS     = 19,  /* pointer lock without focus (WP nofocus) */
	NDTK_E_CANCELLED   = 20,  /* an asynchronous request was cancelled */
};

enum ndtk_error ndtk_last_error(void);          /* thread-local */
const char     *ndtk_error_detail(void);        /* thread-local UTF-8, never NULL, "" after success */
const char     *ndtk_error_name(enum ndtk_error e);

/* ---------------------------------------------------------------------
 * Versions and capabilities [TK-CAP-001]..[TK-CAP-006]
 */
enum ndtk_cap : uint64_t {
	NDTK_CAP_WINDOWS         = 1ull << 0,   /* wsys reachable, protocol 2 */
	NDTK_CAP_FRAME_FEEDBACK  = 1ull << 1,   /* WP caps: frame 2, configure 2 */
	NDTK_CAP_VIEWPORT        = 1ull << 2,   /* viewport 2 */
	NDTK_CAP_DECOR           = 1ull << 3,   /* decor 2 */
	NDTK_CAP_POPUP           = 1ull << 4,   /* popup 2 */
	NDTK_CAP_KEYMAP          = 1ull << 5,   /* keymap 2 */
	NDTK_CAP_IME             = 1ull << 6,   /* ime 2 */
	NDTK_CAP_PEN             = 1ull << 7,   /* pen 2 */
	NDTK_CAP_POINTER_LOCK    = 1ull << 8,   /* pointer 2 */
	NDTK_CAP_SCROLL          = 1ull << 9,   /* scroll 2 */
	NDTK_CAP_OUTPUTS         = 1ull << 10,  /* output 2 */
	NDTK_CAP_GAMEPAD         = 1ull << 11,  /* gamepad 2 */
	NDTK_CAP_GPU_SURFACE     = 1ull << 12,  /* surface.gpu 2 */
	NDTK_CAP_GPU_HELPER      = 1ull << 13,  /* ndtk_gpu_open: webgpu.h (Dawn) is present */
	NDTK_CAP_GPU_BUDGET      = 1ull << 14,  /* ndtk_gpu_budget (P7; false in v1 until P7 lands) */
	NDTK_CAP_AUDIO           = 1ull << 15,  /* /n/sys/audio reachable, cap:audio:play */
	NDTK_CAP_AUDIO_CAPTURE   = 1ull << 16,  /* AU_CAP_CAPTURE */
	NDTK_CAP_AUDIO_RT_GROUP  = 1ull << 17,  /* AU_CAP_RT_GROUP */
	NDTK_CAP_TIMER_PRECISE   = 1ull << 18,  /* SC_CAP_TIMER_PRECISE */
	NDTK_CAP_INTENT          = 1ull << 19,  /* SC_CAP_INTENT */
	NDTK_CAP_IO_ASYNC        = 1ull << 20,
	NDTK_CAP_PATH_WATCH      = 1ull << 21,
	NDTK_CAP_AGENT           = 1ull << 22,  /* /n/agent registration possible */
	NDTK_CAP_TEXT_SHAPING    = 1ull << 23,  /* always set in a conforming v1 */
	NDTK_CAP_DIALOG_FILE     = 1ull << 24,
	/* Reserved for later versions; reported false in version 1. */
	NDTK_CAP_GPU_BUFFER      = 1ull << 40,  /* buffer import/export (F-107, P7-06) */
	NDTK_CAP_TOUCH           = 1ull << 41,  /* not in window protocol v2 */
	NDTK_CAP_IMMEDIATE       = 1ull << 42,  /* the immediate builder (charter P8) */
	NDTK_CAP_ATTENTION       = 1ull << 43,  /* window attention request */
	NDTK_CAP_CLIPBOARD_TYPES = 1ull << 44,  /* clipboard beyond UTF-8 text */
};

uint32_t ndtk_api_version(void);                /* NDTK_API_VERSION of the running library */
uint64_t ndtk_capabilities(ndtk_loop loop);     /* OR of enum ndtk_cap; loop 0: process-wide subset */

/* The allocator hook: C ABI only, set before any other call [TK-MEM-006]. */
typedef struct ndtk_allocator {
	uint32_t size;                                  /*  0 sizeof(ndtk_allocator) */
	uint32_t reserved;                              /*  4 zero */
	void *(*alloc)(void *ctx, size_t size, size_t align);   /*  8 */
	void  (*free)(void *ctx, void *p, size_t size);          /* 16 */
	void  *ctx;                                     /* 24 */
} ndtk_allocator;                                   /* 32 bytes */

bool ndtk_set_allocator(const ndtk_allocator *a);

/* ---------------------------------------------------------------------
 * Time and threads [TK-TIME-001]..[TK-TIME-007]
 */
#define NDTK_DEADLINE_POLL  0ull           /* = SC_DEADLINE_POLL */
#define NDTK_DEADLINE_NONE  UINT64_MAX     /* = SC_DEADLINE_NONE */

/* Values equal enum sc_intent (nd_sched.h). */
enum ndtk_intent : uint32_t {
	NDTK_INTENT_NONE        = 0,
	NDTK_INTENT_INTERACTIVE = 1,
	NDTK_INTENT_THROUGHPUT  = 2,
	NDTK_INTENT_BACKGROUND  = 3,
	NDTK_INTENT_AUDIO       = 4,
};

typedef void (*ndtk_thread_fn)(void *arg);

uint64_t    ndtk_now(void);                                                     /* [T2] [RT] */
bool        ndtk_sleep_until(uint64_t deadline_ns, uint64_t leeway_ns);
ndtk_thread ndtk_thread_spawn(uint32_t intent, const char *name, ndtk_thread_fn fn, void *arg);
bool        ndtk_thread_join(ndtk_thread thread);
bool        ndtk_thread_set_intent(uint32_t intent);                             /* calling thread only */

/* ---------------------------------------------------------------------
 * Geometry. Logical rectangles are in points (float); pixel rectangles are
 * buffer pixels, half-open [x0, x1) x [y0, y1).
 */
typedef struct ndtk_rect  { float x, y, width, height; } ndtk_rect;           /* 16 bytes */
typedef struct ndtk_irect { int32_t x0, y0, x1, y1; } ndtk_irect;             /* 16 bytes */

/* ---------------------------------------------------------------------
 * Events [TK-EV-001]..[TK-EV-014]. One 96-byte record. Kinds are only
 * ever appended; a reader skips kinds it does not know.
 */
enum ndtk_event_kind : uint16_t {
	NDTK_EV_KEY_DOWN           = 1,   /* u.key          DE_KEY down */
	NDTK_EV_KEY_UP             = 2,   /* u.key          DE_KEY up */
	NDTK_EV_TEXT               = 3,   /* u.text, data   DE_TEXT */
	NDTK_EV_PREEDIT            = 4,   /* u.text, data   DE_PREEDIT: text, then ndtk_preedit_style runs */
	NDTK_EV_POINTER_MOTION     = 5,   /* u.pointer      DE_MOUSE, no button change */
	NDTK_EV_POINTER_DOWN       = 6,   /* u.pointer      DE_MOUSE, a button pressed */
	NDTK_EV_POINTER_UP         = 7,   /* u.pointer      DE_MOUSE, a button released */
	NDTK_EV_SCROLL             = 8,   /* u.scroll       DE_SCROLL */
	NDTK_EV_RELATIVE           = 9,   /* u.relative     DE_MOTION */
	NDTK_EV_CONFIGURE          = 10,  /* u.configure    DE_CONFIGURE */
	NDTK_EV_FRAME              = 11,  /* u.frame        DE_FRAME */
	NDTK_EV_GAMEPAD            = 12,  /* u.gamepad      DE_GAMEPAD, DE_PADBUTTON, DE_PADAXIS, DE_PADSENSOR */
	NDTK_EV_TIMER              = 13,  /* u.timer */
	NDTK_EV_FD                 = 14,  /* u.fd */
	NDTK_EV_MESSAGE            = 15,  /* u.message */
	NDTK_EV_WAKE               = 16,  /* no payload; coalesced */
	NDTK_EV_AUDIO              = 17,  /* u.audio: an AUevent, verbatim */
	NDTK_EV_CLOSE              = 18,  /* u.close        DE_CLOSE */
	NDTK_EV_QUIT               = 19,  /* no payload     DE_QUIT, or the session asks the app to quit */
	NDTK_EV_ACK                = 20,  /* u.ack, data    DE_ACK: error text */
	NDTK_EV_DELETE_SURROUNDING = 21,  /* u.delete_surrounding  DE_DELSURROUND */
	NDTK_EV_FOCUS              = 22,  /* u.focus        DE_FOCUS */
	NDTK_EV_POINTER_ENTER      = 23,  /* u.pointer      DE_ENTER */
	NDTK_EV_POINTER_LEAVE      = 24,  /* u.pointer      DE_LEAVE */
	NDTK_EV_STATE              = 25,  /* u.state        DE_STATE */
	NDTK_EV_MENU               = 26,  /* u.menu         DE_MENU */
	NDTK_EV_DROP               = 27,  /* u.drop, data   DE_DROP: paths, newline-terminated */
	NDTK_EV_POPUP_DONE         = 28,  /* u.popup_done   DE_POPUPDONE */
	NDTK_EV_POINTER_CONSTRAINT = 29,  /* u.constraint   DE_POINTER */
	NDTK_EV_PROXIMITY          = 30,  /* u.proximity    DE_PROXIMITY */
	NDTK_EV_KEYMAP             = 31,  /* u.keymap, data DE_KEYMAP: layout name */
	NDTK_EV_OUTPUT             = 32,  /* u.output, data DE_OUTPUT: output name */
	NDTK_EV_THEME              = 33,  /* u.theme        DE_THEME */
	NDTK_EV_IO                 = 34,  /* u.io */
	NDTK_EV_DIALOG             = 35,  /* u.dialog, data: chosen paths, newline-terminated */
	NDTK_EV_AGENT              = 36,  /* u.agent, data: the verb line */
	NDTK_EV_EXPOSE             = 37,  /* u.expose       DE_EXPOSE (non-retained windows only) */
	NDTK_EV_MOVE               = 38,  /* u.move         DE_MOVE */
	NDTK_EV_PATH               = 39,  /* u.path, data: the path */
	NDTK_EV_GPU                = 40,  /* u.gpu, data: webgpu.h's message */
	NDTK_EV_KIND_MAX
};

/* ndtk_event.flags */
#define NDTK_EVF_AGENT     0x0001u   /* synthesised from an /n/agent action, not a device */
#define NDTK_EVF_CONTINUES 0x0002u   /* text continues in the next NDTK_EV_TEXT (DTF_CONT) */
#define NDTK_EVF_LAGGING   0x0004u   /* the window's stream overflowed before this event (DS_LAGGING) */

/* Modifier bits: equal DK_* (desktop.h), state after the event. */
#define NDTK_MOD_SHIFT  0x0001u
#define NDTK_MOD_CTRL   0x0002u
#define NDTK_MOD_ALT    0x0004u
#define NDTK_MOD_META   0x0008u
#define NDTK_MOD_CAPS   0x0010u
#define NDTK_MOD_NUM    0x0020u
#define NDTK_MOD_RSHIFT 0x0100u
#define NDTK_MOD_RCTRL  0x0200u
#define NDTK_MOD_RALT   0x0400u
#define NDTK_MOD_RMETA  0x0800u
#define NDTK_MOD_LEVEL3 0x1000u

/* Key flags: equal DKF_*. */
#define NDTK_KEY_DOWN    0x01u
#define NDTK_KEY_REPEAT  0x02u
#define NDTK_KEY_NORUNE  0x04u
#define NDTK_KEY_IMEPASS 0x08u
#define NDTK_KEY_BOUND   0x10u
#define NDTK_KEY_CANCEL  0x20u

/* Keysyms the toolkit names (X11-compatible); printable keys carry their code point. */
#define NDTK_KEYSYM_BACKSPACE 0xff08u
#define NDTK_KEYSYM_TAB       0xff09u
#define NDTK_KEYSYM_RETURN    0xff0du
#define NDTK_KEYSYM_ESCAPE    0xff1bu
#define NDTK_KEYSYM_HOME      0xff50u
#define NDTK_KEYSYM_LEFT      0xff51u
#define NDTK_KEYSYM_UP        0xff52u
#define NDTK_KEYSYM_RIGHT     0xff53u
#define NDTK_KEYSYM_DOWN      0xff54u
#define NDTK_KEYSYM_PAGEUP    0xff55u
#define NDTK_KEYSYM_PAGEDOWN  0xff56u
#define NDTK_KEYSYM_END       0xff57u
#define NDTK_KEYSYM_DELETE    0xffffu

/* Pointer buttons: equal DB_* bits. */
#define NDTK_BUTTON_LEFT   0x01u
#define NDTK_BUTTON_MIDDLE 0x02u
#define NDTK_BUTTON_RIGHT  0x04u
#define NDTK_BUTTON_4      0x20u
#define NDTK_BUTTON_5      0x40u

/* Pointer flags: equal DMF_*. */
#define NDTK_POINTER_PEN    0x1u
#define NDTK_POINTER_WARPED 0x2u
#define NDTK_POINTER_HOVER  0x4u

/* Tools: equal Desktool. */
enum ndtk_tool : uint16_t {
	NDTK_TOOL_MOUSE = 0, NDTK_TOOL_PEN = 1, NDTK_TOOL_ERASER = 2, NDTK_TOOL_BRUSH = 3,
	NDTK_TOOL_PENCIL = 4, NDTK_TOOL_AIRBRUSH = 5, NDTK_TOOL_LENS = 6,
};

/* Scroll flags: equal DSF_*. */
#define NDTK_SCROLL_PRECISE  0x1u
#define NDTK_SCROLL_INVERTED 0x2u
#define NDTK_SCROLL_STOP     0x4u

/* Frame flags: equal DFF_*, plus one bit only a host shim sets. */
#define NDTK_FRAME_HWTIME     0x01u
#define NDTK_FRAME_COMPOSITED 0x02u
#define NDTK_FRAME_THROTTLED  0x04u
#define NDTK_FRAME_REQUESTED  0x08u
#define NDTK_FRAME_ZEROCOPY   0x10u
#define NDTK_FRAME_LATE       0x20u
#define NDTK_FRAME_ESTIMATED  0x10000u   /* host shims only; never set on NeoDarwin (TK-FRM-006) */

/* Window state bits: equal DS_*. */
#define NDTK_STATE_VISIBLE     0x001u
#define NDTK_STATE_FOCUSED     0x002u
#define NDTK_STATE_HIDDEN      0x004u
#define NDTK_STATE_ZOOMED      0x008u
#define NDTK_STATE_GRABBED     0x010u
#define NDTK_STATE_LAGGING     0x020u
#define NDTK_STATE_SNAPPED     0x040u
#define NDTK_STATE_FULL        0x080u
#define NDTK_STATE_CLOSING     0x100u
#define NDTK_STATE_INTERACTIVE 0x200u
#define NDTK_STATE_OCCLUDED    0x400u

/* Visibility: equal Deskvis. */
enum ndtk_visibility : uint32_t {
	NDTK_VIS_UNMAPPED = 0, NDTK_VIS_SHOWN = 1, NDTK_VIS_OCCLUDED = 2, NDTK_VIS_OFFSCREEN = 3, NDTK_VIS_HIDDEN = 4,
};

/* Gamepad buttons: equal Deskpadbuttons; axes in Deskpadaxes order. */
#define NDTK_PAD_A         (1u << 0)
#define NDTK_PAD_B         (1u << 1)
#define NDTK_PAD_X         (1u << 2)
#define NDTK_PAD_Y         (1u << 3)
#define NDTK_PAD_BACK      (1u << 4)
#define NDTK_PAD_GUIDE     (1u << 5)
#define NDTK_PAD_START     (1u << 6)
#define NDTK_PAD_LSTICK    (1u << 7)
#define NDTK_PAD_RSTICK    (1u << 8)
#define NDTK_PAD_LSHOULDER (1u << 9)
#define NDTK_PAD_RSHOULDER (1u << 10)
#define NDTK_PAD_UP        (1u << 11)
#define NDTK_PAD_DOWN      (1u << 12)
#define NDTK_PAD_LEFT      (1u << 13)
#define NDTK_PAD_RIGHT     (1u << 14)
enum ndtk_pad_axis : uint32_t {
	NDTK_PAD_LEFTX = 0, NDTK_PAD_LEFTY = 1, NDTK_PAD_RIGHTX = 2, NDTK_PAD_RIGHTY = 3,
	NDTK_PAD_LTRIGGER = 4, NDTK_PAD_RTRIGGER = 5, NDTK_PAD_NAXES = 6,
};
enum ndtk_pad_change : uint32_t {
	NDTK_PAD_CONNECTED = 1, NDTK_PAD_DISCONNECTED = 2, NDTK_PAD_BUTTON = 3, NDTK_PAD_AXIS = 4, NDTK_PAD_SENSOR = 5,
};

/* fd readiness and path-watch bits. */
#define NDTK_FD_READ   0x1u
#define NDTK_FD_WRITE  0x2u
#define NDTK_FD_EOF    0x4u
#define NDTK_PATH_WRITE  0x1u
#define NDTK_PATH_DELETE 0x2u
#define NDTK_PATH_RENAME 0x4u
#define NDTK_PATH_ATTRIB 0x8u

/* Payloads. Each fits the 56-byte union. */
typedef struct ndtk_key_event {
	uint32_t scancode;      /*  0 USB HID usage (WP-KEY-003) */
	uint32_t keysym;        /*  4 */
	uint32_t base;          /*  8 the key's rune with no modifiers, active layout */
	uint32_t rune;          /* 12 0 for modifiers and dead keys (NDTK_KEY_NORUNE) */
	uint32_t mods;          /* 16 NDTK_MOD_*, after the event */
	uint32_t flags;         /* 20 NDTK_KEY_* */
	uint32_t layout;        /* 24 active layout index */
	uint32_t reserved;      /* 28 */
} ndtk_key_event;           /* 32 bytes */

typedef struct ndtk_text_event {
	uint32_t ime_serial;    /*  0 */
	uint32_t flags;         /*  4 NDTK_TEXT_KEY: typed by a passed key, not a commit */
	int32_t  cursor_begin;  /*  8 preedit: byte offsets into the text; -1 hides the cursor */
	int32_t  cursor_end;    /* 12 */
	uint32_t text_len;      /* 16 bytes of text at the start of data */
	uint32_t style_count;   /* 20 preedit: ndtk_preedit_style runs after the text, 4-aligned */
	uint64_t reserved;      /* 24 */
} ndtk_text_event;          /* 32 bytes */
#define NDTK_TEXT_KEY 0x2u  /* = DTF_KEY */

typedef struct ndtk_preedit_style { uint32_t begin, end, style; } ndtk_preedit_style;   /* 12 bytes */

typedef struct ndtk_delete_surrounding_event {
	uint32_t ime_serial, before, after, reserved;
} ndtk_delete_surrounding_event;   /* 16 bytes */

typedef struct ndtk_pointer_event {
	float    x, y;          /*  0 window content, logical points */
	uint32_t buttons;       /*  8 NDTK_BUTTON_* held after the event */
	uint32_t button;        /* 12 the button that changed; 0 for motion */
	uint32_t mods;          /* 16 */
	uint32_t flags;         /* 20 NDTK_POINTER_* */
	uint16_t tool;          /* 24 enum ndtk_tool */
	uint16_t reserved0;     /* 26 */
	float    pressure;      /* 28 0..1 */
	float    tilt_x, tilt_y;/* 32 degrees */
	float    rotation;      /* 40 degrees */
	float    distance;      /* 44 0..1 */
	uint32_t tool_serial;   /* 48 */
	uint32_t reserved1;     /* 52 */
} ndtk_pointer_event;       /* 56 bytes */

typedef struct ndtk_scroll_event {
	float    dx, dy;        /*  0 points */
	int32_t  v120_x, v120_y;/*  8 wheel detents in 1/120 */
	uint32_t flags;         /* 16 NDTK_SCROLL_* */
	uint32_t mods;          /* 20 */
} ndtk_scroll_event;        /* 24 bytes */

typedef struct ndtk_relative_event {
	float dx, dy;           /*  0 unaccelerated device counts */
	float accel_dx, accel_dy;/* 8 accelerated, points */
} ndtk_relative_event;      /* 16 bytes */

typedef struct ndtk_constraint_event { uint32_t state, reason; } ndtk_constraint_event;   /* DPS_*, DPR_* */

typedef struct ndtk_proximity_event {
	uint32_t in;            /*  0 */
	uint32_t tool_serial;   /*  4 */
	uint32_t tool;          /*  8 enum ndtk_tool */
	uint32_t caps;          /* 12 */
	uint64_t hw_serial;     /* 16 */
} ndtk_proximity_event;     /* 24 bytes */

typedef struct ndtk_configure_event {
	float       width, height;              /*  0 logical content size, points */
	uint32_t    pixel_width, pixel_height;  /*  8 device pixels covered */
	uint32_t    scale_num, scale_den;       /* 16 scale = num / den */
	uint32_t    buffer_width, buffer_height;/* 24 buffer pixels */
	uint32_t    config_seq;                 /* 32 */
	uint32_t    state;                      /* 36 NDTK_STATE_* */
	uint32_t    visibility;                 /* 40 enum ndtk_visibility */
	uint32_t    refresh_ns;                 /* 44 frame clock interval (throttled if so) */
	ndtk_output output;                     /* 48 */
} ndtk_configure_event;                     /* 56 bytes */

typedef struct ndtk_frame_event {
	uint64_t frame;         /*  0 */
	uint64_t target_ns;     /*  8 a valid wait deadline (SC clock) */
	uint64_t presented_ns;  /* 16 actual presentation time of the last present shown; 0 if none */
	uint64_t refresh_ns;    /* 24 */
	uint32_t present;       /* 32 present number `presented_ns` refers to */
	uint32_t config_seq;    /* 36 config that present was tagged with */
	uint32_t flags;         /* 40 NDTK_FRAME_* */
	uint32_t missed;        /* 44 presents superseded unseen */
	uint64_t reserved;      /* 48 */
} ndtk_frame_event;         /* 56 bytes */

typedef struct ndtk_ack_event {
	uint32_t tag;           /*  0 */
	int32_t  error;         /*  4 enum ndtk_error */
	uint32_t desc_seq;      /*  8 */
	uint32_t config_seq;    /* 12 */
} ndtk_ack_event;           /* 16 bytes */

typedef struct ndtk_focus_event      { uint32_t focused, reserved; } ndtk_focus_event;
typedef struct ndtk_state_event      { uint32_t state, old_state; } ndtk_state_event;
typedef struct ndtk_move_event       { int32_t x, y; } ndtk_move_event;          /* content origin, desktop points */
typedef struct ndtk_close_event      { uint32_t forced, reserved; } ndtk_close_event;
typedef struct ndtk_menu_event       { uint32_t menu, item, checked, reserved; } ndtk_menu_event;
typedef struct ndtk_drop_event       { float x, y; uint32_t count, reserved; } ndtk_drop_event;
typedef struct ndtk_popup_done_event { uint32_t reason, reserved; } ndtk_popup_done_event;   /* DPD_* */
typedef struct ndtk_expose_event     { ndtk_irect rect; } ndtk_expose_event;
typedef struct ndtk_keymap_event     { uint32_t generation, layout, delay_ms, interval_us; } ndtk_keymap_event;
typedef struct ndtk_theme_event      { uint32_t generation, reserved; } ndtk_theme_event;

typedef struct ndtk_gamepad_event {
	ndtk_gamepad pad;       /*  0 */
	uint32_t change;        /*  8 enum ndtk_pad_change */
	uint32_t index;         /* 12 button bit number, axis, or sensor */
	float    value;         /* 16 button 0/1; stick -1..1 (+y down); trigger 0..1 */
	uint32_t buttons;       /* 20 NDTK_PAD_* held after the event */
	float    x, y, z;       /* 24 sensor sample */
	uint32_t caps;          /* 36 DGC_* on connection */
} ndtk_gamepad_event;       /* 40 bytes */

typedef struct ndtk_output_event {
	ndtk_output output;     /*  0 */
	uint32_t change;        /*  8 1 added, 2 removed, 3 changed */
	uint32_t flags;         /* 12 NDTK_OUTPUT_* */
	uint32_t scale_num, scale_den; /* 16 */
	uint32_t refresh_ns;    /* 24 */
	uint32_t reserved;      /* 28 */
	ndtk_rect rect;         /* 32 desktop points */
} ndtk_output_event;        /* 48 bytes */

typedef struct ndtk_timer_event {
	ndtk_timer timer;       /*  0 */
	uint64_t deadline_ns;   /*  8 the deadline that fired */
	uint64_t fired_ns;      /* 16 SC clock when found expired */
	uint32_t expirations;   /* 24 repeating: periods coalesced into this event */
	uint32_t reserved;      /* 28 */
} ndtk_timer_event;         /* 32 bytes */

typedef struct ndtk_fd_event {
	ndtk_source source;     /*  0 */
	int32_t  fd;            /*  8 */
	uint32_t ready;         /* 12 NDTK_FD_* */
	int64_t  data;          /* 16 bytes readable or writable, as kevent reports */
} ndtk_fd_event;            /* 24 bytes */

typedef struct ndtk_message { uint64_t a, b; } ndtk_message;   /* 16 bytes, copied */

typedef struct ndtk_audio_event { uint64_t au_event[4]; } ndtk_audio_event;   /* an AUevent, 32 bytes, verbatim */

typedef struct ndtk_io_event {
	ndtk_io  request;       /*  0 */
	uint64_t udata;         /*  8 */
	uint64_t bytes;         /* 16 transferred */
	int32_t  error;         /* 24 enum ndtk_error */
	uint32_t op;            /* 28 1 read, 2 write */
} ndtk_io_event;            /* 32 bytes */

typedef struct ndtk_dialog_event {
	ndtk_dialog dialog;     /*  0 */
	uint32_t result;        /*  8 1 accepted, 0 cancelled */
	uint32_t count;         /* 12 paths in data */
} ndtk_dialog_event;        /* 16 bytes */

typedef struct ndtk_agent_event {
	uint32_t request;       /*  0 reply with ndtk_agent_reply */
	uint32_t verb;          /*  4 enum ndtk_agent_verb; NDTK_AGENT_APP for an app verb */
	ndtk_node node;         /*  8 UI verbs */
	double   value;         /* 16 set_value */
} ndtk_agent_event;         /* 24 bytes */

typedef struct ndtk_path_event { ndtk_source source; uint32_t what, reserved; } ndtk_path_event;

enum ndtk_gpu_reason : uint32_t { NDTK_GPU_DEVICE_LOST = 1, NDTK_GPU_ERROR = 2, NDTK_GPU_PRESSURE = 3 };
typedef struct ndtk_gpu_event { ndtk_gpu gpu; uint32_t reason, reserved; } ndtk_gpu_event;

typedef struct ndtk_event {
	uint16_t    kind;       /*  0 enum ndtk_event_kind */
	uint16_t    flags;      /*  2 NDTK_EVF_* */
	uint32_t    seq;        /*  4 per loop, +1 per delivered event */
	ndtk_window window;     /*  8 0 for loop-level events */
	uint64_t    time_ns;    /* 16 SC clock: sample time for input, queue time otherwise */
	const uint8_t *data;    /* 24 variable payload, valid until the next wait; NULL if none */
	uint32_t    data_len;   /* 32 */
	uint32_t    reserved;   /* 36 */
	union {                 /* 40 */
		ndtk_key_event                key;
		ndtk_text_event               text;
		ndtk_delete_surrounding_event delete_surrounding;
		ndtk_pointer_event            pointer;
		ndtk_scroll_event             scroll;
		ndtk_relative_event           relative;
		ndtk_constraint_event         constraint;
		ndtk_proximity_event          proximity;
		ndtk_configure_event          configure;
		ndtk_frame_event              frame;
		ndtk_ack_event                ack;
		ndtk_focus_event              focus;
		ndtk_state_event              state;
		ndtk_move_event               move;
		ndtk_close_event              close;
		ndtk_menu_event               menu;
		ndtk_drop_event               drop;
		ndtk_popup_done_event         popup_done;
		ndtk_expose_event             expose;
		ndtk_keymap_event             keymap;
		ndtk_theme_event              theme;
		ndtk_gamepad_event            gamepad;
		ndtk_output_event             output;
		ndtk_timer_event              timer;
		ndtk_fd_event                 fd;
		ndtk_message                  message;
		ndtk_audio_event              audio;
		ndtk_io_event                 io;
		ndtk_dialog_event             dialog;
		ndtk_agent_event              agent;
		ndtk_path_event               path;
		ndtk_gpu_event                gpu;
		uint8_t                       reserved_[56];
	} u;
} ndtk_event;               /* 96 bytes */

/* ---------------------------------------------------------------------
 * Loop [TK-LOOP-001]..[TK-LOOP-017]
 */
#define NDTK_LOOP_AUTO_INTENT 0x1u   /* interactive while a window is shown, background when none is */

typedef struct ndtk_loop_desc {
	uint32_t size;            /*  0 sizeof(ndtk_loop_desc) */
	uint32_t flags;           /*  4 NDTK_LOOP_* */
	uint32_t event_capacity;  /*  8 events per wait (0: 256) */
	uint32_t arena_bytes;     /* 12 initial frame arena (0: 64 KiB) */
	uint32_t post_capacity;   /* 16 message ring slots (0: 1024) */
	uint32_t intent;          /* 20 enum ndtk_intent for the waiting thread; 0 leaves it */
	uint64_t reserved[2];     /* 24 zero */
} ndtk_loop_desc;             /* 40 bytes */

ndtk_loop   ndtk_loop_create(const ndtk_loop_desc *desc);                  /* desc may be NULL */
void        ndtk_loop_destroy(ndtk_loop loop);
int32_t     ndtk_wait(ndtk_loop loop, uint64_t deadline_ns, uint64_t leeway_ns,
                      const ndtk_event **events);                          /* [T2] count, or -1 */
int32_t     ndtk_poll(ndtk_loop loop, const ndtk_event **events);          /* [T2] */
bool        ndtk_wake(ndtk_loop loop);                                     /* any thread [RT] */
bool        ndtk_post(ndtk_loop loop, const ndtk_message *message);        /* any thread [RT] */
ndtk_timer  ndtk_timer_at(ndtk_loop loop, uint64_t deadline_ns, uint64_t leeway_ns, uint64_t repeat_ns);
bool        ndtk_timer_cancel(ndtk_timer timer);
ndtk_source ndtk_watch_fd(ndtk_loop loop, int32_t fd, uint32_t interest);  /* NDTK_FD_READ|WRITE */
ndtk_source ndtk_watch_port(ndtk_loop loop, uint32_t mach_port);           /* EVFILT_MACHPORT; ready = READ */
bool        ndtk_unwatch(ndtk_source source);
int32_t     ndtk_loop_fd(ndtk_loop loop);                                  /* the loop's kqueue, for nesting */

/* ---------------------------------------------------------------------
 * Callback driver [TK-DRV-001]..[TK-DRV-006]: SDL3's four callbacks.
 */
enum ndtk_app_result : int32_t { NDTK_APP_CONTINUE = 0, NDTK_APP_SUCCESS = 1, NDTK_APP_FAILURE = 2 };

typedef struct ndtk_app {
	uint32_t size;                                                           /*  0 */
	uint32_t reserved;                                                       /*  4 */
	enum ndtk_app_result (*init)(void **appstate, ndtk_loop loop, int argc, char **argv);   /*  8 */
	enum ndtk_app_result (*iterate)(void *appstate, ndtk_loop loop);                         /* 16 */
	enum ndtk_app_result (*event)(void *appstate, ndtk_loop loop, const ndtk_event *e);      /* 24 */
	void                 (*quit)(void *appstate, enum ndtk_app_result result);               /* 32 */
	const ndtk_loop_desc *loop;                                              /* 40 may be NULL */
} ndtk_app;                                                                  /* 48 bytes */

int32_t ndtk_run(const ndtk_app *app, int argc, char **argv);   /* exit status */

/* ---------------------------------------------------------------------
 * Windows and outputs [TK-WIN-001]..[TK-WIN-016]
 */
enum ndtk_window_kind : uint32_t {
	NDTK_WINDOW_TOPLEVEL = 0, NDTK_WINDOW_TRANSIENT = 1, NDTK_WINDOW_POPUP = 2, NDTK_WINDOW_TOOLTIP = 3,
};

#define NDTK_WIN_HIDDEN     0x0001u   /* do not show at open */
#define NDTK_WIN_NO_DECOR   0x0002u   /* decor none (hit regions) */
#define NDTK_WIN_FIXED_SIZE 0x0004u
#define NDTK_WIN_ALPHA      0x0008u   /* content has alpha (chan r8g8b8a8) */
#define NDTK_WIN_NO_FOCUS   0x0010u
#define NDTK_WIN_ON_TOP     0x0020u
#define NDTK_WIN_WANT_ACKS  0x0040u   /* deliver NDTK_EV_ACK for successful requests too */
#define NDTK_WIN_NO_RETAIN  0x0080u   /* server keeps no copy; NDTK_EV_EXPOSE is delivered */
#define NDTK_WIN_LOGICAL_BUFFER 0x0100u   /* `buffer logical` instead of the default `buffer device` */

enum ndtk_buffer_mode : uint32_t {  /* equal Deskbufmode; windows open with DEVICE */
	NDTK_BUFFER_LOGICAL = 0, NDTK_BUFFER_DEVICE = 1, NDTK_BUFFER_FIXED = 2,
};

typedef struct ndtk_window_desc {
	uint32_t    size;             /*  0 sizeof(ndtk_window_desc) */
	uint32_t    kind;             /*  4 enum ndtk_window_kind */
	const char *title;            /*  8 UTF-8, NUL-terminated; may be NULL */
	float       width, height;    /* 16 logical points */
	uint32_t    flags;            /* 24 NDTK_WIN_* */
	uint32_t    reserved0;        /* 28 zero */
	ndtk_window parent;           /* 32 transient, popup, tooltip */
	float       min_width, min_height;   /* 40 */
	float       max_width, max_height;   /* 48 0: unlimited */
	uint64_t    reserved;         /* 56 zero */
} ndtk_window_desc;               /* 64 bytes */

ndtk_window ndtk_window_open(ndtk_loop loop, const char *title, float width, float height, uint32_t flags);
ndtk_window ndtk_window_open_desc(ndtk_loop loop, const ndtk_window_desc *desc);
void        ndtk_window_close(ndtk_window window);
bool        ndtk_window_attach(ndtk_window window, ndtk_loop loop);       /* move to another loop */
uint32_t    ndtk_window_id(ndtk_window window);                           /* the wsys window id */

/* Requests. Each is applied or refused when the call returns (WP-ACK-001);
 * out_tag, when not NULL, receives the tag of the NDTK_EV_ACK that follows
 * the events the request caused. */
enum ndtk_request_op : uint32_t {
	NDTK_REQ_TITLE = 1,        /* text */
	NDTK_REQ_SIZE = 2,         /* rect.width, rect.height */
	NDTK_REQ_MOVE = 3,         /* rect.x, rect.y */
	NDTK_REQ_SHOW = 4, NDTK_REQ_HIDE = 5, NDTK_REQ_RAISE = 6, NDTK_REQ_LOWER = 7,
	NDTK_REQ_FOCUS = 8, NDTK_REQ_ZOOM = 9,
	NDTK_REQ_FULLSCREEN = 10,  /* value 1 on, 0 off */
	NDTK_REQ_MIN_SIZE = 11, NDTK_REQ_MAX_SIZE = 12,        /* rect.width, rect.height */
	NDTK_REQ_DECOR = 13,       /* value 1 server, 0 none */
	NDTK_REQ_HIT = 14,         /* value: enum ndtk_hit; rect; value2 edge */
	NDTK_REQ_HIT_CLEAR = 15,
	NDTK_REQ_INTERACTIVE_MOVE = 16,
	NDTK_REQ_INTERACTIVE_RESIZE = 17,   /* value2 edge, 0 nearest corner */
	NDTK_REQ_POINTER = 18,     /* value: 0 free, 1 lock, 2 confine (rect), 3 warp (rect.x, rect.y) */
	NDTK_REQ_KEY_REPEAT = 19,  /* value 1 on, 0 off */
	NDTK_REQ_MENU = 20,        /* text: the menu file (WP §4.13) */
	NDTK_REQ_MENU_ITEM = 21,   /* value item id, value2 1 enable, 0 disable */
	NDTK_REQ_CURSOR = 22,      /* text: cursor(6) image bytes; text_len; NULL restores default */
	NDTK_REQ_CURSOR_VISIBLE = 23,
	NDTK_REQ_WORKSPACE = 24,
	NDTK_REQ_PARENT = 25,      /* window */
	NDTK_REQ_LATENCY = 26,     /* value 0 mailbox, 1..3 */
	NDTK_REQ_BUFFER = 27,      /* value enum ndtk_buffer_mode; rect.width/height for FIXED */
	NDTK_REQ_VIEWPORT = 28,    /* rect in buffer pixels; value 1 nearest; rect all 0: off */
	NDTK_REQ_POPUP = 29,       /* window = parent; rect = anchor; value anchor, value2 gravity; flags NDTK_POPUP_* */
};

enum ndtk_hit : uint32_t {
	NDTK_HIT_CLIENT = 0, NDTK_HIT_DRAG = 1, NDTK_HIT_CLOSE = 2, NDTK_HIT_DEPTH = 3, NDTK_HIT_ZOOM = 4,
	NDTK_HIT_HIDE = 5, NDTK_HIT_MENU = 6, NDTK_HIT_EDGE = 7,
};
/* Edges and popup anchors/gravities share one encoding. */
enum ndtk_edge : uint32_t {
	NDTK_EDGE_NONE = 0, NDTK_EDGE_N = 1, NDTK_EDGE_S = 2, NDTK_EDGE_E = 3, NDTK_EDGE_W = 4,
	NDTK_EDGE_NE = 5, NDTK_EDGE_NW = 6, NDTK_EDGE_SE = 7, NDTK_EDGE_SW = 8, NDTK_EDGE_CENTER = 9,
};
#define NDTK_POPUP_FLIP   0x1u
#define NDTK_POPUP_SLIDE  0x2u
#define NDTK_POPUP_RESIZE 0x4u
#define NDTK_POPUP_GRAB   0x8u

typedef struct ndtk_request {
	uint32_t    size;             /*  0 sizeof(ndtk_request) */
	uint32_t    op;               /*  4 enum ndtk_request_op */
	uint32_t    value;            /*  8 */
	uint32_t    value2;           /* 12 */
	ndtk_rect   rect;             /* 16 logical points unless the op says buffer pixels */
	ndtk_window window;           /* 32 */
	const char *text;             /* 40 UTF-8 or bytes; copied */
	uint32_t    text_len;         /* 48 */
	uint32_t    flags;            /* 52 */
	uint64_t    reserved;         /* 56 zero */
} ndtk_request;                   /* 64 bytes */

enum ndtk_error ndtk_window_request(ndtk_window window, const ndtk_request *request, uint32_t *out_tag);
enum ndtk_error ndtk_window_set_title(ndtk_window window, const char *title);
enum ndtk_error ndtk_window_set_size(ndtk_window window, float width, float height);
enum ndtk_error ndtk_window_show(ndtk_window window, bool shown);
enum ndtk_error ndtk_window_ctl(ndtk_window window, const char *line, uint32_t *out_tag);  /* raw WP ctl line */

/* Outputs */
#define NDTK_OUTPUT_PRIMARY 0x1u
#define NDTK_OUTPUT_HWTIME  0x2u

typedef struct ndtk_output_info {
	uint32_t    size;             /*   0 */
	uint32_t    flags;            /*   4 NDTK_OUTPUT_* */
	ndtk_output output;           /*   8 */
	uint32_t    scale_num, scale_den;        /*  16 */
	uint64_t    refresh_ns;       /*  24 */
	ndtk_rect   rect;             /*  32 desktop points */
	uint32_t    pixel_width, pixel_height;   /*  48 */
	char        name[64];         /*  56 stable name (WP-OUT-002), UTF-8, NUL-padded */
	uint64_t    reserved[2];      /* 120 */
} ndtk_output_info;               /* 136 bytes */

uint32_t ndtk_outputs(ndtk_loop loop, ndtk_output_info *out, uint32_t cap);   /* returns the count present */

/* Clipboard (snarf) and drag */
bool     ndtk_clipboard_set(ndtk_loop loop, const char *utf8, size_t len);
size_t   ndtk_clipboard_get(ndtk_loop loop, char *buf, size_t cap);           /* returns the length needed */
bool     ndtk_drag_paths(ndtk_loop loop, const char *const *paths, uint32_t count);  /* count 0 cancels */

/* Dialogs: asynchronous objects answered by NDTK_EV_DIALOG */
enum ndtk_dialog_kind : uint32_t { NDTK_DIALOG_OPEN = 1, NDTK_DIALOG_SAVE = 2, NDTK_DIALOG_FOLDER = 3, NDTK_DIALOG_MESSAGE = 4 };

typedef struct ndtk_dialog_desc {
	uint32_t    size;             /*  0 */
	uint32_t    kind;             /*  4 enum ndtk_dialog_kind */
	ndtk_window parent;           /*  8 */
	const char *title;            /* 16 */
	const char *path;             /* 24 start folder or suggested name */
	const char *filter;           /* 32 e.g. "*.txt;*.md"; NULL for all */
	uint32_t    flags;            /* 40 NDTK_DIALOG_MULTIPLE */
	uint32_t    reserved0;        /* 44 */
	uint64_t    reserved;         /* 48 */
} ndtk_dialog_desc;               /* 56 bytes */
#define NDTK_DIALOG_MULTIPLE 0x1u

ndtk_dialog ndtk_dialog_open(ndtk_loop loop, const ndtk_dialog_desc *desc);
bool        ndtk_dialog_cancel(ndtk_dialog dialog);

/* ---------------------------------------------------------------------
 * Input [TK-INP-001]..[TK-INP-016]
 */
enum ndtk_text_purpose : uint32_t {
	NDTK_PURPOSE_NORMAL = 0, NDTK_PURPOSE_PASSWORD = 1, NDTK_PURPOSE_NUMBER = 2, NDTK_PURPOSE_PHONE = 3,
	NDTK_PURPOSE_EMAIL = 4, NDTK_PURPOSE_URL = 5, NDTK_PURPOSE_TERMINAL = 6,
};

typedef struct ndtk_text_input {
	uint32_t    size;             /*  0 */
	uint32_t    enabled;          /*  4 */
	uint32_t    purpose;          /*  8 enum ndtk_text_purpose */
	uint32_t    cursor;           /* 12 byte offset into surrounding */
	uint32_t    anchor;           /* 16 */
	uint32_t    surrounding_len;  /* 20 */
	const char *surrounding;      /* 24 UTF-8; copied */
	ndtk_rect   caret;            /* 32 content coordinates, points */
} ndtk_text_input;                /* 48 bytes */

enum ndtk_error ndtk_text_input_set(ndtk_window window, const ndtk_text_input *state);
enum ndtk_error ndtk_text_input_reset(ndtk_window window);

typedef struct ndtk_gamepad_state {
	uint32_t size;                /*  0 */
	uint32_t buttons;             /*  4 NDTK_PAD_* */
	float    axes[8];             /*  8 enum ndtk_pad_axis order; 6 used */
	uint64_t time_ns;             /* 40 last event folded in */
	uint32_t seq;                 /* 48 */
	uint32_t connected;           /* 52 */
} ndtk_gamepad_state;             /* 56 bytes */

bool     ndtk_gamepad_state_get(ndtk_gamepad pad, ndtk_gamepad_state *out);      /* [T2] */
bool     ndtk_gamepad_rumble(ndtk_gamepad pad, uint16_t low, uint16_t high, uint32_t ms);
bool     ndtk_gamepad_led(ndtk_gamepad pad, uint8_t r, uint8_t g, uint8_t b);
bool     ndtk_gamepad_sensors(ndtk_gamepad pad, bool on);
bool     ndtk_gamepad_watch(ndtk_loop loop, bool on);   /* which loop gets pad events */
uint32_t ndtk_keymap_layout(ndtk_loop loop, char *name, size_t cap);   /* active layout index */

/* ---------------------------------------------------------------------
 * Frames and surfaces [TK-FRM-001]..[TK-FRM-013]
 */
typedef struct ndtk_cpu_surface {
	void        *pixels;          /*  0 row-major, `chan` format; premultiplied if the chan has alpha */
	uint64_t     byte_count;      /*  8 */
	uint32_t     stride;          /* 16 bytes per row */
	uint32_t     width, height;   /* 20 buffer pixels */
	uint32_t     chan;            /* 28 Deskchan */
	uint32_t     config_seq;      /* 32 the configuration this buffer belongs to */
	uint32_t     age;             /* 36 0 undefined; n: holds the frame presented n presents ago */
	ndtk_surface surface;         /* 40 */
} ndtk_cpu_surface;               /* 48 bytes */

bool ndtk_request_frame(ndtk_window window);   /* [T2] marks the window; the loop writes wantframe before its next wait */
bool ndtk_cpu_surface_acquire(ndtk_window window, ndtk_cpu_surface *out);      /* [T2] */
void ndtk_cpu_surface_fill(ndtk_cpu_surface *surface, uint32_t pixel, const ndtk_irect *rect);   /* [T2] */
bool ndtk_present(ndtk_window window, ndtk_cpu_surface *surface, const ndtk_irect *damage,
                  uint32_t config_seq, uint64_t at_ns);                         /* [T2] */
bool ndtk_cpu_surface_release(ndtk_cpu_surface *surface);                      /* dropped: nothing shown */

/* ---------------------------------------------------------------------
 * Drawing [TK-DRAW-001]..[TK-DRAW-008]. A canvas lives in the frame arena
 * and is valid between begin and end on one thread.
 */
typedef struct ndtk_canvas ndtk_canvas;

typedef struct ndtk_text_metrics {
	float    width, height;       /*  0 points */
	float    ascent, descent;     /*  8 first line */
	uint32_t lines;               /* 16 */
	uint32_t clusters;            /* 20 grapheme clusters */
} ndtk_text_metrics;              /* 24 bytes */

ndtk_canvas *ndtk_canvas_begin(ndtk_cpu_surface *surface, uint32_t scale_num, uint32_t scale_den);   /* [T2] */
bool ndtk_canvas_end(ndtk_canvas *canvas, ndtk_irect *damage_out);                                   /* [T2] */
void ndtk_canvas_clip_push(ndtk_canvas *canvas, ndtk_rect rect);                                     /* [T2] */
void ndtk_canvas_clip_pop(ndtk_canvas *canvas);                                                      /* [T2] */
void ndtk_canvas_fill(ndtk_canvas *canvas, ndtk_rect rect, float radius, uint32_t argb);             /* [T2] */
void ndtk_canvas_stroke(ndtk_canvas *canvas, ndtk_rect rect, float radius, float width, uint32_t argb); /* [T2] */
void ndtk_canvas_polyline(ndtk_canvas *canvas, const float *xy, uint32_t points, float width, uint32_t argb); /* [T2] */
void ndtk_canvas_material(ndtk_canvas *canvas, ndtk_rect rect, const char *cls, uint32_t part);      /* [T2] theme */
void ndtk_canvas_text(ndtk_canvas *canvas, float x, float y, const char *cls,
                      const char *utf8, size_t len, float max_width);                                /* [T2] shaped */
void ndtk_canvas_image(ndtk_canvas *canvas, ndtk_rect dst, const void *pixels, uint32_t stride,
                       uint32_t width, uint32_t height, uint32_t chan);                              /* [T2] */
bool ndtk_text_measure(ndtk_window window, const char *cls, const char *utf8, size_t len,
                       float max_width, ndtk_text_metrics *out);                                     /* [T2] shaped */

/* ---------------------------------------------------------------------
 * GPU [TK-GPU-001]..[TK-GPU-010]. WebGPU objects are webgpu.h's (Dawn).
 */
struct WGPUInstanceImpl;
struct WGPUAdapterImpl;
struct WGPUDeviceImpl;
struct WGPUQueueImpl;
struct WGPUSurfaceImpl;
struct WGPUTextureImpl;
struct WGPUTextureViewImpl;

enum ndtk_handle_bag_kind : uint32_t { NDTK_BAG_WSYS = 1 };

typedef struct ndtk_gpu_handle_bag {  /* raw-window-handle for NeoDarwin (R7) */
	uint32_t    size;             /*  0 */
	uint32_t    kind;             /*  4 NDTK_BAG_WSYS */
	uint32_t    window_id;        /*  8 wsys window id */
	uint32_t    surface_port;     /* 12 Mach port name of windows/N/surface, re-read per buffer change */
	ndtk_window window;           /* 16 */
	const char *desktop;          /* 24 mount of the window system, "/n/desktop" */
	uint32_t    config_seq;       /* 32 configuration at the time of the call */
	uint32_t    reserved0;        /* 36 */
	uint64_t    reserved[2];      /* 40 */
} ndtk_gpu_handle_bag;            /* 56 bytes */

#define NDTK_GPU_LOW_POWER 0x1u
#define NDTK_GPU_STORAGE   0x2u   /* the surface texture also has STORAGE_BINDING usage (compute to display) */
#define NDTK_GPU_SRGB      0x4u

typedef struct ndtk_gpu_desc {
	uint32_t        size;             /*  0 */
	uint32_t        flags;            /*  4 NDTK_GPU_* */
	uint32_t        latency;          /*  8 0 mailbox, 1..3 (WP latency) */
	uint32_t        buffer_width;     /* 12 0: the window's device pixels; else a fixed buffer */
	uint32_t        buffer_height;    /* 16 */
	uint32_t        feature_count;    /* 20 */
	const uint32_t *features;         /* 24 WGPUFeatureName values required */
	uint64_t        reserved[2];      /* 32 */
} ndtk_gpu_desc;                      /* 48 bytes */

typedef struct ndtk_gpu_context {
	uint32_t                 size;        /*  0 */
	uint32_t                 format;      /*  4 WGPUTextureFormat of the surface */
	struct WGPUInstanceImpl *instance;    /*  8 */
	struct WGPUAdapterImpl  *adapter;     /* 16 */
	struct WGPUDeviceImpl   *device;      /* 24 */
	struct WGPUQueueImpl    *queue;       /* 32 */
	struct WGPUSurfaceImpl  *surface;     /* 40 configured for the window */
	ndtk_gpu                 gpu;         /* 48 */
	uint64_t                 usage;       /* 56 WGPUTextureUsage of the surface textures */
	uint32_t                 width, height; /* 64 configured size */
	uint32_t                 config_seq;  /* 72 */
	uint32_t                 reserved0;   /* 76 */
	uint64_t                 reserved[2]; /* 80 */
} ndtk_gpu_context;                       /* 96 bytes */

typedef struct ndtk_gpu_frame {
	struct WGPUTextureImpl     *texture;  /*  0 owned by the surface until end_frame */
	struct WGPUTextureViewImpl *view;     /*  8 */
	uint32_t                    width, height; /* 16 */
	uint32_t                    config_seq;    /* 24 the frame is drawn for this configuration */
	uint32_t                    reconfigured;  /* 28 1 if begin_frame reconfigured the surface */
} ndtk_gpu_frame;                         /* 32 bytes */

typedef struct ndtk_gpu_budget {
	uint32_t size;                /*  0 */
	uint32_t pressure;            /*  4 0 none, 1 warn, 2 critical */
	uint64_t budget_bytes;        /*  8 */
	uint64_t used_bytes;          /* 16 */
	uint64_t reserved[2];         /* 24 */
} ndtk_gpu_budget;                /* 40 bytes */

bool ndtk_gpu_handle_bag_get(ndtk_window window, ndtk_gpu_handle_bag *out);
bool ndtk_gpu_open(ndtk_window window, const ndtk_gpu_desc *desc, ndtk_gpu_context *out);  /* desc may be NULL */
bool ndtk_gpu_begin_frame(ndtk_gpu gpu, ndtk_gpu_frame *out);                                /* [T2] */
bool ndtk_gpu_end_frame(ndtk_gpu gpu, const ndtk_gpu_frame *frame, const ndtk_irect *damage); /* [T2] */
bool ndtk_gpu_presented(ndtk_window window, uint32_t config_seq);   /* raw path: tag the next GPU present */
void ndtk_gpu_close(ndtk_gpu gpu);
bool ndtk_gpu_budget_get(ndtk_loop loop, ndtk_gpu_budget *out);    /* NDTK_E_UNSUPPORTED until P7 */

/* ---------------------------------------------------------------------
 * UI [TK-UI-001]..[TK-UI-014], [TK-LAYOUT-001]..[TK-LAYOUT-004], [TK-STYLE-001]..[TK-STYLE-003], [TK-TEXT-001]..[TK-TEXT-007]
 */
enum ndtk_node_kind : uint32_t {
	NDTK_NODE_COLUMN = 1, NDTK_NODE_ROW = 2, NDTK_NODE_STACK = 3, NDTK_NODE_SPACER = 4,
	NDTK_NODE_LABEL = 5, NDTK_NODE_BUTTON = 6, NDTK_NODE_CHECKBOX = 7, NDTK_NODE_SLIDER = 8,
	NDTK_NODE_METER = 9, NDTK_NODE_SCOPE = 10, NDTK_NODE_TEXT_FIELD = 11, NDTK_NODE_TEXT_VIEW = 12,
	NDTK_NODE_IMAGE = 13, NDTK_NODE_CANVAS = 14, NDTK_NODE_SCROLL = 15,
};

enum ndtk_ui_result : uint32_t {
	NDTK_UI_IGNORED = 0, NDTK_UI_CONSUMED = 1, NDTK_UI_CLICKED = 2, NDTK_UI_CHANGED = 3,
	NDTK_UI_EDITED = 4, NDTK_UI_FOCUSED = 5,
};

typedef struct ndtk_ui_event {
	uint32_t  result;             /*  0 enum ndtk_ui_result */
	uint32_t  reserved;           /*  4 */
	ndtk_node node;               /*  8 */
	double    value;              /* 16 slider, checkbox */
} ndtk_ui_event;                  /* 24 bytes */

typedef struct ndtk_ui_stats {
	uint32_t size;                /*  0 */
	uint32_t nodes;               /*  4 live */
	uint64_t presents;            /*  8 */
	uint64_t skipped_draws;       /* 16 draw() with nothing changed */
	uint64_t full_repaints;       /* 24 */
	uint64_t invalid_handles;     /* 32 */
	uint64_t arena_capacity;      /* 40 bytes */
	uint64_t arena_high_water;    /* 48 bytes */
	uint64_t arena_spills;        /* 56 frames that spilled to the heap */
	uint64_t reserved[2];         /* 64 */
} ndtk_ui_stats;                  /* 80 bytes */

typedef void (*ndtk_canvas_fn)(void *ctx, ndtk_canvas *canvas, ndtk_rect frame);   /* [T2] */

ndtk_ui   ndtk_ui_create(ndtk_window window, const char *theme);         /* theme NULL: /n/theme */
void      ndtk_ui_destroy(ndtk_ui ui);
ndtk_node ndtk_node_create(ndtk_ui ui, uint32_t kind, const char *cls, float flex);
bool      ndtk_node_add(ndtk_ui ui, ndtk_node parent, ndtk_node child, uint32_t index);  /* UINT32_MAX appends */
bool      ndtk_node_destroy(ndtk_ui ui, ndtk_node node);                  /* the subtree */
bool      ndtk_ui_set_root(ndtk_ui ui, ndtk_node node);
bool      ndtk_node_set_text(ndtk_ui ui, ndtk_node node, const char *utf8, size_t len);   /* copied */
bool      ndtk_node_set_value(ndtk_ui ui, ndtk_node node, double value);
bool      ndtk_node_set_range(ndtk_ui ui, ndtk_node node, double lo, double hi);
bool      ndtk_node_set_flex(ndtk_ui ui, ndtk_node node, float flex);
bool      ndtk_node_set_class(ndtk_ui ui, ndtk_node node, const char *cls);
bool      ndtk_node_set_hidden(ndtk_ui ui, ndtk_node node, bool hidden);
bool      ndtk_node_set_samples(ndtk_ui ui, ndtk_node node, const float *samples, size_t count); /* [T2] */
bool      ndtk_node_set_image(ndtk_ui ui, ndtk_node node, const void *pixels, uint32_t stride,
                              uint32_t width, uint32_t height, uint32_t chan);  /* copied */
bool      ndtk_node_set_canvas(ndtk_ui ui, ndtk_node node, ndtk_canvas_fn fn, void *ctx);
bool      ndtk_node_set_label(ndtk_ui ui, ndtk_node node, const char *utf8, size_t len);  /* accessibility name */
double    ndtk_node_value(ndtk_ui ui, ndtk_node node);
size_t    ndtk_node_text(ndtk_ui ui, ndtk_node node, char *buf, size_t cap);   /* returns the length needed */
bool      ndtk_node_frame(ndtk_ui ui, ndtk_node node, ndtk_rect *out);
bool      ndtk_ui_focus(ndtk_ui ui, ndtk_node node);
bool      ndtk_ui_scroll(ndtk_ui ui, ndtk_node node, float dy);
bool      ndtk_ui_handle(ndtk_ui ui, const ndtk_event *event, ndtk_ui_event *out);   /* [T2] */
bool      ndtk_ui_draw(ndtk_ui ui);                                                   /* [T2] */
bool      ndtk_ui_stats_get(ndtk_ui ui, ndtk_ui_stats *out);
size_t    ndtk_ui_export(ndtk_ui ui, char *buf, size_t cap);                          /* returns the length needed */

/* ---------------------------------------------------------------------
 * Audio [TK-AUD-001]..[TK-AUD-013]. Streams, buffers and voices are
 * AUhandles; records are nd_audio.h's.
 */
struct AUcontract;
struct AUrender_info;
struct AUstream_params;

/* Identical in type to AUrender_fn (nd_audio.h): the C trampoline of AU-T2-004. */
typedef void (*ndtk_render_fn)(void *ctx, const struct AUrender_info *info, void *buf);

#define NDTK_VOICE_LOOP 0x1u      /* = AU_VF_LOOP */

ndtk_au_handle ndtk_audio_open(ndtk_loop loop, uint32_t rate, uint32_t channels, uint32_t period_frames,
                               ndtk_render_fn render, void *ctx);          /* F32 output, callback, default device */
ndtk_au_handle ndtk_audio_open_params(ndtk_loop loop, const struct AUstream_params *params);
bool           ndtk_audio_start(ndtk_au_handle stream);
bool           ndtk_audio_stop(ndtk_au_handle stream);
void           ndtk_audio_close(ndtk_au_handle stream);
bool           ndtk_audio_contract(ndtk_au_handle stream, struct AUcontract *out);            /* [RT] */
bool           ndtk_audio_join(ndtk_au_handle stream);           /* calling thread joins the stream's deadline */
ndtk_au_handle ndtk_audio_buffer(ndtk_loop loop, const float *samples, uint32_t frames, uint32_t rate,
                                 uint32_t channels);
ndtk_au_handle ndtk_audio_play(ndtk_loop loop, ndtk_au_handle buffer, float gain, uint32_t flags);   /* [RT] */
bool           ndtk_audio_voice_stop(ndtk_au_handle voice);                                          /* [RT] */
ndtk_au_handle ndtk_audio_session(ndtk_loop loop);   /* the toolkit's audio session, for direct au_* calls */

/* ---------------------------------------------------------------------
 * Files and I/O [TK-IO-001]..[TK-IO-006]
 */
enum ndtk_known_path : uint32_t {
	NDTK_PATH_HOME = 1, NDTK_PATH_DOCUMENTS = 2, NDTK_PATH_CONFIG = 3, NDTK_PATH_DATA = 4,
	NDTK_PATH_CACHE = 5, NDTK_PATH_TEMP = 6, NDTK_PATH_RESOURCES = 7,
};

ndtk_io     ndtk_io_read(ndtk_loop loop, int32_t fd, uint64_t offset, void *buf, uint64_t len, uint64_t udata);
ndtk_io     ndtk_io_write(ndtk_loop loop, int32_t fd, uint64_t offset, const void *buf, uint64_t len, uint64_t udata);
bool        ndtk_io_cancel(ndtk_io request);
size_t      ndtk_known_path_get(uint32_t which, char *buf, size_t cap);   /* returns the length needed */
ndtk_source ndtk_watch_path(ndtk_loop loop, const char *path, uint32_t what);   /* NDTK_PATH_WRITE ... */

/* ---------------------------------------------------------------------
 * Agent export and accessibility [TK-AGENT-001]..[TK-AGENT-011]
 */
enum ndtk_agent_verb : uint32_t {
	NDTK_AGENT_APP = 0,        /* an app verb: data holds the command line */
	NDTK_AGENT_PRESS = 1, NDTK_AGENT_SET_VALUE = 2, NDTK_AGENT_SET_TEXT = 3, NDTK_AGENT_FOCUS = 4,
	NDTK_AGENT_SCROLL = 5,
};

bool ndtk_agent_export(ndtk_loop loop, const char *appid, const char *version);
bool ndtk_agent_verb_add(ndtk_loop loop, const char *name, const char *params, const char *description);
bool ndtk_agent_state_set(ndtk_loop loop, const char *json, size_t len);   /* copied; seq advances */
bool ndtk_agent_reply(ndtk_loop loop, uint32_t request, const char *json, size_t len);
bool ndtk_agent_log(ndtk_loop loop, const char *json, size_t len);

#ifdef __cplusplus
}
#endif

/* ---------------------------------------------------------------------
 * Layout assertions (spec-conventions.md §5). Every field offset is
 * asserted in ndtk_layout.c; the record sizes are asserted here too so
 * that every includer checks them.
 */
static_assert(sizeof(ndtk_event) == 96, "ndtk_event is 96 bytes");
static_assert(offsetof(ndtk_event, u) == 40, "ndtk_event payload at 40");
static_assert(sizeof(ndtk_key_event) == 32, "ndtk_key_event is 32 bytes");
static_assert(sizeof(ndtk_text_event) == 32, "ndtk_text_event is 32 bytes");
static_assert(sizeof(ndtk_pointer_event) == 56, "ndtk_pointer_event is 56 bytes");
static_assert(sizeof(ndtk_configure_event) == 56, "ndtk_configure_event is 56 bytes");
static_assert(sizeof(ndtk_frame_event) == 56, "ndtk_frame_event is 56 bytes");
static_assert(sizeof(ndtk_gamepad_event) == 40, "ndtk_gamepad_event is 40 bytes");
static_assert(sizeof(ndtk_output_event) == 48, "ndtk_output_event is 48 bytes");
static_assert(sizeof(ndtk_audio_event) == 32, "ndtk_audio_event holds one AUevent");
static_assert(sizeof(ndtk_loop_desc) == 40, "ndtk_loop_desc is 40 bytes");
static_assert(sizeof(ndtk_app) == 48, "ndtk_app is 48 bytes");
static_assert(sizeof(ndtk_window_desc) == 64, "ndtk_window_desc is 64 bytes");
static_assert(sizeof(ndtk_request) == 64, "ndtk_request is 64 bytes");
static_assert(sizeof(ndtk_output_info) == 136, "ndtk_output_info is 136 bytes");
static_assert(sizeof(ndtk_dialog_desc) == 56, "ndtk_dialog_desc is 56 bytes");
static_assert(sizeof(ndtk_text_input) == 48, "ndtk_text_input is 48 bytes");
static_assert(sizeof(ndtk_gamepad_state) == 56, "ndtk_gamepad_state is 56 bytes");
static_assert(sizeof(ndtk_cpu_surface) == 48, "ndtk_cpu_surface is 48 bytes");
static_assert(sizeof(ndtk_gpu_handle_bag) == 56, "ndtk_gpu_handle_bag is 56 bytes");
static_assert(sizeof(ndtk_gpu_desc) == 48, "ndtk_gpu_desc is 48 bytes");
static_assert(sizeof(ndtk_gpu_context) == 96, "ndtk_gpu_context is 96 bytes");
static_assert(sizeof(ndtk_gpu_frame) == 32, "ndtk_gpu_frame is 32 bytes");
static_assert(sizeof(ndtk_gpu_budget) == 40, "ndtk_gpu_budget is 40 bytes");
static_assert(sizeof(ndtk_ui_event) == 24, "ndtk_ui_event is 24 bytes");
static_assert(sizeof(ndtk_ui_stats) == 80, "ndtk_ui_stats is 80 bytes");
static_assert(sizeof(ndtk_allocator) == 32, "ndtk_allocator is 32 bytes");

#endif /* NDTK_H */
