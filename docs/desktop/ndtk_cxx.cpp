// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: checks that ndtk.h, a C ABI header, also compiles as C++ for C++ clients (SDL, Qt, Dawn, engines)
/*
 * ndtk_cxx.cpp - C++20 compile check for docs/desktop/ndtk.h (TK-T-002 in
 * toolkit-api.md). The header must compile as C++20 with no warnings and
 * give the same layouts as in C; the forward-declared audio and WebGPU
 * types must be usable as the incomplete types they are.
 */
#include "docs/desktop/ndtk.h"

static_assert(sizeof(ndtk_event) == 96, "ndtk_event is 96 bytes in C++ too");
static_assert(offsetof(ndtk_event, u) == 40, "payload at 40 in C++ too");
static_assert(offsetof(ndtk_event, time_ns) == 16, "time_ns at 16 in C++ too");
static_assert(offsetof(ndtk_frame_event, presented_ns) == 16, "presented_ns at 16 in C++ too");
static_assert(offsetof(ndtk_configure_event, output) == 48, "configure output at 48 in C++ too");
static_assert(offsetof(ndtk_gpu_context, usage) == 56, "gpu usage at 56 in C++ too");
static_assert(sizeof(ndtk_cpu_surface) == 48, "ndtk_cpu_surface is 48 bytes in C++ too");
static_assert(sizeof(enum ndtk_event_kind) == 2 && sizeof(enum ndtk_cap) == 8 && sizeof(enum ndtk_kind) == 1,
              "enum underlying types hold in C++");

namespace {

enum ndtk_app_result init_cb(void **state, ndtk_loop, int, char **) { *state = nullptr; return NDTK_APP_CONTINUE; }
enum ndtk_app_result iterate_cb(void *, ndtk_loop) { return NDTK_APP_SUCCESS; }
enum ndtk_app_result event_cb(void *, ndtk_loop, const ndtk_event *e) {
	return e->kind == NDTK_EV_QUIT ? NDTK_APP_SUCCESS : NDTK_APP_CONTINUE;
}
void quit_cb(void *, enum ndtk_app_result) {}
void render_cb(void *, const struct AUrender_info *, void *) {}

}  // namespace

extern "C++" bool ndtk_cxx_check(ndtk_event *e, ndtk_gpu_context *g);

bool ndtk_cxx_check(ndtk_event *e, ndtk_gpu_context *g)
{
	ndtk_app app = {};
	app.size = sizeof app;
	app.init = init_cb;
	app.iterate = iterate_cb;
	app.event = event_cb;
	app.quit = quit_cb;

	ndtk_render_fn render = render_cb;
	struct WGPUDeviceImpl *device = g->device;
	ndtk_request r = {};
	r.size = sizeof r;
	r.op = NDTK_REQ_TITLE;

	e->kind = NDTK_EV_KEY_DOWN;
	e->u.key.flags = NDTK_KEY_DOWN;
	e->u.key.mods = NDTK_MOD_SHIFT | NDTK_MOD_RSHIFT;
	ndtk_handle h = NDTK_HANDLE_MAKE(NDTK_KIND_WINDOW, 1, 2);
	return app.event(nullptr, 0, e) == NDTK_APP_CONTINUE && render != nullptr && device == g->device
		&& NDTK_HANDLE_KIND(h) == NDTK_KIND_WINDOW && NDTK_HANDLE_INDEX(h) == 2 && r.op == NDTK_REQ_TITLE;
}
