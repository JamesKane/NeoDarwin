/* Provenance: distilled from the plan-neo pilot (commit b0a35a1). Reference copy; the build consumes the mirror. */
/*
 * style.h - NeoDarwin themeable style engine for the wsys window server.
 *
 * Three layers, all freestanding C23 with no allocation:
 *
 *   Theme      the parsed theme file: palette, chrome metrics, gradients,
 *              menu bar geometry, font descriptors. Text in, text out.
 *   raster     a small software rasterizer over XRGB32 buffers: fills,
 *              lines, bevels, ordered-dither gradients, 1-bit glyph blits.
 *   chrome     window frames, gadgets in their four states, drop shadows,
 *              menu bar, desktop; geometry queries for hit testing.
 *
 * The normative description of the theme file format and of the /n/theme
 * tree is docs/theme-engine.md.
 */
#ifndef PLANNEO_STYLE_H
#define PLANNEO_STYLE_H

#include <stdint.h>
#include <stddef.h>
#include "desktop.h"

#define STYLE_VERSION   1
#define STYLE_MOUNT     "/n/theme"
#define STYLE_NAMEMAX   64
#define STYLE_PATHMAX   128
#define STYLE_ERRMAX    128
#define STYLE_LINEMAX   256

/* ------------------------------------------------------------------ colours */

typedef uint32_t Scolor;                    /* 0xAARRGGBB; AA=0xff opaque */

#define SCOLOR(a, r, g, b) ((Scolor)(a) << 24 | (Scolor)(r) << 16 | (Scolor)(g) << 8 | (Scolor)(b))
#define SRGB(r, g, b)      SCOLOR(0xff, (r), (g), (b))

static inline uint8_t s_a(Scolor c) { return (uint8_t)(c >> 24); }
static inline uint8_t s_r(Scolor c) { return (uint8_t)(c >> 16); }
static inline uint8_t s_g(Scolor c) { return (uint8_t)(c >> 8); }
static inline uint8_t s_b(Scolor c) { return (uint8_t)c; }

/* Named palette slots. The names, as spelled in theme files, are in style_palettename. */
enum Spalette : uint32_t {
	SC_DESKTOP,             /* workspace background */
	SC_GRID,                /* desktop grid dots, usually translucent */
	SC_FRAME,               /* frame body, focused window */
	SC_FRAMEINACTIVE,
	SC_EDGE,                /* outer outline, focused */
	SC_EDGEINACTIVE,
	SC_EDGEDARK,            /* inner outline around content and between gadgets */
	SC_TITLETEXT,
	SC_TITLETEXTINACTIVE,
	SC_TITLESHADOW,         /* text shadow under title text */
	SC_SCANLINE,            /* overlay on alternate title rows, translucent */
	SC_GADGET,              /* gadget face, normal */
	SC_GADGETINK,
	SC_GADGETHOVER,
	SC_GADGETHOVERINK,
	SC_GADGETACTIVE,        /* pressed */
	SC_GADGETACTIVEINK,
	SC_GADGETINACTIVE,      /* window not focused */
	SC_GADGETINACTIVEINK,
	SC_SHADOW,              /* drop shadow colour, alpha honoured */
	SC_MENUBAR,
	SC_MENUEDGE,
	SC_MENUTEXT,
	SC_MENUHILITE,
	SC_MENUHILITETEXT,
	SC_ACCENT,              /* first menu title, badges */
	SC_ACCENT2,
	SC_TEXT,                /* default content text for clients using the theme */
	SC_TEXTDIM,
	SC_BACK,                /* default content background */
	SC_NPALETTE,
};
extern const char *const style_palettename[SC_NPALETTE];

/* ------------------------------------------------------------------ theme */

enum Sgradkind : uint8_t { SG_FLAT, SG_VERTICAL, SG_HORIZONTAL };

typedef struct Sgradient {
	Scolor  a, b;           /* start and end colour; SG_FLAT uses a */
	uint8_t kind;           /* Sgradkind */
	uint8_t dither;         /* 1: ordered 4x4 dither between steps */
	uint8_t bands;          /* 0: continuous, else number of colour steps */
	uint8_t pad;
} Sgradient;

enum Sfontslot : uint32_t { SF_TEXT, SF_TITLE, SF_MENU, SF_MONO, SF_NFONT };
extern const char *const style_fontname[SF_NFONT];

typedef struct Sfontdesc {
	char    path[STYLE_PATHMAX];    /* font or subfont file */
	int16_t tracking;               /* extra pixels between glyphs, may be negative */
	uint8_t bold;                   /* synthetic bold: draw twice, 1px apart */
	uint8_t pad;
} Sfontdesc;

enum Sshadowstyle : uint8_t { SS_NONE, SS_SOLID, SS_CHECKER, SS_HATCH };
enum Smenumode    : uint8_t { SM_ALWAYS, SM_BUTTON };
enum Stitlealign  : uint8_t { ST_LEFT, ST_CENTER };

