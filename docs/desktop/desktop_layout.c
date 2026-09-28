// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: compile-checks the C ABI records of desktop.h (spec-conventions.md §5)
/*
 * desktop_layout.c - layout check for docs/desktop/desktop.h (window
 * protocol version 2). Every record that crosses the 9P boundary has
 * its size and the offset of every field asserted here; main() round-
 * trips the pack and unpack helpers so that `bazel test //docs/desktop/...`
 * runs them. Conformance tests WP-T-058 and WP-T-059 in window-protocol.md.
 */
#include "docs/desktop/desktop.h"

#define OFF(T, f, n) static_assert(offsetof(T, f) == (n), #T "." #f " must be at offset " #n)
#define SIZE(T, n)   static_assert(sizeof(T) == (n), #T " must be " #n " bytes")

/* version 1 records: unchanged */
SIZE(Deskpoint, 8);
SIZE(Deskrect, 16);
OFF(Deskrect, x0, 0); OFF(Deskrect, y0, 4); OFF(Deskrect, x1, 8); OFF(Deskrect, y1, 12);

SIZE(Deskevent, 32);
OFF(Deskevent, type, 0); OFF(Deskevent, win, 4); OFF(Deskevent, msec, 8); OFF(Deskevent, seq, 12);
OFF(Deskevent, a, 16); OFF(Deskevent, b, 20); OFF(Deskevent, c, 24); OFF(Deskevent, d, 28);

SIZE(Deskwin, 96);
OFF(Deskwin, id, 0); OFF(Deskwin, parent, 4); OFF(Deskwin, workspace, 8); OFF(Deskwin, depth, 12);
OFF(Deskwin, r, 16); OFF(Deskwin, frame, 32);
OFF(Deskwin, mindx, 48); OFF(Deskwin, mindy, 52); OFF(Deskwin, maxdx, 56); OFF(Deskwin, maxdy, 60);
OFF(Deskwin, flags, 64); OFF(Deskwin, state, 68); OFF(Deskwin, chan, 72); OFF(Deskwin, stride, 76);
OFF(Deskwin, seq, 80); OFF(Deskwin, pid, 84); OFF(Deskwin, mode, 88); OFF(Deskwin, reserved, 92);

/* version 1 values the wire depends on */
static_assert(DE_QUIT == 18 && DE_MAX == 19, "version 1 event numbers are fixed");
static_assert(DM_ALL == 0x7ffff, "DM_ALL selects types 0..18");
static_assert(DS_CLOSING == 1u << 8 && DF_FIXEDSIZE == 1u << 14, "version 1 bits are fixed");

/* version 2 records */
SIZE(Deskconfig, 80);
OFF(Deskconfig, seq, 0); OFF(Deskconfig, state, 4); OFF(Deskconfig, dx, 8); OFF(Deskconfig, dy, 12);
OFF(Deskconfig, pdx, 16); OFF(Deskconfig, pdy, 20); OFF(Deskconfig, scalenum, 24); OFF(Deskconfig, scaleden, 28);
OFF(Deskconfig, bdx, 32); OFF(Deskconfig, bdy, 36); OFF(Deskconfig, stride, 40); OFF(Deskconfig, chan, 44);
OFF(Deskconfig, visibility, 48); OFF(Deskconfig, bufmode, 52); OFF(Deskconfig, refresh, 56);
OFF(Deskconfig, output, 60); OFF(Deskconfig, latency, 64); OFF(Deskconfig, reserved, 68);

SIZE(Deskexthead, 32);
OFF(Deskexthead, type, 0); OFF(Deskexthead, win, 4); OFF(Deskexthead, msec, 8); OFF(Deskexthead, seq, 12);
OFF(Deskexthead, size, 16); OFF(Deskexthead, bodylen, 20); OFF(Deskexthead, datalen, 22); OFF(Deskexthead, nsec, 24);
static_assert(alignof(Deskexthead) == 8, "Deskexthead is 8-byte aligned");

