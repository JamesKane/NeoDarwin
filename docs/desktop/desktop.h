/* Provenance: distilled from the plan-neo pilot (commit b0a35a1). Reference copy; the build consumes the mirror. */
/*
 * desktop.h - NeoDarwin desktop windowing protocol over 9P, version 1.
 *
 * Companion to docs/desktop/window-protocol.md, which is the normative text.
 * This header defines the binary records that cross the 9P boundary
 * (window descriptors and events), the bitmasks used in them, and the
 * spelling of every control message. It is freestanding C23: only
 * <stdint.h> and <stddef.h> are required, no libc.
 *
 * Wire encoding: every multi-byte field is little-endian regardless of
 * host. On the two supported targets (x86_64, aarch64) the structs
 * below have the same layout as the wire format, so a client may read
 * straight into them; the pack/unpack helpers are the portable path.
 */
#ifndef PLANNEO_DESKTOP_H
#define PLANNEO_DESKTOP_H

#include <stdint.h>
#include <stddef.h>

#define DESK_VERSION      1
#define DESK_MOUNT        "/n/desktop"      /* default mount point */
#define DESK_SRVFMT       "wsys.%s.%d"        /* nsd registry name: user, pid */

#define DESK_EVENTSZ      32                  /* bytes per Deskevent on the wire */
#define DESK_DESCSZ       96                  /* bytes per Deskwin on the wire */
#define DESK_TITLEMAX     256                 /* bytes, UTF-8, including NUL */
#define DESK_MAXQUEUE     1024                /* events queued per window before lag */

/* ---------------------------------------------------------------------
 * Geometry. Screen coordinates have their origin at the top left of the
 * desktop; window coordinates have their origin at the top left of the
 * window's content area (inside the frame). Rectangles are half-open:
 * [x0,x1) x [y0,y1), as in libdraw.
 */
typedef struct Deskpoint { int32_t x, y; } Deskpoint;
typedef struct Deskrect  { int32_t x0, y0, x1, y1; } Deskrect;

static inline int32_t desk_dx(Deskrect r) { return r.x1 - r.x0; }
static inline int32_t desk_dy(Deskrect r) { return r.y1 - r.y0; }
static inline bool desk_ptinrect(Deskpoint p, Deskrect r)
{
	return p.x >= r.x0 && p.x < r.x1 && p.y >= r.y0 && p.y < r.y1;
}

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

	DF_DEFAULT    = DF_TITLEBAR|DF_CLOSE|DF_DEPTH|DF_ZOOM|DF_SIZE|DF_DRAG|DF_RETAIN,
	DF_DIALOG     = DF_TITLEBAR|DF_CLOSE|DF_DRAG|DF_TRANSIENT|DF_RETAIN,
};

/* Window state (Deskwin.state). Changed by the user, the server or ctl. */
enum Deskstate : uint32_t {
	DS_VISIBLE   = 1u << 0,     /* mapped on its workspace */
	DS_FOCUSED   = 1u << 1,     /* receives keyboard input, owns the menu bar */
	DS_HIDDEN    = 1u << 2,     /* iconified to the desktop (Workbench icon) */
	DS_ZOOMED    = 1u << 3,     /* at its alternate (zoom) geometry */
	DS_GRABBED   = 1u << 4,     /* pointer grabbed by this window */
	DS_LAGGING   = 1u << 5,     /* event queue overflowed; motion is being dropped */
	DS_SNAPPED   = 1u << 6,     /* placed by a snap command; cleared by move/resize */
	DS_FULL      = 1u << 7,     /* covers the whole workspace, frame hidden */
	DS_CLOSING   = 1u << 8,     /* DE_CLOSE sent, waiting for the client */
};

/* Buffer modes (see "image" and the present message). */
enum Deskmode : uint32_t {
	DESK_MODECOPY = 0,          /* present copies the dirty rect into the front buffer */
	DESK_MODEFLIP = 1,          /* present swaps buffers; client must repaint what it presents */
};

