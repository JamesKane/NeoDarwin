// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: checks that desktop.h, a C ABI header, also compiles as C++ for C++ clients (SDL, Qt, Dawn, engines)
/*
 * desktop_cxx.cpp - C++ compile check for docs/desktop/desktop.h
 * (WP-T-152 in window-protocol.md). The header must compile as C++20
 * with no warnings and give the same layouts as in C; every inline
 * helper is instantiated once so C++ type rules are applied to its body.
 */
#include "docs/desktop/desktop.h"

static_assert(sizeof(Deskevent) == DESK_EVENTSZ, "Deskevent is 32 bytes in C++ too");
static_assert(sizeof(Deskwin) == DESK_DESCSZ, "Deskwin is 96 bytes in C++ too");
static_assert(sizeof(Deskconfig) == DESK_CONFIGSZ, "Deskconfig is 80 bytes in C++ too");
static_assert(sizeof(Deskexthead) == DESK_EXTHEADSZ, "Deskexthead is 32 bytes in C++ too");
static_assert(offsetof(Deskexthead, nsec) == 24, "Deskexthead.nsec at 24 in C++ too");
static_assert(offsetof(Deskframe, presented) == 32, "Deskframe.presented at 32 in C++ too");
static_assert(offsetof(Deskpointer, pressure) == 34, "Deskpointer.pressure at 34 in C++ too");
static_assert(sizeof(Desketype) == 4 && sizeof(Desktool) == 2, "enum underlying types hold in C++");

extern "C++" bool desk_cxx_check(uint8_t *buf);

bool desk_cxx_check(uint8_t *buf)
{
	Deskevent e = { DE_KEY, 1, 2, 3, 97, 4, 0, DKF_DOWN }, f = {};
	desk_packevent(buf, &e);
	desk_unpackevent(buf, &f);

	Deskwin w = {}, v = {};
	desk_packwin(buf, &w);
	desk_unpackwin(buf, &v);

	Deskconfig c = {}, d = {};
	desk_packconfig(buf, &c);
	desk_unpackconfig(buf, &d);

	Deskexthead h = {}, g = {};
	h.type = DE_FRAME | DESK_EXT;
	h.bodylen = sizeof(Deskframe);
	h.size = desk_extsize(h.bodylen, 0);
	desk_packhead(buf, &h);
	desk_unpackhead(buf, &g);

	Deskmaskset m = {};
	desk_maskadd(&m, DE_CONFIGURE);
	desk_maskdel(&m, DE_MOVE);

	Deskpoint p = { 1, 1 };
	Deskrect r = desk_eventrect(&e);
	return f.a == 97 && g.size == desk_reclen(buf, DESK_EXTMAX) && desk_maskhas(&m, DE_CONFIGURE)
		&& desk_typeclass(DE_TEXT) == DC_TEXT && desk_ptinrect(p, r) && desk_dx(r) >= 0 && desk_dy(r) >= 0
		&& desk_chandepth(DESK_XRGB32) == 32 && desk_pixeloffset(&v, 0, 0) == 0 && desk_bufoffset(&d, 0, 0) == 0
		&& desk_fixint(DESK_FIX_ONE) == 1 && desk_get64(buf + 24) == 0;
}