SIZE(Deskwords, 16);
OFF(Deskwords, a, 0); OFF(Deskwords, b, 4); OFF(Deskwords, c, 8); OFF(Deskwords, d, 12);

SIZE(Deskpointer, 48);
OFF(Deskpointer, v1, 0); OFF(Deskpointer, fx, 16); OFF(Deskpointer, fy, 20); OFF(Deskpointer, flags, 24);
OFF(Deskpointer, tool, 28); OFF(Deskpointer, tooltype, 32); OFF(Deskpointer, pressure, 34);
OFF(Deskpointer, tiltx, 36); OFF(Deskpointer, tilty, 38); OFF(Deskpointer, rotation, 40);
OFF(Deskpointer, distance, 42); OFF(Deskpointer, reserved, 44);

SIZE(Deskkey, 32);
OFF(Deskkey, v1, 0); OFF(Deskkey, keysym, 16); OFF(Deskkey, base, 20); OFF(Deskkey, layout, 24); OFF(Deskkey, reserved, 28);

SIZE(Deskframe, 64);
OFF(Deskframe, v1, 0); OFF(Deskframe, frame, 16); OFF(Deskframe, target, 24); OFF(Deskframe, presented, 32);
OFF(Deskframe, refresh, 40); OFF(Deskframe, present, 48); OFF(Deskframe, config, 52);
OFF(Deskframe, flags, 56); OFF(Deskframe, missed, 60);

SIZE(Deskack, 16);
OFF(Deskack, tag, 0); OFF(Deskack, error, 4); OFF(Deskack, descseq, 8); OFF(Deskack, configseq, 12);

SIZE(Desktext, 8);
OFF(Desktext, imeseq, 0); OFF(Desktext, flags, 4);

SIZE(Deskpreedit, 24);
OFF(Deskpreedit, imeseq, 0); OFF(Deskpreedit, textlen, 4); OFF(Deskpreedit, cursor0, 8);
OFF(Deskpreedit, cursor1, 12); OFF(Deskpreedit, nstyle, 16); OFF(Deskpreedit, reserved, 20);
SIZE(Deskpestyle, 12);
OFF(Deskpestyle, start, 0); OFF(Deskpestyle, end, 4); OFF(Deskpestyle, style, 8);

SIZE(Deskdelsurround, 16);
OFF(Deskdelsurround, imeseq, 0); OFF(Deskdelsurround, before, 4); OFF(Deskdelsurround, after, 8);

SIZE(Deskkeymap, 16);
OFF(Deskkeymap, gen, 0); OFF(Deskkeymap, layout, 4); OFF(Deskkeymap, delay, 8); OFF(Deskkeymap, interval, 12);

SIZE(Deskscroll, 24);
OFF(Deskscroll, dx, 0); OFF(Deskscroll, dy, 4); OFF(Deskscroll, v120x, 8); OFF(Deskscroll, v120y, 12);
OFF(Deskscroll, flags, 16); OFF(Deskscroll, reserved, 20);

SIZE(Deskmotion, 16);
OFF(Deskmotion, dx, 0); OFF(Deskmotion, dy, 4); OFF(Deskmotion, adx, 8); OFF(Deskmotion, ady, 12);

SIZE(Deskpointerstate, 8);
OFF(Deskpointerstate, state, 0); OFF(Deskpointerstate, reason, 4);

SIZE(Deskproximity, 24);
OFF(Deskproximity, in, 0); OFF(Deskproximity, tool, 4); OFF(Deskproximity, tooltype, 8);
OFF(Deskproximity, caps, 12); OFF(Deskproximity, hwserial, 16);

SIZE(Deskpopupdone, 8);
OFF(Deskpopupdone, reason, 0);

SIZE(Deskoutput, 48);
OFF(Deskoutput, id, 0); OFF(Deskoutput, change, 4); OFF(Deskoutput, scalenum, 8); OFF(Deskoutput, scaleden, 12);
OFF(Deskoutput, refresh, 16); OFF(Deskoutput, flags, 20); OFF(Deskoutput, r, 24);
OFF(Deskoutput, pdx, 40); OFF(Deskoutput, pdy, 44);