/* ---------------------------------------------------------------------
 * Window descriptor: the binary contents of windows/N/desc, and the
 * record delivered in DE_WINNEW. 96 bytes. All rects in screen coords.
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
 * Events: the binary records read from windows/N/events and from the
 * desktop-level events file. 32 bytes each; a read returns whole
 * records only. Payload fields a..d depend on type as listed.
 */
enum Desketype : uint32_t {
	DE_NONE      = 0,
	DE_MOUSE     = 1,   /* a=x b=y (window coords) c=buttons d=modifiers */
	DE_KEY       = 2,   /* a=rune b=keycode c=modifiers d=Deskkeyflags */
	DE_RESIZE    = 3,   /* a=dx b=dy c=stride d=chan; image reallocated */
	DE_MOVE      = 4,   /* a=x0 b=y0 (screen coords of content) */
	DE_EXPOSE    = 5,   /* a..d = rect (window coords) to repaint; only without DF_RETAIN */
	DE_FRAME     = 6,   /* a=frame counter b=microseconds since previous frame */
	DE_FOCUS     = 7,   /* a=1 gained, 0 lost */
	DE_ENTER     = 8,   /* a=x b=y pointer entered the content area */
	DE_LEAVE     = 9,   /* a=x b=y pointer left the content area */
	DE_CLOSE     = 10,  /* close gadget or menu; a=1 if forced (user chose Kill) */
	DE_MENU      = 11,  /* a=menu id b=item id c=1 if now checked, 0 otherwise */
	DE_STATE     = 12,  /* a=new state b=old state c=depth */
	DE_DROP      = 13,  /* a=x b=y c=count of paths, read windows/N/drop for them */
	DE_WINNEW    = 14,  /* desktop-level: window created; win=id */
	DE_WINGONE   = 15,  /* desktop-level: window destroyed; win=id */
	DE_WORKSPACE = 16,  /* desktop-level: a=new index b=old index */
	DE_THEME     = 17,  /* desktop-level: theme file changed */
	DE_QUIT      = 18,  /* server is exiting; all files will return errors */
	DE_MAX       = 19,
};

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

typedef struct Deskevent {
	uint32_t type;              /* Desketype */
	uint32_t win;               /* window id, 0 for desktop-level events */
	uint32_t msec;              /* server clock, milliseconds, wraps */
	uint32_t seq;               /* per-stream sequence number, wraps */
	int32_t  a, b, c, d;        /* payload, see Desketype */
} Deskevent;

/* DE_MOUSE.c: same encoding as mouse(3), so libdraw code carries over. */
enum Deskbuttons : uint32_t {
	DB_LEFT      = 1,
	DB_MIDDLE    = 2,
	DB_RIGHT     = 4,
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
};

