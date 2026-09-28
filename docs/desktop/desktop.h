// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: the window protocol's wire records and ctl spellings are the C ABI shared by C, Swift and out-of-tree bindings (language-policy.md §3)
/* Provenance: distilled from the plan-neo pilot (commit b0a35a1). Reference copy; the build consumes the mirror. Revised to version 2 by NeoDarwin epic P4-17. */
/*
 * desktop.h - NeoDarwin desktop windowing protocol over 9P, version 2.
 *
 * Companion to docs/desktop/window-protocol.md, which is the normative
 * text: where the two differ, the text wins and this header is a defect.
 * This header defines the binary records that cross the 9P boundary
 * (window descriptors, configurations, events and their extended form),
 * the bitmasks used in them, and the spelling of every control message.
 * It is freestanding C23 and also compiles as C++20: only <stdint.h>
 * and <stddef.h> are required, no libc (bool, static_assert and alignof
 * are keywords in both).
 *
 * Wire encoding: every multi-byte field is little-endian regardless of
 * host. On the two supported targets (x86_64, aarch64) the structs
 * below have the same layout as the wire format, so a client may read
 * straight into them; the pack/unpack helpers are the portable path.
 *
 * Version 2 is additive: every version 1 record, value and spelling
 * keeps its meaning. What version 2 adds is marked "v2".
 */
#ifndef NEODARWIN_DESKTOP_H
#define NEODARWIN_DESKTOP_H

#include <stdint.h>
#include <stddef.h>

/*
 * The header is also C++ (C++20 and later): SDL backends, Qt, Dawn and
 * engines include it. C's minimum-length array parameters ("static N"
 * inside the brackets) are spelled through DESK_STATIC, since C++ has
 * no such form.
 */