SIZE(Deskgamepad, 16);
OFF(Deskgamepad, pad, 0); OFF(Deskgamepad, connected, 4); OFF(Deskgamepad, caps, 8); OFF(Deskgamepad, gen, 12);

SIZE(Deskpadbutton, 16);
OFF(Deskpadbutton, button, 0); OFF(Deskpadbutton, down, 4); OFF(Deskpadbutton, buttons, 8);
SIZE(Deskpadaxis, 16);
OFF(Deskpadaxis, axis, 0); OFF(Deskpadaxis, value, 4);
SIZE(Deskpadsensor, 16);
OFF(Deskpadsensor, sensor, 0); OFF(Deskpadsensor, x, 4); OFF(Deskpadsensor, y, 8); OFF(Deskpadsensor, z, 12);

SIZE(Deskpadstate, 32);
OFF(Deskpadstate, seq, 0); OFF(Deskpadstate, buttons, 4); OFF(Deskpadstate, axes, 8);
OFF(Deskpadstate, reserved, 20); OFF(Deskpadstate, nsec, 24);

/* bodies fit a record, and a record fits a read */
static_assert(DESK_EXTHEADSZ + sizeof(Deskframe) == 96, "a frame record is 96 bytes");
static_assert(DESK_EXTHEADSZ + sizeof(Deskconfig) <= DESK_EXTMAX, "a configure record fits");
static_assert(DESK_EXTMAX % DESK_EXTALIGN == 0, "the largest record is aligned");
static_assert(DESK_MASKWORDS == 8, "the mask is 256 bits");

/* ---------------------------------------------------------------------
 * Round trips, run by the test target.
 */
static int nfail;

static void
check(bool ok, const char *what)
{
	if(!ok){
		nfail++;
		/* freestanding: no stdio; the exit status carries the count */
		(void)what;
	}
}

static bool
sameevent(const Deskevent *x, const Deskevent *y)
{
	return x->type == y->type && x->win == y->win && x->msec == y->msec && x->seq == y->seq
		&& x->a == y->a && x->b == y->b && x->c == y->c && x->d == y->d;
}