typedef struct Theme {
	char     name[STYLE_NAMEMAX];
	uint32_t gen;                       /* bumped by wsys on every successful change */
	Scolor   palette[SC_NPALETTE];

	struct {
		int32_t border;                 /* frame thickness left, right, bottom */
		int32_t title;                  /* title bar height, gadgets are this tall */
		int32_t gadget;                 /* gadget width */
		int32_t corner;                 /* sizing gadget square, overlaps content */
		int32_t edge;                   /* outline thickness, 0..2 */
		int32_t gap;                    /* between gadgets */
	} frame;

	struct {
		Sgradient active, inactive;
		int32_t  pad;                   /* text inset from the first gadget */
		uint8_t  align;                 /* Stitlealign */
		uint8_t  scanlines;             /* overlay SC_SCANLINE on odd rows */
		uint8_t  textshadow;            /* 1px SC_TITLESHADOW under the text */
		uint8_t  pad2;
	} title;

	struct {
		int32_t height, pad, itempad, gap;
		int32_t edge;                   /* bottom edge thickness */
		uint8_t mode;                   /* Smenumode */
		uint8_t pad2[3];
	} menubar;

	struct {
		int32_t dx, dy;                 /* offset of the shadow from the frame */
		uint8_t style;                  /* Sshadowstyle */
		uint8_t pad[3];
	} shadow;

	struct {
		int32_t grid;                   /* dot spacing, 0 for none */
	} desktop;

	Sfontdesc font[SF_NFONT];
} Theme;

/* Fill with the compiled-in default (Retro-Future Cyberpunk). */
void   style_default(Theme *t);

/* Parse a whole theme file into t, starting from the default. Returns 0, or -1
 * with a message and the line number in err. On failure t is left as the default. */
int    style_parse(Theme *t, const char *text, size_t n, char *err, size_t errn);

/* Set one key from its textual value, as in a theme line without the key. */
int    style_set(Theme *t, const char *key, const char *value, char *err, size_t errn);

/* Textual value of one key; returns length or -1 if the key is unknown. */
int    style_get(const Theme *t, const char *key, char *buf, size_t n);

/* Serialize the whole theme as a theme file. Returns bytes written (no NUL counted). */
size_t style_format(const Theme *t, char *buf, size_t n);

/* One /n/theme/ctl line: "set KEY VALUE", "get KEY", "reset", "name NAME", "list".
 * Returns 0 and a reply (possibly empty), or -1 and an error string, in reply. */
int    style_ctl(Theme *t, const char *line, char *reply, size_t n);

/* ------------------------------------------------------------------ raster */

typedef struct Simage {
	uint32_t *pix;          /* XRGB32 rows, top to bottom */
	int32_t   w, h;
	int32_t   stride;       /* in pixels, >= w */
	/*
	 * Where drawing is allowed. Every primitive below reaches the pixels
	 * through s_clip, so setting this once confines a whole scene - which
	 * is what the compositor's damage tracking needs (roadmap D6). An
	 * empty rectangle, and so a zeroed or brace-initialised Simage, means
	 * the whole image: s_setclip(img, (Deskrect){0}) restores that.
	 */
	Deskrect  clip;
} Simage;

static inline Deskrect s_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	return (Deskrect){ x0, y0, x1, y1 };
}
static inline Deskrect s_inset(Deskrect r, int32_t n)
{
	return (Deskrect){ r.x0 + n, r.y0 + n, r.x1 - n, r.y1 - n };
}
static inline Deskrect s_offset(Deskrect r, int32_t dx, int32_t dy)
{
	return (Deskrect){ r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy };
}
Deskrect s_clip(const Simage *img, Deskrect r);      /* intersect with the image and its clip */
void     s_setclip(Simage *img, Deskrect r);        /* empty r: the whole image */
Scolor   s_blend(Scolor dst, Scolor src);           /* src over dst by src alpha */
Scolor   s_mix(Scolor a, Scolor b, int32_t t256);   /* a..b at t/256 */

void s_fill(Simage *img, Deskrect r, Scolor c);     /* alpha-blended fill */
void s_hline(Simage *img, int32_t x0, int32_t x1, int32_t y, Scolor c);
void s_vline(Simage *img, int32_t x, int32_t y0, int32_t y1, Scolor c);
void s_outline(Simage *img, Deskrect r, int32_t w, Scolor c);          /* inside r */
void s_bevel(Simage *img, Deskrect r, Scolor light, Scolor dark, int32_t w);
void s_gradient(Simage *img, Deskrect r, const Sgradient *g);
void s_scanlines(Simage *img, Deskrect r, Scolor c);                    /* odd rows */
void s_checker(Simage *img, Deskrect r, Scolor c);                      /* (x+y)&1 */
void s_hatch(Simage *img, Deskrect r, Scolor c);                        /* diagonals */
void s_dots(Simage *img, Deskrect r, int32_t period, Scolor c);
/* 1-bit mask blit: bit set -> ink. bits are rows of bstride bytes, MSB first. */
void s_blit1(Simage *img, int32_t x, int32_t y, const uint8_t *bits, int32_t bstride,
             Deskrect src, Scolor ink);