#ifdef __cplusplus
#define DESK_STATIC(n)    n
extern "C" {
#else
#define DESK_STATIC(n)    static n
#endif

#define DESK_API_VERSION  2
#define DESK_VERSION      DESK_API_VERSION  /* the version 1 name, kept */
#define WP_API_VERSION    DESK_API_VERSION  /* the spec-conventions.md §5 name (<PREFIX>_API_VERSION) */
#define DESK_MOUNT        "/n/desktop"      /* default mount point */
#define DESK_SRVFMT       "wsys.%s.%d"        /* nsd registry name: user, pid */

#define DESK_EVENTSZ      32                  /* bytes per Deskevent on the wire */
#define DESK_DESCSZ       96                  /* bytes per Deskwin on the wire */
#define DESK_TITLEMAX     256                 /* bytes, UTF-8, including NUL */
#define DESK_MAXQUEUE     1024                /* events queued per window before lag */

/* v2 sizes and limits */
#define DESK_EXTHEADSZ    32                  /* bytes of the extended-record head */
#define DESK_EXTMAX       4096                /* largest extended record, head included */
#define DESK_EXTALIGN     8                   /* extended records are padded to this */
#define DESK_CONFIGSZ     80                  /* bytes per Deskconfig (config file, DE_CONFIGURE body) */
#define DESK_NTYPES       256                 /* event type numbers the mask can name */
#define DESK_MASKWORDS    (DESK_NTYPES / 32)
#define DESK_HARDQUEUE    65536               /* records queued before the stream is resynchronised */
#define DESK_MAXLATENCY   3                   /* largest `latency N` */
#define DESK_SCALEDEN     120                 /* Deskconfig.scaleden in version 2 */
#define DESK_THROTTLE_MIN_HZ 1                /* a throttled clock ticks at least this often */
#define DESK_THROTTLE_MAX_HZ 10               /* and at most this often */

/* ---------------------------------------------------------------------
 * Geometry. Screen coordinates have their origin at the top left of the
 * desktop; window coordinates have their origin at the top left of the
 * window's content area (inside the frame). Rectangles are half-open:
 * [x0,x1) x [y0,y1), as in libdraw. v2: both are in logical units
 * (points); at scale 1 a point is a pixel, which is all version 1 knew.
 */
typedef struct Deskpoint { int32_t x, y; } Deskpoint;
typedef struct Deskrect  { int32_t x0, y0, x1, y1; } Deskrect;

static inline int32_t desk_dx(Deskrect r) { return r.x1 - r.x0; }
static inline int32_t desk_dy(Deskrect r) { return r.y1 - r.y0; }
static inline bool desk_ptinrect(Deskpoint p, Deskrect r)
{
	return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1;
}

/* v2: 24.8 fixed point, used for sub-point pointer positions and deltas. */
#define DESK_FIX_ONE      256
static inline int32_t desk_fixint(int32_t f) { return f >> 8; }

/* ---------------------------------------------------------------------
 * Pixel formats: the channel descriptor encoding of draw(3)/image(6),
 * so a value here is also a valid argument to libdraw's allocimage and
 * a valid string via chantostr. Listed as bytes from most significant.
 */
enum Deskchan : uint32_t {
	DESK_XRGB32 = 0x68081828,   /* x8r8g8b8, the default; 4 bytes/pixel */
	DESK_RGBA32 = 0x08182848,   /* r8g8b8a8, for windows with DF_ALPHA */
	DESK_XBGR32 = 0x68281808,   /* x8b8g8r8 */
	DESK_RGB24  = 0x00081828,   /* r8g8b8, 3 bytes/pixel */
	DESK_GREY8  = 0x00000038,   /* k8 */
};

static inline uint32_t desk_chandepth(uint32_t chan)
{
	uint32_t d = 0;
	for(; chan != 0; chan >>= 8)
		d += chan & 15;
	return d;                                   /* bits per pixel */
}

/* ---------------------------------------------------------------------
 * Window flags (Deskwin.flags). The first group names the Intuition
 * gadgets the frame carries; a window with none of them and
 * DF_BORDERLESS has no frame at all.
 */
enum Deskflags : uint32_t {
	DF_TITLEBAR   = 1u << 0,    /* drag bar with title text */
	DF_CLOSE      = 1u << 1,    /* close gadget, top left */
	DF_DEPTH      = 1u << 2,    /* depth gadget, top right: front/back toggle */
	DF_ZOOM       = 1u << 3,    /* zoom gadget: toggle between two sizes */
	DF_SIZE       = 1u << 4,    /* sizing gadget, bottom right */
	DF_DRAG       = 1u << 5,    /* window may be moved by its title bar */
	DF_BORDERLESS = 1u << 6,    /* no frame; content rect == frame rect */
	DF_BACKDROP   = 1u << 7,    /* always at the back, behind all others */
	DF_ONTOP      = 1u << 8,    /* always above ordinary windows */
	DF_TRANSIENT  = 1u << 9,    /* dialog owned by Deskwin.parent; raised with it */
	DF_NOFOCUS    = 1u << 10,   /* never receives keyboard focus */
	DF_RETAIN     = 1u << 11,   /* server keeps a full copy of the content (default on) */
	DF_ALPHA      = 1u << 12,   /* content has an alpha channel; composited over */
	DF_NOMENU     = 1u << 13,   /* window contributes nothing to the menu bar */
	DF_FIXEDSIZE  = 1u << 14,   /* user may not resize; server may still, on ctl */
	DF_POPUP      = 1u << 15,   /* v2: kind popup; placed by the popup message */
	DF_TOOLTIP    = 1u << 16,   /* v2: kind tooltip; a popup that never takes input */
	DF_NODECOR    = 1u << 17,   /* v2: `decor none`: no drawn frame, behaviour through hit regions */

	DF_DEFAULT    = DF_TITLEBAR|DF_CLOSE|DF_DEPTH|DF_ZOOM|DF_SIZE|DF_DRAG|DF_RETAIN,
	DF_DIALOG     = DF_TITLEBAR|DF_CLOSE|DF_DRAG|DF_TRANSIENT|DF_RETAIN,
};

/* Window state (Deskwin.state). Changed by the user, the server or ctl. */
enum Deskstate : uint32_t {
	DS_VISIBLE     = 1u << 0,   /* mapped on its workspace */
	DS_FOCUSED     = 1u << 1,   /* receives keyboard input, owns the menu bar */
	DS_HIDDEN      = 1u << 2,   /* iconified to the desktop (Workbench icon) */
	DS_ZOOMED      = 1u << 3,   /* at its alternate (zoom) geometry */
	DS_GRABBED     = 1u << 4,   /* pointer grabbed by this window */
	DS_LAGGING     = 1u << 5,   /* event queue overflowed; motion is being dropped */
	DS_SNAPPED     = 1u << 6,   /* placed by a snap command; cleared by move/resize */
	DS_FULL        = 1u << 7,   /* covers the whole workspace, frame hidden */
	DS_CLOSING     = 1u << 8,   /* DE_CLOSE sent, waiting for the client */
	DS_INTERACTIVE = 1u << 9,   /* v2: a user move or resize gesture is in progress */
	DS_OCCLUDED    = 1u << 10,  /* v2: mapped, but no pixel of it is on any output */
};

/* Buffer modes (see "image" and the present message). */
enum Deskmode : uint32_t {
	DESK_MODECOPY = 0,          /* present copies the dirty rect into the front buffer */
	DESK_MODEFLIP = 1,          /* present swaps buffers; client must repaint what it presents */
};

/* v2: Deskconfig.visibility, computed by the server, which knows the stack. */
enum Deskvis : uint32_t {
	DESK_VIS_UNMAPPED  = 0,     /* not placed yet (popup), or dismissed */
	DESK_VIS_SHOWN     = 1,     /* at least one pixel is on an output */
	DESK_VIS_OCCLUDED  = 2,     /* on the shown workspace, but fully covered */
	DESK_VIS_OFFSCREEN = 3,     /* on a workspace not shown, or its outputs are off or locked */
	DESK_VIS_HIDDEN    = 4,     /* iconified */
};

/* v2: Deskconfig.bufmode, set by the buffer message. */
enum Deskbufmode : uint32_t {
	DESK_BUF_LOGICAL = 0,       /* one buffer pixel per point; the compositor scales (version 1) */
	DESK_BUF_DEVICE  = 1,       /* one buffer pixel per device pixel: logical size x scale */
	DESK_BUF_FIXED   = 2,       /* a client-chosen size; the compositor scales it to the content */
};

/* ---------------------------------------------------------------------
 * Window descriptor: the binary contents of windows/N/desc, and the
 * record delivered in DE_WINNEW. 96 bytes. All rects in screen coords.
 * Unchanged in version 2; what version 2 adds is in Deskconfig.
 */
typedef struct Deskwin {
	uint32_t id;                /* window id, > 0; 0 means none */
	uint32_t parent;            /* owner for DF_TRANSIENT, else 0 */
	uint32_t workspace;         /* workspace (Intuition screen) index */
	uint32_t depth;             /* stacking position, 0 = frontmost */

	Deskrect r;                 /* content rectangle */
	Deskrect frame;             /* frame rectangle including decorations */

	uint32_t mindx, mindy;      /* size limits for user resizing */
	uint32_t maxdx, maxdy;      /* 0 means unlimited */

	uint32_t flags;             /* Deskflags */
	uint32_t state;             /* Deskstate */
	uint32_t chan;              /* Deskchan of the image file */
	uint32_t stride;            /* bytes per row of the image file */

	uint32_t seq;               /* incremented on every change to this record */
	uint32_t pid;               /* lead process, 0 if unknown */
	uint32_t mode;              /* Deskmode */
	uint32_t reserved;
} Deskwin;

/* ---------------------------------------------------------------------
 * v2: window configuration: the binary contents of windows/N/config and
 * the body of DE_CONFIGURE. Everything a client needs to render one
 * frame at the right size and density. 80 bytes.
 */
typedef struct Deskconfig {
	uint32_t seq;               /* configuration sequence: 1 at creation, +1 per change */
	uint32_t state;             /* Deskstate at this configuration */
	uint32_t dx, dy;            /* logical content size, points */
	uint32_t pdx, pdy;          /* device pixels the content covers */
	uint32_t scalenum;          /* scale = scalenum / scaleden */
	uint32_t scaleden;          /* DESK_SCALEDEN in version 2 */
	uint32_t bdx, bdy;          /* buffer size, pixels (image and surface) */
	uint32_t stride;            /* buffer bytes per row */
	uint32_t chan;              /* buffer Deskchan */
	uint32_t visibility;        /* Deskvis */
	uint32_t bufmode;           /* Deskbufmode */
	uint32_t refresh;           /* interval of this window's frame clock, ns (throttled if so) */
	uint32_t output;            /* id of the output the scale and clock come from; 0 if none */
	uint32_t latency;           /* current `latency`: 0 = mailbox, else N */
	uint32_t reserved[3];
} Deskconfig;

/* ---------------------------------------------------------------------
 * Events: the binary records read from windows/N/events and from the
 * desktop-level events file. In the plain format (the default, version
 * 1) every record is a 32-byte Deskevent. In the extended format (v2,
 * `format ext` written to the open events fid) every record is a
 * Deskexthead followed by a typed body and optional variable data.
 * A read returns whole records only.
 *
 * Types 1..18 are version 1. Types 19..31 are never assigned, so a
 * version 1 mask with every bit set selects exactly the version 1 types.
 * Types 32 and up are version 2 and are delivered only in the extended
 * format.
 */
enum Desketype : uint32_t {
	DE_NONE      = 0,
	DE_MOUSE     = 1,   /* a=x b=y (window coords) c=buttons d=modifiers; ext body Deskpointer */
	DE_KEY       = 2,   /* a=rune b=keycode (USB HID usage) c=modifiers after d=Deskkeyflags; ext body Deskkey */
	DE_RESIZE    = 3,   /* a=buffer dx b=buffer dy c=stride d=chan; image reallocated */
	DE_MOVE      = 4,   /* a=x0 b=y0 (screen coords of content) */
	DE_EXPOSE    = 5,   /* a..d = rect (window coords) to repaint; only without DF_RETAIN */
	DE_FRAME     = 6,   /* a=frame counter b=microseconds since previous frame; ext body Deskframe */
	DE_FOCUS     = 7,   /* a=1 gained, 0 lost */
	DE_ENTER     = 8,   /* a=x b=y pointer entered the content area */
	DE_LEAVE     = 9,   /* a=x b=y pointer left the content area */
	DE_CLOSE     = 10,  /* close gadget or menu; a=1 if forced (user chose Kill) */
	DE_MENU      = 11,  /* a=menu id b=item id c=1 if now checked, 0 otherwise */
	DE_STATE     = 12,  /* a=new state b=old state c=depth */
	DE_DROP      = 13,  /* a=x b=y c=count of paths, read windows/N/drop for them; ext data: the paths */
	DE_WINNEW    = 14,  /* desktop-level: window created; win=id */
	DE_WINGONE   = 15,  /* desktop-level: window destroyed; win=id */
	DE_WORKSPACE = 16,  /* desktop-level: a=new index b=old index */
	DE_THEME     = 17,  /* desktop-level: theme file changed */
	DE_QUIT      = 18,  /* server is exiting; all files will return errors */
	DE_MAX       = 19,  /* end of the version 1 types (the name is version 1's) */

	/* v2: extended format only; body type in the comment */
	DE_CONFIGURE     = 32,  /* Deskconfig: size, scale, buffer, visibility changed */
	DE_ACK           = 33,  /* Deskack: a tagged ctl request completed; data: error text */
	DE_TEXT          = 34,  /* Desktext; data: committed UTF-8 text */
	DE_PREEDIT       = 35,  /* Deskpreedit; data: UTF-8 text, then Deskpestyle runs */
	DE_DELSURROUND   = 36,  /* Deskdelsurround: delete text around the cursor */
	DE_KEYMAP        = 37,  /* Deskkeymap; data: layout name. Window and desktop level */
	DE_SCROLL        = 38,  /* Deskscroll: precise and discrete scrolling */
	DE_MOTION        = 39,  /* Deskmotion: relative pointer motion */
	DE_POINTER       = 40,  /* Deskpointerstate: lock or confinement changed */
	DE_PROXIMITY     = 41,  /* Deskproximity: a pen tool entered or left proximity */
	DE_POPUPDONE     = 42,  /* Deskpopupdone: the popup was dismissed by the server */
	DE_OUTPUT        = 43,  /* desktop-level, Deskoutput; data: output name */
	DE_GAMEPAD       = 44,  /* desktop-level, Deskgamepad; data: pad name */
	DE_PADBUTTON     = 45,  /* gamepads/N/events, Deskpadbutton */
	DE_PADAXIS       = 46,  /* gamepads/N/events, Deskpadaxis */
	DE_PADSENSOR     = 47,  /* gamepads/N/events, Deskpadsensor */
	DE_V2MAX         = 48,  /* end of the version 2 types */
};

/* Version 1 mask word: bit t selects type t < 32. */
#define DESK_MASK(t)  (UINT32_C(1) << (t))

enum Deskmask : uint32_t {
	DM_MOUSE     = DESK_MASK(DE_MOUSE),
	DM_KEY       = DESK_MASK(DE_KEY),
	DM_RESIZE    = DESK_MASK(DE_RESIZE),
	DM_MOVE      = DESK_MASK(DE_MOVE),
	DM_EXPOSE    = DESK_MASK(DE_EXPOSE),
	DM_FRAME     = DESK_MASK(DE_FRAME),
	DM_FOCUS     = DESK_MASK(DE_FOCUS),
	DM_ENTER     = DESK_MASK(DE_ENTER),
	DM_LEAVE     = DESK_MASK(DE_LEAVE),
	DM_CLOSE     = DESK_MASK(DE_CLOSE),
	DM_MENU      = DESK_MASK(DE_MENU),
	DM_STATE     = DESK_MASK(DE_STATE),
	DM_DROP      = DESK_MASK(DE_DROP),
	DM_WINNEW    = DESK_MASK(DE_WINNEW),
	DM_WINGONE   = DESK_MASK(DE_WINGONE),
	DM_WORKSPACE = DESK_MASK(DE_WORKSPACE),
	DM_THEME     = DESK_MASK(DE_THEME),
	DM_QUIT      = DESK_MASK(DE_QUIT),

	DM_ALL       = DESK_MASK(DE_MAX) - 1,
	/* what a window gets before it writes a mask message */
	DM_DEFAULT   = DM_MOUSE|DM_KEY|DM_RESIZE|DM_EXPOSE|DM_FRAME|DM_FOCUS|DM_CLOSE|DM_MENU|DM_STATE|DM_QUIT,
};

/* v2: the full mask, DESK_NTYPES bits; word 0 is the version 1 mask. */
typedef struct Deskmaskset { uint32_t w[DESK_MASKWORDS]; } Deskmaskset;

static inline void desk_maskadd(Deskmaskset *m, uint32_t t)
{
	if(t < DESK_NTYPES)
		m->w[t / 32] |= UINT32_C(1) << (t % 32);
}

static inline void desk_maskdel(Deskmaskset *m, uint32_t t)
{
	if(t < DESK_NTYPES)
		m->w[t / 32] &= ~(UINT32_C(1) << (t % 32));
}

static inline bool desk_maskhas(const Deskmaskset *m, uint32_t t)
{
	return t < DESK_NTYPES && (m->w[t / 32] >> (t % 32) & 1) != 0;
}

/* v2: event classes, which the mask message accepts by name. */
enum Deskclass : uint32_t {
	DC_NONE     = 0,
	DC_POINTER  = 1,    /* "pointer":  mouse enter leave scroll motion pointer proximity */
	DC_KEYBOARD = 2,    /* "keyboard": key keymap */
	DC_TEXT     = 3,    /* "text":     text preedit delsurround */
	DC_WINDOW   = 4,    /* "window":   resize move expose focus close state configure */
	DC_FRAME    = 5,    /* "frame":    frame */
	DC_MENU     = 6,    /* "menu":     menu */
	DC_DND      = 7,    /* "dnd":      drop */
	DC_ACK      = 8,    /* "ack":      ack */
	DC_POPUP    = 9,    /* "popup":    popupdone */
	DC_DESKTOP  = 10,   /* "desktop":  winnew wingone workspace theme quit output gamepad */
	DC_GAMEPAD  = 11,   /* "gamepad":  padbutton padaxis padsensor */
};

static inline uint32_t desk_typeclass(uint32_t t)
{
	switch(t){
	case DE_MOUSE: case DE_ENTER: case DE_LEAVE: case DE_SCROLL:
	case DE_MOTION: case DE_POINTER: case DE_PROXIMITY:
		return DC_POINTER;
	case DE_KEY: case DE_KEYMAP:
		return DC_KEYBOARD;
	case DE_TEXT: case DE_PREEDIT: case DE_DELSURROUND:
		return DC_TEXT;
	case DE_RESIZE: case DE_MOVE: case DE_EXPOSE: case DE_FOCUS:
	case DE_CLOSE: case DE_STATE: case DE_CONFIGURE:
		return DC_WINDOW;
	case DE_FRAME:
		return DC_FRAME;
	case DE_MENU:
		return DC_MENU;
	case DE_DROP:
		return DC_DND;
	case DE_ACK:
		return DC_ACK;
	case DE_POPUPDONE:
		return DC_POPUP;
	case DE_WINNEW: case DE_WINGONE: case DE_WORKSPACE: case DE_THEME:
	case DE_QUIT: case DE_OUTPUT: case DE_GAMEPAD:
		return DC_DESKTOP;
	case DE_PADBUTTON: case DE_PADAXIS: case DE_PADSENSOR:
		return DC_GAMEPAD;
	default:
		return DC_NONE;
	}
}

typedef struct Deskevent {
	uint32_t type;              /* Desketype */
	uint32_t win;               /* window id, 0 for desktop-level events */
	uint32_t msec;              /* server clock, milliseconds, wraps */
	uint32_t seq;               /* per-stream sequence number, wraps */
	int32_t  a, b, c, d;        /* payload, see Desketype */
} Deskevent;

/* DE_MOUSE.c: same encoding as mouse(3), so libdraw code carries over. */
enum Deskbuttons : uint32_t {
	DB_LEFT      = 1,           /* v2: also a pen tip in contact */
	DB_MIDDLE    = 2,           /* v2: also a pen's first barrel button */
	DB_RIGHT     = 4,           /* v2: also a pen's second barrel button */
	DB_WHEELUP   = 8,
	DB_WHEELDOWN = 16,
	DB_BUTTON4   = 32,
	DB_BUTTON5   = 64,
};

/* DE_MOUSE.d and DE_KEY.c: modifier state. DK_META is the Amiga/Command key. */
enum Deskmods : uint32_t {
	DK_SHIFT = 1,
	DK_CTRL  = 2,
	DK_ALT   = 4,
	DK_META  = 8,
	DK_CAPS  = 16,
	DK_NUM   = 32,
	/* v2: which side is held, and the third shift level (AltGr) */
	DK_RSHIFT = 1u << 8,
	DK_RCTRL  = 1u << 9,
	DK_RALT   = 1u << 10,
	DK_RMETA  = 1u << 11,
	DK_LEVEL3 = 1u << 12,
};

/* DE_KEY.d */
enum Deskkeyflags : uint32_t {
	DKF_DOWN    = 1,            /* 0 means key release */
	DKF_REPEAT  = 2,            /* autorepeat of a held key */
	DKF_NORUNE  = 4,            /* a is 0: modifier or dead key; use b */
	DKF_IMEPASS = 8,            /* v2: text input is on and the input method passed this key on */
	DKF_BOUND   = 16,           /* v2: a `bindp` bind also acted on this key */
	DKF_CANCEL  = 32,           /* v2: a release made by the server because focus left with the key held */
};

/* DE_KEY.a: runes for non-printing keys, as in keyboard.h. */
enum Deskkeys : uint32_t {
	DESK_KF      = 0xF000,      /* KF|1..KF|12 are F1..F12 */
	DESK_KHOME   = 0xF00D,
	DESK_KUP     = 0xF00E,
	DESK_KPGUP   = 0xF00F,
	DESK_KLEFT   = 0xF011,
	DESK_KRIGHT  = 0xF012,
	DESK_KPGDOWN = 0xF013,
	DESK_KINS    = 0xF014,
	DESK_KEND    = 0xF018,
	DESK_KDOWN   = 0x80,
	DESK_KDEL    = 0x7F,
	DESK_KESC    = 0x1B,
};

/* ---------------------------------------------------------------------
 * v2: extended records. Head, then a typed body of bodylen bytes, then
 * datalen bytes of variable data, then zero padding to DESK_EXTALIGN.
 * size == DESK_EXTHEADSZ + bodylen + datalen rounded up to 8.
 * The first four words match Deskevent. A body only grows by appending
 * fields in a later version, so a reader uses the fields it knows and
 * skips the rest by bodylen.
 */
#define DESK_EXT          (UINT32_C(1) << 31)  /* set in Deskexthead.type */

typedef struct Deskexthead {
	uint32_t type;              /* Desketype | DESK_EXT */
	uint32_t win;               /* window id, 0 for desktop-level and gamepad events */
	uint32_t msec;              /* as Deskevent.msec */
	uint32_t seq;               /* as Deskevent.seq: the same stream counter */
	uint32_t size;              /* bytes of the whole record, a multiple of 8 */
	uint16_t bodylen;           /* bytes of typed body, a multiple of 4 */
	uint16_t datalen;           /* bytes of variable data, unpadded */
	uint64_t nsec;              /* event time, ns, on the SC clock: mach_absolute_time in ns (SC-TIME-001, SC-TIME-002) */
} Deskexthead;

/* The body of a version 1 type in the extended format starts with its words. */
typedef struct Deskwords { int32_t a, b, c, d; } Deskwords;

/* DE_MOUSE body: the pointer record, with pen fields (zero when not a pen). */
typedef struct Deskpointer {
	Deskwords v1;               /* a=x b=y c=buttons d=modifiers, as the plain record */
	int32_t  fx, fy;            /* position, window coords, 24.8 fixed point */
	uint32_t flags;             /* Deskpointerflags */
	uint32_t tool;              /* tool serial from DE_PROXIMITY, 0 for a mouse */
	uint16_t tooltype;          /* Desktool */
	uint16_t pressure;          /* 0..65535 */
	int16_t  tiltx, tilty;      /* hundredths of a degree, -9000..9000 */
	uint16_t rotation;          /* hundredths of a degree, 0..35999 */
	uint16_t distance;          /* 0 (touching) .. 65535 (edge of proximity) */
	uint32_t reserved;
} Deskpointer;

enum Deskpointerflags : uint32_t {
	DMF_PEN    = 1,             /* this sample is from a pen tool */
	DMF_WARPED = 2,             /* the first sample after a `pointer warp` */
	DMF_HOVER  = 4,             /* pen in proximity, not in contact */
};

enum Desktool : uint16_t {
	DESK_TOOL_MOUSE    = 0,
	DESK_TOOL_PEN      = 1,
	DESK_TOOL_ERASER   = 2,
	DESK_TOOL_BRUSH    = 3,
	DESK_TOOL_PENCIL   = 4,
	DESK_TOOL_AIRBRUSH = 5,
	DESK_TOOL_LENS     = 6,     /* a puck */
};

/* DE_KEY body */
typedef struct Deskkey {
	Deskwords v1;               /* a=rune b=HID usage c=modifiers after d=Deskkeyflags */
	uint32_t keysym;            /* X11-compatible keysym for non-printing keys; the rune otherwise */
	uint32_t base;              /* the rune this key gives with no modifiers, in the active layout */
	uint32_t layout;            /* index of the active layout in the keymap */
	uint32_t reserved;
} Deskkey;

/* DE_FRAME body: presentation feedback. */
typedef struct Deskframe {
	Deskwords v1;               /* a=frame counter (low 32 bits of frame) b=us since previous frame */
	uint64_t frame;             /* frame sequence of this window's clock */
	uint64_t target;            /* ns, SC clock: when a present written now can next be presented */
	uint64_t presented;         /* ns, SC clock: when the latest present reported here became visible; 0 if none */
	uint64_t refresh;           /* ns: the clock's interval (the throttled one if throttled) */
	uint32_t present;           /* which present `presented` is: the count of accepted presents */
	uint32_t config;            /* the config seq that present was tagged with */
	uint32_t flags;             /* Deskframeflags */
	uint32_t missed;            /* presents superseded before being shown, since the previous DE_FRAME */
} Deskframe;

enum Deskframeflags : uint32_t {
	DFF_HWTIME     = 1,         /* presented is the output's reported scanout time */
	DFF_COMPOSITED = 2,         /* the output reports no scanout time: presented is composition completion */
	DFF_THROTTLED  = 4,         /* the window's clock is throttled (not shown) */
	DFF_REQUESTED  = 8,         /* sent for a wantframe request */
	DFF_ZEROCOPY   = 16,        /* the buffer was scanned out without a composition copy */
	DFF_LATE       = 32,        /* the present was shown after its `at` time plus one interval */
};

/* DE_ACK body; on error, data holds the error text */
typedef struct Deskack {
	uint32_t tag;               /* the @TAG of the request */
	uint32_t error;             /* Deskerr, DESK_EOK on success */
	uint32_t descseq;           /* Deskwin.seq after the request */
	uint32_t configseq;         /* Deskconfig.seq after the request */
} Deskack;

/* DE_TEXT body; data holds UTF-8 text, whole characters only */
typedef struct Desktext {
	uint32_t imeseq;            /* the ime file serial this text was computed against */
	uint32_t flags;             /* Desktextflags */
} Desktext;

enum Desktextflags : uint32_t {
	DTF_CONT = 1,               /* the text continues in the next DE_TEXT */
	DTF_KEY  = 2,               /* typed by a key the input method passed on, not a commit */
};

/* DE_PREEDIT body; data: textlen bytes of UTF-8, zero padding to 4, then nstyle Deskpestyle */
typedef struct Deskpreedit {
	uint32_t imeseq;
	uint32_t textlen;           /* 0 ends the composition */
	int32_t  cursor0, cursor1;  /* byte offsets into the text; -1 hides the cursor */
	uint32_t nstyle;
	uint32_t reserved;
} Deskpreedit;

typedef struct Deskpestyle {
	uint32_t start, end;        /* byte offsets, half-open */
	uint32_t style;             /* Deskpestyles */
} Deskpestyle;

enum Deskpestyles : uint32_t {
	DESK_PE_UNDERLINE = 1,
	DESK_PE_SELECTED  = 2,      /* the segment being converted */
	DESK_PE_INACTIVE  = 4,
};

/* DE_DELSURROUND body: delete these many bytes before and after the cursor, before the next DE_TEXT */
typedef struct Deskdelsurround {
	uint32_t imeseq;
	uint32_t before, after;     /* bytes of UTF-8, whole characters */
	uint32_t reserved;
} Deskdelsurround;

/* DE_KEYMAP body; data: the layout name. The table is the keymap file. */
typedef struct Deskkeymap {
	uint32_t gen;               /* keymap generation: +1 per change */
	uint32_t layout;            /* index of the active layout */
	uint32_t delay;             /* repeat delay, ms; 0 = the server does not repeat */
	uint32_t interval;          /* repeat interval, us */
} Deskkeymap;

/* DE_SCROLL body */
typedef struct Deskscroll {
	int32_t  dx, dy;            /* logical points, 24.8 fixed point; positive is right and down */
	int32_t  v120x, v120y;      /* wheel detents in 1/120 units; 0 for a continuous source */
	uint32_t flags;             /* Deskscrollflags */
	uint32_t reserved;
} Deskscroll;

enum Deskscrollflags : uint32_t {
	DSF_PRECISE  = 1,           /* continuous source (trackpad), not detents */
	DSF_INVERTED = 2,           /* natural scrolling is on; deltas already follow it */
	DSF_STOP     = 4,           /* the continuous sequence ended (fingers lifted) */
};

/* DE_MOTION body: relative motion, never coalesced into absolute motion */
typedef struct Deskmotion {
	int32_t dx, dy;             /* unaccelerated device counts, 24.8 fixed point */
	int32_t adx, ady;           /* accelerated, logical points, 24.8 fixed point */
} Deskmotion;

/* DE_POINTER body */
typedef struct Deskpointerstate {
	uint32_t state;             /* Deskpointerlock */
	uint32_t reason;            /* Deskpointerreason */
} Deskpointerstate;

enum Deskpointerlock : uint32_t {
	DPS_FREE     = 0,
	DPS_LOCKED   = 1,
	DPS_CONFINED = 2,
};

enum Deskpointerreason : uint32_t {
	DPR_REQUEST  = 0,           /* the client asked */
	DPR_FOCUS    = 1,           /* suspended or restored by a focus change */
	DPR_GESTURE  = 2,           /* suspended or restored around a server gesture */
};

/* DE_PROXIMITY body */
typedef struct Deskproximity {
	uint32_t in;                /* 1 entered, 0 left */
	uint32_t tool;              /* tool serial, stable while the tool is in proximity */
	uint32_t tooltype;          /* Desktool */
	uint32_t caps;              /* Desktoolcaps */
	uint64_t hwserial;          /* hardware serial, 0 if the device has none */
} Deskproximity;

enum Desktoolcaps : uint32_t {
	DTC_PRESSURE = 1,
	DTC_TILT     = 2,
	DTC_ROTATION = 4,
	DTC_DISTANCE = 8,
};

/* DE_POPUPDONE body */
typedef struct Deskpopupdone {
	uint32_t reason;            /* Deskpopupreason */
	uint32_t reserved;
} Deskpopupdone;

enum Deskpopupreason : uint32_t {
	DPD_OUTSIDE = 1,            /* a press outside the popup chain */
	DPD_PARENT  = 2,            /* the parent was hidden, moved to another workspace or deleted */
};

/* DE_OUTPUT body; data: the output's stable name */
typedef struct Deskoutput {
	uint32_t id;                /* stable while the output exists */
	uint32_t change;            /* Deskoutputchange */
	uint32_t scalenum, scaleden;
	uint32_t refresh;           /* ns */
	uint32_t flags;             /* Deskoutputflags */
	Deskrect r;                 /* logical screen rectangle */
	uint32_t pdx, pdy;          /* mode, device pixels */
} Deskoutput;

enum Deskoutputchange : uint32_t {
	DESK_OUT_ADDED   = 1,
	DESK_OUT_REMOVED = 2,
	DESK_OUT_CHANGED = 3,
};

enum Deskoutputflags : uint32_t {
	DESK_OUT_PRIMARY = 1,
	DESK_OUT_HWTIME  = 2,       /* reports scanout times: DE_FRAME can carry DFF_HWTIME */
};

/* DE_GAMEPAD body; data: the pad's name */
typedef struct Deskgamepad {
	uint32_t pad;               /* N in gamepads/N */
	uint32_t connected;         /* 1 connected, 0 gone */
	uint32_t caps;              /* Deskpadcaps */
	uint32_t gen;               /* generation of slot N: +1 per connection */
} Deskgamepad;

enum Deskpadcaps : uint32_t {
	DGC_RUMBLE        = 1,
	DGC_TRIGGERRUMBLE = 2,
	DGC_LED           = 4,
	DGC_PLAYER        = 8,
	DGC_GYRO          = 16,
	DGC_ACCEL         = 32,
	DGC_TOUCHPAD      = 64,
};

/* The mapped layout: every pad reports these buttons and axes. */
enum Deskpadbuttons : uint32_t {
	DG_A = 1u << 0, DG_B = 1u << 1, DG_X = 1u << 2, DG_Y = 1u << 3,     /* by position: south east west north */
	DG_BACK = 1u << 4, DG_GUIDE = 1u << 5, DG_START = 1u << 6,
	DG_LSTICK = 1u << 7, DG_RSTICK = 1u << 8,
	DG_LSHOULDER = 1u << 9, DG_RSHOULDER = 1u << 10,
	DG_UP = 1u << 11, DG_DOWN = 1u << 12, DG_LEFT = 1u << 13, DG_RIGHT = 1u << 14,
	DG_MISC = 1u << 15,                                               /* share, capture, mute */
	DG_PADDLE1 = 1u << 16, DG_PADDLE2 = 1u << 17, DG_PADDLE3 = 1u << 18, DG_PADDLE4 = 1u << 19,
	DG_TOUCHPAD = 1u << 20,
};

enum Deskpadaxes : uint32_t {
	DG_LEFTX = 0, DG_LEFTY = 1, DG_RIGHTX = 2, DG_RIGHTY = 3,   /* -32768..32767, +y is down */
	DG_LTRIGGER = 4, DG_RTRIGGER = 5,                          /* 0..32767 */
	DG_NAXES = 6,
};

enum Deskpadsensors : uint32_t {
	DG_GYRO  = 1,               /* x y z in millidegrees per second */
	DG_ACCEL = 2,               /* x y z in mm/s^2 */
};

typedef struct Deskpadbutton { uint32_t button, down, buttons, reserved; } Deskpadbutton;
typedef struct Deskpadaxis   { uint32_t axis; int32_t value; uint32_t reserved[2]; } Deskpadaxis;
typedef struct Deskpadsensor { uint32_t sensor; int32_t x, y, z; } Deskpadsensor;

/* gamepads/N/state: a snapshot, 32 bytes */
typedef struct Deskpadstate {
	uint32_t seq;               /* seq of the last event folded in */
	uint32_t buttons;           /* Deskpadbuttons held */
	int16_t  axes[DG_NAXES];
	uint32_t reserved;
	uint64_t nsec;              /* time of the last event folded in, ns, SC clock */
} Deskpadstate;

/* ---------------------------------------------------------------------
 * v2: error codes. A failed ctl write's error string starts with the
 * code's name (DESK_ERR_*) and ": "; DE_ACK carries the number.
 */
enum Deskerr : uint32_t {
	DESK_EOK          = 0,
	DESK_EBADMSG      = 1,      /* "badmsg": unknown message or malformed line */
	DESK_EBADARG      = 2,      /* "badarg": an argument is out of range or of the wrong kind */
	DESK_EBUSY        = 3,      /* "busy": the latency bound is reached, or a read is already outstanding on the stream */
	DESK_ENOCONFIG    = 4,      /* "noconfig": a present named a config seq never issued */
	DESK_ENOFOCUS     = 5,      /* "nofocus": needs keyboard focus (pointer lock, warp) */
	DESK_ENOPRESS     = 6,      /* "nopress": an interactive move or resize needs a pressed button */
	DESK_EGONE        = 7,      /* "gone": the window or pad no longer exists */
	DESK_EPERM        = 8,      /* "perm": the attach does not grant this */
	DESK_EUNSUPPORTED = 9,      /* "unsupported": the capability is not offered */
	DESK_ESHORT       = 10,     /* "short": a read too small for the next whole record */
};

#define DESK_ERR_BADMSG      "badmsg"
#define DESK_ERR_BADARG      "badarg"
#define DESK_ERR_BUSY        "busy"
#define DESK_ERR_NOCONFIG    "noconfig"
#define DESK_ERR_NOFOCUS     "nofocus"
#define DESK_ERR_NOPRESS     "nopress"
#define DESK_ERR_GONE        "gone"
#define DESK_ERR_PERM        "perm"
#define DESK_ERR_UNSUPPORTED "unsupported"
#define DESK_ERR_SHORT       "short"

/* ---------------------------------------------------------------------
 * Control messages. Each is a single text line written to a ctl file:
 * a command word followed by space-separated arguments. Numbers are
 * decimal. A write fails with an error string on a bad message. The
 * spelling here is normative; argument shapes are in the comments.
 * v2: a line may start with "@TAG " to ask for a DE_ACK.
 */
#define DESK_CTL_TAG       '@'         /* @TAG message...  (TAG: 1..4294967295) */

/* windows/N/ctl */
#define DESK_CTL_RESIZE    "resize"    /* resize DX DY  |  resize -r X0 Y0 X1 Y1 (screen)  |  v2: resize +-DX +-DY  |  resize [edge E] */
#define DESK_CTL_MOVE      "move"      /* move X Y  (screen coords of content x0,y0)  |  v2: move +-DX +-DY  |  move */
#define DESK_CTL_RAISE     "raise"     /* to front of its layer */
#define DESK_CTL_LOWER     "lower"     /* to back of its layer */
#define DESK_CTL_SNAP      "snap"      /* snap left|right|top|bottom|full|center|grid C R I */
#define DESK_CTL_ZOOM      "zoom"      /* toggle zoom geometry;  zoom X0 Y0 X1 Y1 sets it */
#define DESK_CTL_HIDE      "hide"      /* iconify */
#define DESK_CTL_SHOW      "show"      /* de-iconify and raise */
#define DESK_CTL_FOCUS     "focus"     /* make current: keyboard and menu bar */
#define DESK_CTL_TITLE     "title"     /* title TEXT... (rest of line) */
#define DESK_CTL_FLAGS     "flags"     /* flags +close -depth ... (names of DF_ bits, lowercase) */
#define DESK_CTL_MINSIZE   "minsize"   /* minsize DX DY */
#define DESK_CTL_MAXSIZE   "maxsize"   /* maxsize DX DY  (0 0 = unlimited) */
#define DESK_CTL_MASK      "mask"      /* mask 0xHEX  |  mask NAME...  |  v2: mask +NAME -NAME ... (types or classes) */
#define DESK_CTL_PRESENT   "present"   /* present [X0 Y0 X1 Y1] [config SEQ] [at NS]  (window coords; none = all) */
#define DESK_CTL_MODE      "mode"      /* mode copy|flip */
#define DESK_CTL_CHAN      "chan"      /* chan x8r8g8b8|r8g8b8a8|...  (image(6) names) */
#define DESK_CTL_GRAB      "grab"      /* grab on|off  (pointer) */
#define DESK_CTL_PARENT    "parent"    /* parent ID   (makes this a transient of ID) */
#define DESK_CTL_WORKSPACE "workspace" /* workspace N */
#define DESK_CTL_PID       "pid"       /* pid N  (lead process, for the label and Kill) */
#define DESK_CTL_CLOSE     "close"     /* ask the client to close: delivers DE_CLOSE */
#define DESK_CTL_DELETE    "delete"    /* destroy the window now; kills the pid's note group */
/* v2 window ctl */
#define DESK_CTL_WANTFRAME "wantframe" /* one DE_FRAME at the next tick; coalesced */
#define DESK_CTL_LATENCY   "latency"   /* latency mailbox | latency N  (1..DESK_MAXLATENCY) */
#define DESK_CTL_BUFFER    "buffer"    /* buffer logical | buffer device | buffer DX DY */
#define DESK_CTL_VIEWPORT  "viewport"  /* viewport X0 Y0 X1 Y1 [linear|nearest]  |  viewport off */
#define DESK_CTL_DECOR     "decor"     /* decor server|none */
#define DESK_CTL_HIT       "hit"       /* hit REGION X0 Y0 X1 Y1  |  hit clear */
#define DESK_CTL_POPUP     "popup"     /* popup PARENT X0 Y0 X1 Y1 [anchor A] [gravity G] [offset DX DY] [flip] [slide] [resize] [grab] */
#define DESK_CTL_POINTER   "pointer"   /* pointer lock | confine X0 Y0 X1 Y1 | free [X Y] | warp X Y */
#define DESK_CTL_REPEAT    "repeat"    /* repeat on|off  (server key repeat for this window) */

/* desktop ctl */
#define DESK_CTL_NEW       "new"       /* new [-r X0 Y0 X1 Y1] [-dx N -dy N] [-flags ...] [-title T] [-ws N] [-kind K] [cmd args...] */
#define DESK_CTL_WSNEW     "wsnew"     /* wsnew [NAME] */
#define DESK_CTL_WSSWITCH  "wsswitch"  /* wsswitch N */
#define DESK_CTL_WSDELETE  "wsdelete"  /* wsdelete N */
#define DESK_CTL_LOWERALL  "lowerall"  /* show the desktop */
#define DESK_CTL_CLEANUP   "cleanup"   /* Workbench "Clean Up": tile icons */
#define DESK_CTL_DRAG      "drag"      /* drag PATH...: the pointer carries these until the buttons come up */
#define DESK_CTL_QUIT      "quit"      /* exit; DE_QUIT to every window first */

/* v2: written to an open events fid */
#define DESK_EVCTL_FORMAT  "format"    /* format plain|ext */

/* snap targets, arguments to DESK_CTL_SNAP */
#define DESK_SNAP_LEFT     "left"
#define DESK_SNAP_RIGHT    "right"
#define DESK_SNAP_TOP      "top"
#define DESK_SNAP_BOTTOM   "bottom"
#define DESK_SNAP_FULL     "full"
#define DESK_SNAP_CENTER   "center"
#define DESK_SNAP_GRID     "grid"      /* grid COLS ROWS INDEX */

/* v2: window kinds, arguments to new -kind */
#define DESK_KIND_TOPLEVEL  "toplevel"
#define DESK_KIND_TRANSIENT "transient"
#define DESK_KIND_POPUP     "popup"
#define DESK_KIND_TOOLTIP   "tooltip"

/* v2: hit regions, arguments to DESK_CTL_HIT (edges: n s e w ne nw se sw) */
#define DESK_HIT_CLIENT    "client"    /* delivered to the client; overrides regions below it */
#define DESK_HIT_DRAG      "drag"      /* press-drag moves, double-click zooms */
#define DESK_HIT_CLOSE     "close"
#define DESK_HIT_DEPTH     "depth"
#define DESK_HIT_ZOOM      "zoom"
#define DESK_HIT_HIDE      "hide"
#define DESK_HIT_MENU      "menu"      /* opens the window's menu at the press */
#define DESK_HIT_EDGE      "edge"      /* edge E: press-drag resizes from that edge or corner */

/* v2: popup anchors and gravities: n s e w ne nw se sw center */

/* v2: windows/N/ime, one line per write */
#define DESK_IME_ENABLE     "enable"     /* enable [purpose P] */
#define DESK_IME_DISABLE    "disable"
#define DESK_IME_RECT       "rect"       /* rect X0 Y0 X1 Y1  (window coords: the caret) */
#define DESK_IME_PURPOSE    "purpose"    /* purpose normal|password|number|phone|email|url|terminal */
#define DESK_IME_SURROUND   "surrounding" /* surrounding CURSOR ANCHOR TEXT...  (byte offsets into TEXT) */
#define DESK_IME_RESET      "reset"      /* abandon the composition */

/* v2: gamepads/N/ctl */
#define DESK_PAD_RUMBLE     "rumble"     /* rumble LOW HIGH MS  (0..65535) */
#define DESK_PAD_TRIGGERS   "triggers"   /* triggers LEFT RIGHT MS */
#define DESK_PAD_LED        "led"        /* led R G B */
#define DESK_PAD_PLAYER     "player"     /* player N */
#define DESK_PAD_SENSORS    "sensors"    /* sensors on|off */
#define DESK_PAD_BACKGROUND "background" /* background on|off */

/* v2: capability names, one per line in /n/desktop/caps as "NAME VERSION" */
#define DESK_CAP_PROTOCOL   "protocol"
#define DESK_CAP_EXT        "ext"
#define DESK_CAP_ACK        "ack"
#define DESK_CAP_CONFIGURE  "configure"
#define DESK_CAP_FRAME      "frame"
#define DESK_CAP_VIEWPORT   "viewport"
#define DESK_CAP_DECOR      "decor"
#define DESK_CAP_POPUP      "popup"
#define DESK_CAP_KEYMAP     "keymap"
#define DESK_CAP_IME        "ime"
#define DESK_CAP_PEN        "pen"
#define DESK_CAP_POINTER    "pointer"
#define DESK_CAP_SCROLL     "scroll"
#define DESK_CAP_OUTPUT     "output"
#define DESK_CAP_GAMEPAD    "gamepad"
#define DESK_CAP_SURFACEGPU "surface.gpu"

/* ---------------------------------------------------------------------
 * Menu definition file (windows/N/menu). Text, one directive per line,
 * fields separated by tabs. Ids are client-chosen small integers.
 *
 *   menu  TITLE
 *   item  ID  LABEL  [SHORTCUT]
 *   check ID  LABEL  [SHORTCUT]  on|off
 *   sep
 *   sub   LABEL          ... end
 *   off   ID             (disable an item, greyed)
 *   on    ID
 */
#define DESK_MENU_MENU     "menu"
#define DESK_MENU_ITEM     "item"
#define DESK_MENU_CHECK    "check"
#define DESK_MENU_SEP      "sep"
#define DESK_MENU_SUB      "sub"
#define DESK_MENU_END      "end"
#define DESK_MENU_OFF      "off"
#define DESK_MENU_ON       "on"

/* Menu ids reserved by the server for its own Desktop menu, delivered as DE_MENU with a=0 */
enum Deskdeskmenu : int32_t {
	DESK_M_ABOUT    = 1,
	DESK_M_EXECUTE  = 2,        /* "Execute Command..." */
	DESK_M_NEWSHELL = 3,
	DESK_M_CLEANUP  = 4,
	DESK_M_SNAPSHOT = 5,        /* write /n/desktop/screen to a file */
	DESK_M_QUIT     = 6,
};

/* ---------------------------------------------------------------------
 * Wire helpers. Portable little-endian pack and unpack; no alignment
 * requirements on the byte buffers.
 */
static inline uint16_t desk_get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | p[1] << 8);
}