/* DE_KEY.d */
enum Deskkeyflags : uint32_t {
	DKF_DOWN   = 1,             /* 0 means key release */
	DKF_REPEAT = 2,             /* autorepeat of a held key */
	DKF_NORUNE = 4,             /* a is 0: modifier or dead key; use b */
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
 * Control messages. Each is a single text line written to a ctl file:
 * a command word followed by space-separated arguments. Numbers are
 * decimal. A write fails with an error string on a bad message. The
 * spelling here is normative; argument shapes are in the comments.
 */

/* windows/N/ctl */
#define DESK_CTL_RESIZE    "resize"    /* resize DX DY  |  resize -r X0 Y0 X1 Y1 (screen) */
#define DESK_CTL_MOVE      "move"      /* move X Y  (screen coords of content x0,y0) */
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
#define DESK_CTL_MASK      "mask"      /* mask 0xHEX  |  mask mouse key frame ... */
#define DESK_CTL_PRESENT   "present"   /* present [X0 Y0 X1 Y1]  (window coords; none = all) */
#define DESK_CTL_MODE      "mode"      /* mode copy|flip */
#define DESK_CTL_CHAN      "chan"      /* chan x8r8g8b8|r8g8b8a8|...  (image(6) names) */
#define DESK_CTL_GRAB      "grab"      /* grab on|off  (pointer) */
#define DESK_CTL_PARENT    "parent"    /* parent ID   (makes this a transient of ID) */
#define DESK_CTL_WORKSPACE "workspace" /* workspace N */
#define DESK_CTL_PID       "pid"       /* pid N  (lead process, for the label and Kill) */
#define DESK_CTL_CLOSE     "close"     /* ask the client to close: delivers DE_CLOSE */
#define DESK_CTL_DELETE    "delete"    /* destroy the window now; kills the pid's note group */

/* desktop ctl */
#define DESK_CTL_NEW       "new"       /* new [-r X0 Y0 X1 Y1] [-dx N -dy N] [-flags ...] [-title T] [-ws N] [cmd args...] */
#define DESK_CTL_WSNEW     "wsnew"     /* wsnew [NAME] */
#define DESK_CTL_WSSWITCH  "wsswitch"  /* wsswitch N */
#define DESK_CTL_WSDELETE  "wsdelete"  /* wsdelete N */
#define DESK_CTL_LOWERALL  "lowerall"  /* show the desktop */
#define DESK_CTL_CLEANUP   "cleanup"   /* Workbench "Clean Up": tile icons */
#define DESK_CTL_DRAG      "drag"      /* drag PATH...: the pointer carries these until the buttons come up */
#define DESK_CTL_QUIT      "quit"      /* exit; DE_QUIT to every window first */

/* snap targets, arguments to DESK_CTL_SNAP */
#define DESK_SNAP_LEFT     "left"
#define DESK_SNAP_RIGHT    "right"
#define DESK_SNAP_TOP      "top"
#define DESK_SNAP_BOTTOM   "bottom"
#define DESK_SNAP_FULL     "full"
#define DESK_SNAP_CENTER   "center"
#define DESK_SNAP_GRID     "grid"      /* grid COLS ROWS INDEX */

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
static inline uint32_t desk_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline void desk_put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static inline void desk_unpackevent(const uint8_t buf[static DESK_EVENTSZ], Deskevent *e)
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

static inline void desk_packevent(uint8_t buf[static DESK_EVENTSZ], const Deskevent *e)
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

static inline void desk_unpackwin(const uint8_t buf[static DESK_DESCSZ], Deskwin *w)
{
	uint32_t *f = (uint32_t*)w;                 /* Deskwin is 24 consecutive 32-bit fields */
	for(size_t i = 0; i < DESK_DESCSZ/4; i++)
		f[i] = desk_get32(buf + 4*i);
}

static inline void desk_packwin(uint8_t buf[static DESK_DESCSZ], const Deskwin *w)
{
	const uint32_t *f = (const uint32_t*)w;
	for(size_t i = 0; i < DESK_DESCSZ/4; i++)
		desk_put32(buf + 4*i, f[i]);
}

/* Rectangle carried in a DE_EXPOSE event. */
static inline Deskrect desk_eventrect(const Deskevent *e)
{
	return (Deskrect){ e->a, e->b, e->c, e->d };
}

/* Byte offset of pixel (x, y) in the image file, given the descriptor. */
static inline uint64_t desk_pixeloffset(const Deskwin *w, int32_t x, int32_t y)
{
	return (uint64_t)y * w->stride + (uint64_t)x * (desk_chandepth(w->chan) / 8);
}

/* Layout guarantees the protocol depends on. */
static_assert(sizeof(Deskevent) == DESK_EVENTSZ, "Deskevent must be 32 bytes");
static_assert(sizeof(Deskwin) == DESK_DESCSZ, "Deskwin must be 96 bytes");
static_assert(sizeof(Deskrect) == 16, "Deskrect must be 16 bytes");
static_assert(alignof(Deskwin) == 4, "Deskwin must be 4-byte aligned");
static_assert(DE_MAX <= 32, "event types must fit a 32-bit mask");

#endif /* PLANNEO_DESKTOP_H */