/* ------------------------------------------------------------------ fonts */

#define SFONT_MAXCHARS 256
#define SFONT_MAXSUB   24                   /* subfonts one font may hold */
#define SFONT_MAXPATH  128

/* One Plan 9 subfont (font(6)): a contiguous run of glyphs from rune `first`. */
typedef struct Ssubfont {
	int32_t  n, height, ascent;
	uint32_t first;
	int32_t  imgw, imgh, depth, bstride;
	const uint8_t *bits;                /* decoded glyph image, k1 or k8 */
	uint16_t x[SFONT_MAXCHARS + 1];
	uint8_t  top[SFONT_MAXCHARS + 1], bottom[SFONT_MAXCHARS + 1];
	uint8_t  left[SFONT_MAXCHARS + 1], width[SFONT_MAXCHARS + 1];
} Ssubfont;

/*
 * A font: the height and ascent of the whole face and the subfonts that
 * cover it, in the order the font file lists them. A single subfont file
 * loaded on its own is a font with one subfont.
 */
typedef struct Sfont {
	int32_t  height, ascent;
	int32_t  nsub;
	Ssubfont sub[SFONT_MAXSUB];
} Sfont;

/* One line of a font file: the rune range and the subfont file that holds it. */
typedef struct Ssubfontspec {
	uint32_t min, max;
	char     path[SFONT_MAXPATH];       /* as written, relative to the font file */
} Ssubfontspec;

/* Load a subfont file image (compressed or not, k1 or k8). The decoded glyph
 * bitmap is written into scratch, which must outlive the font. Returns 0 or
 * -1 with a message in err. Needs sfont_scratchsize(data, n) bytes. */
size_t sfont_scratchsize(const uint8_t *data, size_t n);
int    sfont_load(Sfont *f, const uint8_t *data, size_t n, uint32_t first,
                  uint8_t *scratch, size_t scratchn, char *err, size_t errn);

/* Add one subfont to a font, covering runes from `first`. The first call
 * fixes the font's height and ascent; later subfonts are drawn on the same
 * baseline. */
int    sfont_addsub(Sfont *f, const uint8_t *data, size_t n, uint32_t first,
                    uint8_t *scratch, size_t scratchn, char *err, size_t errn);

/* Parse a font(6) font file: "height ascent" then lines of "min max path",
 * ranges in hex. Fills spec with up to maxspec entries and returns how many,
 * or -1 with a message in err. The caller reads each path itself (relative
 * to the font file's directory) and hands the bytes to sfont_addsub. */
int    sfont_parsefontfile(const uint8_t *text, size_t n, int32_t *height, int32_t *ascent,
                           Ssubfontspec *spec, int maxspec, char *err, size_t errn);

/* Draw UTF-8 text with its top-left at (x, y). Returns the advance in pixels. */
int32_t s_text(Simage *img, const Sfont *f, int32_t x, int32_t y, const char *s,
               Scolor ink, int32_t tracking, int bold);
int32_t s_textwidth(const Sfont *f, const char *s, int32_t tracking, int bold);

/* ------------------------------------------------------------------ chrome */

enum Sgadget : int32_t {
	SG_NONE = 0, SG_CLOSE, SG_DEPTH, SG_ZOOM, SG_SIZE, SG_TITLE, SG_BORDER,
};

typedef struct Sframestate {
	uint32_t    flags;      /* DF_ bits: which gadgets exist */
	uint32_t    state;      /* DS_ bits: DS_FOCUSED selects active colours */
	int32_t     hover;      /* Sgadget under the pointer, or SG_NONE */
	int32_t     active;     /* Sgadget being pressed, or SG_NONE */
	const char *title;
} Sframestate;

/* Geometry. Content <-> frame, gadget rectangles, hit testing. All in the
 * same coordinate space as the rectangles passed in. */
Deskrect style_framerect(const Theme *t, Deskrect content, uint32_t flags);
Deskrect style_contentrect(const Theme *t, Deskrect frame, uint32_t flags);
Deskrect style_titlerect(const Theme *t, Deskrect frame, uint32_t flags);
Deskrect style_gadgetrect(const Theme *t, Deskrect frame, uint32_t flags, int32_t gadget);
int32_t  style_hit(const Theme *t, Deskrect frame, uint32_t flags, Deskpoint p);
Deskrect style_shadowrect(const Theme *t, Deskrect frame);

/* Drawing. Frames draw only the decoration ring and title bar; the content
 * rectangle is never touched except by the sizing gadget corner. */
void style_drawdesktop(Simage *img, const Theme *t, Deskrect r);
void style_drawshadow(Simage *img, const Theme *t, Deskrect frame);
void style_drawframe(Simage *img, const Theme *t, const Sfont *titlefont,
                     Deskrect frame, const Sframestate *st);
void style_drawmenubar(Simage *img, const Theme *t, const Sfont *menufont, Deskrect r,
                       const char *const *titles, int32_t ntitles, int32_t hilite);

#endif /* PLANNEO_STYLE_H */