static inline void desk_put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static inline uint32_t desk_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void desk_put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline uint64_t desk_get64(const uint8_t *p)
{
	return (uint64_t)desk_get32(p) | (uint64_t)desk_get32(p + 4) << 32;
}

static inline void desk_put64(uint8_t *p, uint64_t v)
{
	desk_put32(p, (uint32_t)v); desk_put32(p + 4, (uint32_t)(v >> 32));
}

static inline void desk_unpackevent(const uint8_t buf[DESK_STATIC(DESK_EVENTSZ)], Deskevent *e)
{
	e->type = desk_get32(buf + 0);
	e->win  = desk_get32(buf + 4);
	e->msec = desk_get32(buf + 8);
	e->seq  = desk_get32(buf + 12);
	e->a = (int32_t)desk_get32(buf + 16);
	e->b = (int32_t)desk_get32(buf + 20);
	e->c = (int32_t)desk_get32(buf + 24);
	e->d = (int32_t)desk_get32(buf + 28);
}

static inline void desk_packevent(uint8_t buf[DESK_STATIC(DESK_EVENTSZ)], const Deskevent *e)
{
	desk_put32(buf + 0, e->type);
	desk_put32(buf + 4, e->win);
	desk_put32(buf + 8, e->msec);
	desk_put32(buf + 12, e->seq);
	desk_put32(buf + 16, (uint32_t)e->a);
	desk_put32(buf + 20, (uint32_t)e->b);
	desk_put32(buf + 24, (uint32_t)e->c);
	desk_put32(buf + 28, (uint32_t)e->d);
}