int
main(void)
{
	uint8_t buf[DESK_EXTMAX];

	/* a version 1 record is little-endian on the wire */
	{
		Deskevent e = { DE_KEY, 3, 1000, 41, 97, 4, DK_SHIFT, DKF_DOWN }, f;
		desk_packevent(buf, &e);
		check(buf[0] == DE_KEY && buf[1] == 0 && buf[16] == 97, "event is little-endian");
		desk_unpackevent(buf, &f);
		check(sameevent(&e, &f), "event round trip");
		check(desk_reclen(buf, DESK_EVENTSZ) == DESK_EVENTSZ, "a plain record is 32 bytes");
		check(desk_reclen(buf, DESK_EVENTSZ - 1) == 0, "a short plain record is refused");
	}
	/* the descriptor */
	{
		Deskwin w = { .id = 3, .r = { 100, 100, 740, 580 }, .chan = DESK_XRGB32, .stride = 2560, .seq = 41 }, v;
		desk_packwin(buf, &w);
		check(desk_get32(buf + 16) == 100 && desk_get32(buf + 76) == 2560, "desc offsets");
		desk_unpackwin(buf, &v);
		check(v.id == 3 && v.r.x1 == 740 && v.stride == 2560 && v.seq == 41, "desc round trip");
		check(desk_pixeloffset(&w, 2, 1) == 2560 + 8, "pixel offset");
	}
	/* the configuration */
	{
		Deskconfig c = { .seq = 7, .dx = 640, .dy = 480, .pdx = 960, .pdy = 720, .scalenum = 180,
			.scaleden = DESK_SCALEDEN, .bdx = 960, .bdy = 720, .stride = 3840, .chan = DESK_XRGB32,
			.visibility = DESK_VIS_SHOWN, .bufmode = DESK_BUF_DEVICE, .refresh = 16666667 }, d;
		desk_packconfig(buf, &c);
		check(desk_get32(buf + 24) == 180 && desk_get32(buf + 56) == 16666667, "config offsets");
		desk_unpackconfig(buf, &d);
		check(d.seq == 7 && d.pdx == 960 && d.bufmode == DESK_BUF_DEVICE && d.refresh == 16666667, "config round trip");
		check(desk_bufoffset(&c, 1, 2) == 2 * 3840 + 4, "buffer offset");
	}
	/* an extended record with variable data, and walking two records */
	{
		Deskexthead h = { DE_TEXT | DESK_EXT, 3, 1000, 42, desk_extsize(sizeof(Desktext), 5),
			(uint16_t)sizeof(Desktext), 5, UINT64_C(0x0123456789abcdef) }, g;
		check(h.size == 48, "text record is 48 bytes");
		for(size_t i = 0; i < sizeof buf; i++)
			buf[i] = 0;
		desk_packhead(buf, &h);
		desk_put32(buf + 32, 9);                   /* Desktext.imeseq */
		desk_put32(buf + 36, 0);                   /* Desktext.flags */
		for(int i = 0; i < 5; i++)
			buf[40 + i] = (uint8_t)"hello"[i];
		desk_unpackhead(buf, &g);
		check(g.type == (DE_TEXT | DESK_EXT) && g.size == 48 && g.bodylen == 8 && g.datalen == 5
			&& g.nsec == UINT64_C(0x0123456789abcdef), "head round trip");
		check(desk_get64(buf + 24) == UINT64_C(0x0123456789abcdef) && buf[24] == 0xef, "nsec is little-endian");
		/* a second record: DE_FRAME with its body */
		Deskexthead fh = { DE_FRAME | DESK_EXT, 3, 1001, 43, desk_extsize(sizeof(Deskframe), 0),
			(uint16_t)sizeof(Deskframe), 0, 5 };
		desk_packhead(buf + 48, &fh);
		check(desk_reclen(buf, 48 + 96) == 48, "first record length");
		check(desk_reclen(buf + 48, 96) == 96, "second record length");
		check(desk_reclen(buf + 48, 95) == 0, "a record cut by the reply is refused");
		desk_put32(buf + 48 + 16, 88);               /* size disagrees with bodylen and datalen */
		check(desk_reclen(buf + 48, 96) == 0, "an inconsistent size is refused");
	}
	/* the wide mask and the classes */
	{
		Deskmaskset m = { { DM_DEFAULT } };
		check(desk_maskhas(&m, DE_KEY) && !desk_maskhas(&m, DE_MOVE), "word 0 is the version 1 mask");
		check(!desk_maskhas(&m, DE_CONFIGURE), "a version 1 mask selects no version 2 type");
		desk_maskadd(&m, DE_CONFIGURE);
		desk_maskadd(&m, DE_PADSENSOR);
		check(desk_maskhas(&m, DE_CONFIGURE) && m.w[1] == (1u << 0 | 1u << 15), "types above 31 use later words");
		desk_maskdel(&m, DE_CONFIGURE);
		check(!desk_maskhas(&m, DE_CONFIGURE), "mask delete");
		check(!desk_maskhas(&m, 300), "types beyond the mask are never selected");
		for(uint32_t t = DE_MAX; t < 32; t++)
			check(desk_typeclass(t) == DC_NONE, "types 19..31 are never assigned");
		for(uint32_t t = 1; t < DE_MAX; t++)
			check(desk_typeclass(t) != DC_NONE, "every version 1 type has a class");
		for(uint32_t t = DE_CONFIGURE; t < DE_V2MAX; t++)
			check(desk_typeclass(t) != DC_NONE, "every version 2 type has a class");
	}
	return nfail == 0 ? 0 : 1;
}