static inline void desk_unpackwin(const uint8_t buf[DESK_STATIC(DESK_DESCSZ)], Deskwin *w)
{
	uint32_t *f = (uint32_t*)w;                 /* Deskwin is 24 consecutive 32-bit fields */
	for(size_t i = 0; i < DESK_DESCSZ/4; i++)
		f[i] = desk_get32(buf + 4*i);
}

static inline void desk_packwin(uint8_t buf[DESK_STATIC(DESK_DESCSZ)], const Deskwin *w)
{
	const uint32_t *f = (const uint32_t*)w;
	for(size_t i = 0; i < DESK_DESCSZ/4; i++)
		desk_put32(buf + 4*i, f[i]);
}

/* v2: Deskconfig is 20 consecutive 32-bit fields. */
static inline void desk_unpackconfig(const uint8_t buf[DESK_STATIC(DESK_CONFIGSZ)], Deskconfig *c)
{
	uint32_t *f = (uint32_t*)c;
	for(size_t i = 0; i < DESK_CONFIGSZ/4; i++)
		f[i] = desk_get32(buf + 4*i);
}

static inline void desk_packconfig(uint8_t buf[DESK_STATIC(DESK_CONFIGSZ)], const Deskconfig *c)
{
	const uint32_t *f = (const uint32_t*)c;
	for(size_t i = 0; i < DESK_CONFIGSZ/4; i++)
		desk_put32(buf + 4*i, f[i]);
}

/* v2: the extended-record head. */
static inline void desk_unpackhead(const uint8_t buf[DESK_STATIC(DESK_EXTHEADSZ)], Deskexthead *h)
{
	h->type    = desk_get32(buf + 0);
	h->win     = desk_get32(buf + 4);
	h->msec    = desk_get32(buf + 8);
	h->seq     = desk_get32(buf + 12);
	h->size    = desk_get32(buf + 16);
	h->bodylen = desk_get16(buf + 20);
	h->datalen = desk_get16(buf + 22);
	h->nsec    = desk_get64(buf + 24);
}

static inline void desk_packhead(uint8_t buf[DESK_STATIC(DESK_EXTHEADSZ)], const Deskexthead *h)
{
	desk_put32(buf + 0, h->type);
	desk_put32(buf + 4, h->win);
	desk_put32(buf + 8, h->msec);
	desk_put32(buf + 12, h->seq);
	desk_put32(buf + 16, h->size);
	desk_put16(buf + 20, h->bodylen);
	desk_put16(buf + 22, h->datalen);
	desk_put64(buf + 24, h->nsec);
}

/* v2: the size an extended record must have, given its body and data. */
static inline uint32_t desk_extsize(uint32_t bodylen, uint32_t datalen)
{
	return DESK_EXTHEADSZ + bodylen + ((datalen + DESK_EXTALIGN - 1) & ~(uint32_t)(DESK_EXTALIGN - 1));
}

/*
 * v2: the length of the record at buf, given the bytes left in a read
 * reply, or 0 if what is there is not one well-formed record. In the
 * plain format every record is DESK_EVENTSZ.
 */
static inline uint32_t desk_reclen(const uint8_t *buf, size_t avail)
{
	uint32_t type, size;

	if(avail < DESK_EVENTSZ)
		return 0;
	type = desk_get32(buf);
	if((type & DESK_EXT) == 0)
		return DESK_EVENTSZ;
	size = desk_get32(buf + 16);
	if(size < DESK_EXTHEADSZ || size > DESK_EXTMAX || size % DESK_EXTALIGN != 0 || size > avail)
		return 0;
	if(size != desk_extsize(desk_get16(buf + 20), desk_get16(buf + 22)))
		return 0;
	return size;
}

/* Rectangle carried in a DE_EXPOSE event. */
static inline Deskrect desk_eventrect(const Deskevent *e)
{
	Deskrect r = { e->a, e->b, e->c, e->d };
	return r;
}

/* Byte offset of pixel (x, y) in the image file, given the descriptor. */
static inline uint64_t desk_pixeloffset(const Deskwin *w, int32_t x, int32_t y)
{
	return (uint64_t)y * w->stride + (uint64_t)x * (desk_chandepth(w->chan) / 8);
}

/* v2: the same, given a configuration (buffer pixels). */
static inline uint64_t desk_bufoffset(const Deskconfig *c, int32_t x, int32_t y)
{
	return (uint64_t)y * c->stride + (uint64_t)x * (desk_chandepth(c->chan) / 8);
}

/* ---------------------------------------------------------------------
 * Layout guarantees the protocol depends on. Every field offset of every
 * wire record is asserted in docs/desktop/desktop_layout.c; the sizes
 * are asserted here so that every includer checks them.
 */
static_assert(sizeof(Deskevent) == DESK_EVENTSZ, "Deskevent must be 32 bytes");
static_assert(sizeof(Deskwin) == DESK_DESCSZ, "Deskwin must be 96 bytes");
static_assert(sizeof(Deskrect) == 16, "Deskrect must be 16 bytes");
static_assert(alignof(Deskwin) == 4, "Deskwin must be 4-byte aligned");
/* The version 1 assertion, kept with its version 1 meaning: the version 1
 * types fit the version 1 mask word. Version 2 types live above it. */
static_assert(DE_MAX <= 32, "version 1 event types must fit the 32-bit version 1 mask word");
static_assert(DE_CONFIGURE >= 32, "version 2 event types must not share the version 1 mask word");
static_assert(DE_V2MAX <= DESK_NTYPES, "event types must fit the Deskmaskset");
static_assert(sizeof(Deskmaskset) == DESK_NTYPES / 8, "Deskmaskset is one bit per type");
static_assert(sizeof(Deskexthead) == DESK_EXTHEADSZ, "Deskexthead must be 32 bytes");
static_assert(sizeof(Deskconfig) == DESK_CONFIGSZ, "Deskconfig must be 80 bytes");
static_assert(sizeof(Deskframe) == 64, "Deskframe must be 64 bytes");
static_assert(sizeof(Deskpointer) == 48, "Deskpointer must be 48 bytes");
static_assert(sizeof(Deskkey) == 32, "Deskkey must be 32 bytes");
static_assert(sizeof(Deskpadstate) == 32, "Deskpadstate must be 32 bytes");

#ifdef __cplusplus
}
#endif

#endif /* NEODARWIN_DESKTOP_H */
