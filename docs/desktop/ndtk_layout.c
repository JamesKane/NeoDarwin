// SPDX-License-Identifier: BSD-2-Clause
// NeoDarwin-Language: portability: compile-checks the C ABI records of ndtk.h (spec-conventions.md §5)
/*
 * ndtk_layout.c - layout check for docs/desktop/ndtk.h (toolkit API,
 * version 1): the size of every record and the offset of every field that
 * crosses the ABI, the fixed width of every enum, and the constants the
 * text says are equal to the lower layers' (desktop.h, nd_sched.h). main()
 * round-trips the handle accessors. Conformance test TK-T-001 in
 * toolkit-api.md.
 */
#include "docs/desktop/ndtk.h"
#include "docs/desktop/desktop.h"
#include "docs/kernel/nd_sched.h"

#define OFF(T, f, n) static_assert(offsetof(T, f) == (n), #T "." #f " must be at offset " #n)
#define SIZE(T, n)   static_assert(sizeof(T) == (n), #T " must be " #n " bytes")
#define SAME(a, b)   static_assert((uint64_t)(a) == (uint64_t)(b), #a " must equal " #b)

static_assert(NDTK_API_VERSION == 1u, "API version 1");

/* enums have the declared fixed width */
static_assert(sizeof(enum ndtk_error) == 4, "ndtk_error is 32-bit");
static_assert(sizeof(enum ndtk_cap) == 8, "ndtk_cap is 64-bit");
static_assert(sizeof(enum ndtk_kind) == 1, "ndtk_kind is 8-bit");
static_assert(sizeof(enum ndtk_event_kind) == 2, "ndtk_event_kind is 16-bit");
static_assert(sizeof(enum ndtk_intent) == 4, "ndtk_intent is 32-bit");
static_assert(sizeof(enum ndtk_tool) == 2, "ndtk_tool is 16-bit");
static_assert(sizeof(enum ndtk_app_result) == 4, "ndtk_app_result is 32-bit");
static_assert(sizeof(ndtk_handle) == 8, "handles are u64");

/* ---- the lower layers' values, repeated by name (TK-EV-006, TK-TIME-003) ---- */
SAME(NDTK_DEADLINE_POLL, SC_DEADLINE_POLL);
SAME(NDTK_DEADLINE_NONE, SC_DEADLINE_NONE);
SAME(NDTK_INTENT_NONE, SC_INTENT_NONE);
SAME(NDTK_INTENT_INTERACTIVE, SC_INTENT_INTERACTIVE);
SAME(NDTK_INTENT_THROUGHPUT, SC_INTENT_THROUGHPUT);
SAME(NDTK_INTENT_BACKGROUND, SC_INTENT_BACKGROUND);
SAME(NDTK_INTENT_AUDIO, SC_INTENT_AUDIO);

SAME(NDTK_MOD_SHIFT, DK_SHIFT); SAME(NDTK_MOD_CTRL, DK_CTRL); SAME(NDTK_MOD_ALT, DK_ALT);
SAME(NDTK_MOD_META, DK_META); SAME(NDTK_MOD_CAPS, DK_CAPS); SAME(NDTK_MOD_NUM, DK_NUM);
SAME(NDTK_MOD_RSHIFT, DK_RSHIFT); SAME(NDTK_MOD_RCTRL, DK_RCTRL); SAME(NDTK_MOD_RALT, DK_RALT);
SAME(NDTK_MOD_RMETA, DK_RMETA); SAME(NDTK_MOD_LEVEL3, DK_LEVEL3);

SAME(NDTK_KEY_DOWN, DKF_DOWN); SAME(NDTK_KEY_REPEAT, DKF_REPEAT); SAME(NDTK_KEY_NORUNE, DKF_NORUNE);
SAME(NDTK_KEY_IMEPASS, DKF_IMEPASS); SAME(NDTK_KEY_BOUND, DKF_BOUND); SAME(NDTK_KEY_CANCEL, DKF_CANCEL);

SAME(NDTK_BUTTON_LEFT, DB_LEFT); SAME(NDTK_BUTTON_MIDDLE, DB_MIDDLE); SAME(NDTK_BUTTON_RIGHT, DB_RIGHT);
SAME(NDTK_BUTTON_4, DB_BUTTON4); SAME(NDTK_BUTTON_5, DB_BUTTON5);

SAME(NDTK_POINTER_PEN, DMF_PEN); SAME(NDTK_POINTER_WARPED, DMF_WARPED); SAME(NDTK_POINTER_HOVER, DMF_HOVER);
SAME(NDTK_TOOL_MOUSE, DESK_TOOL_MOUSE); SAME(NDTK_TOOL_PEN, DESK_TOOL_PEN); SAME(NDTK_TOOL_ERASER, DESK_TOOL_ERASER);
SAME(NDTK_TOOL_BRUSH, DESK_TOOL_BRUSH); SAME(NDTK_TOOL_PENCIL, DESK_TOOL_PENCIL);
SAME(NDTK_TOOL_AIRBRUSH, DESK_TOOL_AIRBRUSH); SAME(NDTK_TOOL_LENS, DESK_TOOL_LENS);

SAME(NDTK_SCROLL_PRECISE, DSF_PRECISE); SAME(NDTK_SCROLL_INVERTED, DSF_INVERTED); SAME(NDTK_SCROLL_STOP, DSF_STOP);
SAME(NDTK_TEXT_KEY, DTF_KEY);

SAME(NDTK_FRAME_HWTIME, DFF_HWTIME); SAME(NDTK_FRAME_COMPOSITED, DFF_COMPOSITED);
SAME(NDTK_FRAME_THROTTLED, DFF_THROTTLED); SAME(NDTK_FRAME_REQUESTED, DFF_REQUESTED);
SAME(NDTK_FRAME_ZEROCOPY, DFF_ZEROCOPY); SAME(NDTK_FRAME_LATE, DFF_LATE);
static_assert((NDTK_FRAME_ESTIMATED & 0xffffu) == 0, "the host-only bit is outside the DFF_ space");

SAME(NDTK_STATE_VISIBLE, DS_VISIBLE); SAME(NDTK_STATE_FOCUSED, DS_FOCUSED); SAME(NDTK_STATE_HIDDEN, DS_HIDDEN);
SAME(NDTK_STATE_ZOOMED, DS_ZOOMED); SAME(NDTK_STATE_GRABBED, DS_GRABBED); SAME(NDTK_STATE_LAGGING, DS_LAGGING);
SAME(NDTK_STATE_SNAPPED, DS_SNAPPED); SAME(NDTK_STATE_FULL, DS_FULL); SAME(NDTK_STATE_CLOSING, DS_CLOSING);
SAME(NDTK_STATE_INTERACTIVE, DS_INTERACTIVE); SAME(NDTK_STATE_OCCLUDED, DS_OCCLUDED);

SAME(NDTK_VIS_UNMAPPED, DESK_VIS_UNMAPPED); SAME(NDTK_VIS_SHOWN, DESK_VIS_SHOWN);
SAME(NDTK_VIS_OCCLUDED, DESK_VIS_OCCLUDED); SAME(NDTK_VIS_OFFSCREEN, DESK_VIS_OFFSCREEN);
SAME(NDTK_VIS_HIDDEN, DESK_VIS_HIDDEN);
SAME(NDTK_BUFFER_LOGICAL, DESK_BUF_LOGICAL); SAME(NDTK_BUFFER_DEVICE, DESK_BUF_DEVICE);
SAME(NDTK_BUFFER_FIXED, DESK_BUF_FIXED);

SAME(NDTK_PAD_A, DG_A); SAME(NDTK_PAD_B, DG_B); SAME(NDTK_PAD_X, DG_X); SAME(NDTK_PAD_Y, DG_Y);
SAME(NDTK_PAD_BACK, DG_BACK); SAME(NDTK_PAD_GUIDE, DG_GUIDE); SAME(NDTK_PAD_START, DG_START);
SAME(NDTK_PAD_LSTICK, DG_LSTICK); SAME(NDTK_PAD_RSTICK, DG_RSTICK);
SAME(NDTK_PAD_LSHOULDER, DG_LSHOULDER); SAME(NDTK_PAD_RSHOULDER, DG_RSHOULDER);
SAME(NDTK_PAD_UP, DG_UP); SAME(NDTK_PAD_DOWN, DG_DOWN); SAME(NDTK_PAD_LEFT, DG_LEFT); SAME(NDTK_PAD_RIGHT, DG_RIGHT);
SAME(NDTK_PAD_LEFTX, DG_LEFTX); SAME(NDTK_PAD_LEFTY, DG_LEFTY); SAME(NDTK_PAD_RIGHTX, DG_RIGHTX);
SAME(NDTK_PAD_RIGHTY, DG_RIGHTY); SAME(NDTK_PAD_LTRIGGER, DG_LTRIGGER); SAME(NDTK_PAD_RTRIGGER, DG_RTRIGGER);
SAME(NDTK_PAD_NAXES, DG_NAXES);

/* ---- events ---- */
SIZE(ndtk_event, 96);
OFF(ndtk_event, kind, 0); OFF(ndtk_event, flags, 2); OFF(ndtk_event, seq, 4); OFF(ndtk_event, window, 8);
OFF(ndtk_event, time_ns, 16); OFF(ndtk_event, data, 24); OFF(ndtk_event, data_len, 32);
OFF(ndtk_event, reserved, 36); OFF(ndtk_event, u, 40);
static_assert(sizeof(((ndtk_event *)0)->u) == 56, "the payload union is 56 bytes");
static_assert(NDTK_EV_KIND_MAX == 41, "40 event kinds in version 1");

SIZE(ndtk_key_event, 32);
OFF(ndtk_key_event, scancode, 0); OFF(ndtk_key_event, keysym, 4); OFF(ndtk_key_event, base, 8);
OFF(ndtk_key_event, rune, 12); OFF(ndtk_key_event, mods, 16); OFF(ndtk_key_event, flags, 20);
OFF(ndtk_key_event, layout, 24); OFF(ndtk_key_event, reserved, 28);

SIZE(ndtk_text_event, 32);
OFF(ndtk_text_event, ime_serial, 0); OFF(ndtk_text_event, flags, 4); OFF(ndtk_text_event, cursor_begin, 8);
OFF(ndtk_text_event, cursor_end, 12); OFF(ndtk_text_event, text_len, 16); OFF(ndtk_text_event, style_count, 20);
OFF(ndtk_text_event, reserved, 24);
SIZE(ndtk_preedit_style, 12);
SIZE(ndtk_delete_surrounding_event, 16);
OFF(ndtk_delete_surrounding_event, before, 4); OFF(ndtk_delete_surrounding_event, after, 8);

SIZE(ndtk_pointer_event, 56);
OFF(ndtk_pointer_event, x, 0); OFF(ndtk_pointer_event, y, 4); OFF(ndtk_pointer_event, buttons, 8);
OFF(ndtk_pointer_event, button, 12); OFF(ndtk_pointer_event, mods, 16); OFF(ndtk_pointer_event, flags, 20);
OFF(ndtk_pointer_event, tool, 24); OFF(ndtk_pointer_event, reserved0, 26); OFF(ndtk_pointer_event, pressure, 28);
OFF(ndtk_pointer_event, tilt_x, 32); OFF(ndtk_pointer_event, tilt_y, 36); OFF(ndtk_pointer_event, rotation, 40);
OFF(ndtk_pointer_event, distance, 44); OFF(ndtk_pointer_event, tool_serial, 48); OFF(ndtk_pointer_event, reserved1, 52);

SIZE(ndtk_scroll_event, 24);
OFF(ndtk_scroll_event, dx, 0); OFF(ndtk_scroll_event, dy, 4); OFF(ndtk_scroll_event, v120_x, 8);
OFF(ndtk_scroll_event, v120_y, 12); OFF(ndtk_scroll_event, flags, 16); OFF(ndtk_scroll_event, mods, 20);
SIZE(ndtk_relative_event, 16);
OFF(ndtk_relative_event, accel_dx, 8); OFF(ndtk_relative_event, accel_dy, 12);
SIZE(ndtk_constraint_event, 8);
SIZE(ndtk_proximity_event, 24);
OFF(ndtk_proximity_event, tool, 8); OFF(ndtk_proximity_event, caps, 12); OFF(ndtk_proximity_event, hw_serial, 16);

SIZE(ndtk_configure_event, 56);
OFF(ndtk_configure_event, width, 0); OFF(ndtk_configure_event, height, 4);
OFF(ndtk_configure_event, pixel_width, 8); OFF(ndtk_configure_event, pixel_height, 12);
OFF(ndtk_configure_event, scale_num, 16); OFF(ndtk_configure_event, scale_den, 20);
OFF(ndtk_configure_event, buffer_width, 24); OFF(ndtk_configure_event, buffer_height, 28);
OFF(ndtk_configure_event, config_seq, 32); OFF(ndtk_configure_event, state, 36);
OFF(ndtk_configure_event, visibility, 40); OFF(ndtk_configure_event, refresh_ns, 44);
OFF(ndtk_configure_event, output, 48);

SIZE(ndtk_frame_event, 56);
OFF(ndtk_frame_event, frame, 0); OFF(ndtk_frame_event, target_ns, 8); OFF(ndtk_frame_event, presented_ns, 16);
OFF(ndtk_frame_event, refresh_ns, 24); OFF(ndtk_frame_event, present, 32); OFF(ndtk_frame_event, config_seq, 36);
OFF(ndtk_frame_event, flags, 40); OFF(ndtk_frame_event, missed, 44); OFF(ndtk_frame_event, reserved, 48);

SIZE(ndtk_ack_event, 16);
OFF(ndtk_ack_event, tag, 0); OFF(ndtk_ack_event, error, 4); OFF(ndtk_ack_event, desc_seq, 8);
OFF(ndtk_ack_event, config_seq, 12);
SIZE(ndtk_focus_event, 8); SIZE(ndtk_state_event, 8); SIZE(ndtk_move_event, 8); SIZE(ndtk_close_event, 8);
SIZE(ndtk_menu_event, 16); SIZE(ndtk_drop_event, 16); SIZE(ndtk_popup_done_event, 8);
SIZE(ndtk_expose_event, 16); SIZE(ndtk_keymap_event, 16); SIZE(ndtk_theme_event, 8);
OFF(ndtk_menu_event, item, 4); OFF(ndtk_menu_event, checked, 8);
OFF(ndtk_drop_event, count, 8);
OFF(ndtk_keymap_event, delay_ms, 8); OFF(ndtk_keymap_event, interval_us, 12);

SIZE(ndtk_gamepad_event, 40);
OFF(ndtk_gamepad_event, pad, 0); OFF(ndtk_gamepad_event, change, 8); OFF(ndtk_gamepad_event, index, 12);
OFF(ndtk_gamepad_event, value, 16); OFF(ndtk_gamepad_event, buttons, 20); OFF(ndtk_gamepad_event, x, 24);
OFF(ndtk_gamepad_event, y, 28); OFF(ndtk_gamepad_event, z, 32); OFF(ndtk_gamepad_event, caps, 36);

SIZE(ndtk_output_event, 48);
OFF(ndtk_output_event, output, 0); OFF(ndtk_output_event, change, 8); OFF(ndtk_output_event, flags, 12);
OFF(ndtk_output_event, scale_num, 16); OFF(ndtk_output_event, scale_den, 20);
OFF(ndtk_output_event, refresh_ns, 24); OFF(ndtk_output_event, rect, 32);

SIZE(ndtk_timer_event, 32);
OFF(ndtk_timer_event, timer, 0); OFF(ndtk_timer_event, deadline_ns, 8); OFF(ndtk_timer_event, fired_ns, 16);
OFF(ndtk_timer_event, expirations, 24);
SIZE(ndtk_fd_event, 24);
OFF(ndtk_fd_event, source, 0); OFF(ndtk_fd_event, fd, 8); OFF(ndtk_fd_event, ready, 12); OFF(ndtk_fd_event, data, 16);
SIZE(ndtk_message, 16);
SIZE(ndtk_audio_event, 32);
SIZE(ndtk_io_event, 32);
OFF(ndtk_io_event, request, 0); OFF(ndtk_io_event, udata, 8); OFF(ndtk_io_event, bytes, 16);
OFF(ndtk_io_event, error, 24); OFF(ndtk_io_event, op, 28);
SIZE(ndtk_dialog_event, 16);
OFF(ndtk_dialog_event, result, 8); OFF(ndtk_dialog_event, count, 12);
SIZE(ndtk_agent_event, 24);
OFF(ndtk_agent_event, request, 0); OFF(ndtk_agent_event, verb, 4); OFF(ndtk_agent_event, node, 8);
OFF(ndtk_agent_event, value, 16);
SIZE(ndtk_path_event, 16);
OFF(ndtk_path_event, what, 8);
SIZE(ndtk_gpu_event, 16);
OFF(ndtk_gpu_event, gpu, 0); OFF(ndtk_gpu_event, reason, 8);

/* ---- calls' records ---- */
SIZE(ndtk_rect, 16); SIZE(ndtk_irect, 16);
SIZE(ndtk_allocator, 32);
OFF(ndtk_allocator, alloc, 8); OFF(ndtk_allocator, free, 16); OFF(ndtk_allocator, ctx, 24);

SIZE(ndtk_loop_desc, 40);
OFF(ndtk_loop_desc, size, 0); OFF(ndtk_loop_desc, flags, 4); OFF(ndtk_loop_desc, event_capacity, 8);
OFF(ndtk_loop_desc, arena_bytes, 12); OFF(ndtk_loop_desc, post_capacity, 16); OFF(ndtk_loop_desc, intent, 20);
OFF(ndtk_loop_desc, reserved, 24);

SIZE(ndtk_app, 48);
OFF(ndtk_app, init, 8); OFF(ndtk_app, iterate, 16); OFF(ndtk_app, event, 24); OFF(ndtk_app, quit, 32);
OFF(ndtk_app, loop, 40);

SIZE(ndtk_window_desc, 64);
OFF(ndtk_window_desc, size, 0); OFF(ndtk_window_desc, kind, 4); OFF(ndtk_window_desc, title, 8);
OFF(ndtk_window_desc, width, 16); OFF(ndtk_window_desc, height, 20); OFF(ndtk_window_desc, flags, 24);
OFF(ndtk_window_desc, reserved0, 28); OFF(ndtk_window_desc, parent, 32); OFF(ndtk_window_desc, min_width, 40);
OFF(ndtk_window_desc, min_height, 44); OFF(ndtk_window_desc, max_width, 48); OFF(ndtk_window_desc, max_height, 52);
OFF(ndtk_window_desc, reserved, 56);

SIZE(ndtk_request, 64);
OFF(ndtk_request, size, 0); OFF(ndtk_request, op, 4); OFF(ndtk_request, value, 8); OFF(ndtk_request, value2, 12);
OFF(ndtk_request, rect, 16); OFF(ndtk_request, window, 32); OFF(ndtk_request, text, 40);
OFF(ndtk_request, text_len, 48); OFF(ndtk_request, flags, 52); OFF(ndtk_request, reserved, 56);

SIZE(ndtk_output_info, 136);
OFF(ndtk_output_info, flags, 4); OFF(ndtk_output_info, output, 8); OFF(ndtk_output_info, scale_num, 16);
OFF(ndtk_output_info, scale_den, 20); OFF(ndtk_output_info, refresh_ns, 24); OFF(ndtk_output_info, rect, 32);
OFF(ndtk_output_info, pixel_width, 48); OFF(ndtk_output_info, pixel_height, 52); OFF(ndtk_output_info, name, 56);
OFF(ndtk_output_info, reserved, 120);

SIZE(ndtk_dialog_desc, 56);
OFF(ndtk_dialog_desc, kind, 4); OFF(ndtk_dialog_desc, parent, 8); OFF(ndtk_dialog_desc, title, 16);
OFF(ndtk_dialog_desc, path, 24); OFF(ndtk_dialog_desc, filter, 32); OFF(ndtk_dialog_desc, flags, 40);

SIZE(ndtk_text_input, 48);
OFF(ndtk_text_input, enabled, 4); OFF(ndtk_text_input, purpose, 8); OFF(ndtk_text_input, cursor, 12);
OFF(ndtk_text_input, anchor, 16); OFF(ndtk_text_input, surrounding_len, 20);
OFF(ndtk_text_input, surrounding, 24); OFF(ndtk_text_input, caret, 32);

SIZE(ndtk_gamepad_state, 56);
OFF(ndtk_gamepad_state, buttons, 4); OFF(ndtk_gamepad_state, axes, 8); OFF(ndtk_gamepad_state, time_ns, 40);
OFF(ndtk_gamepad_state, seq, 48); OFF(ndtk_gamepad_state, connected, 52);

SIZE(ndtk_cpu_surface, 48);
OFF(ndtk_cpu_surface, pixels, 0); OFF(ndtk_cpu_surface, byte_count, 8); OFF(ndtk_cpu_surface, stride, 16);
OFF(ndtk_cpu_surface, width, 20); OFF(ndtk_cpu_surface, height, 24); OFF(ndtk_cpu_surface, chan, 28);
OFF(ndtk_cpu_surface, config_seq, 32); OFF(ndtk_cpu_surface, age, 36); OFF(ndtk_cpu_surface, surface, 40);

SIZE(ndtk_text_metrics, 24);
OFF(ndtk_text_metrics, ascent, 8); OFF(ndtk_text_metrics, lines, 16); OFF(ndtk_text_metrics, clusters, 20);

SIZE(ndtk_gpu_handle_bag, 56);
OFF(ndtk_gpu_handle_bag, kind, 4); OFF(ndtk_gpu_handle_bag, window_id, 8); OFF(ndtk_gpu_handle_bag, surface_port, 12);
OFF(ndtk_gpu_handle_bag, window, 16); OFF(ndtk_gpu_handle_bag, desktop, 24); OFF(ndtk_gpu_handle_bag, config_seq, 32);
OFF(ndtk_gpu_handle_bag, reserved, 40);

SIZE(ndtk_gpu_desc, 48);
OFF(ndtk_gpu_desc, flags, 4); OFF(ndtk_gpu_desc, latency, 8); OFF(ndtk_gpu_desc, buffer_width, 12);
OFF(ndtk_gpu_desc, buffer_height, 16); OFF(ndtk_gpu_desc, feature_count, 20); OFF(ndtk_gpu_desc, features, 24);
OFF(ndtk_gpu_desc, reserved, 32);

SIZE(ndtk_gpu_context, 96);
OFF(ndtk_gpu_context, format, 4); OFF(ndtk_gpu_context, instance, 8); OFF(ndtk_gpu_context, adapter, 16);
OFF(ndtk_gpu_context, device, 24); OFF(ndtk_gpu_context, queue, 32); OFF(ndtk_gpu_context, surface, 40);
OFF(ndtk_gpu_context, gpu, 48); OFF(ndtk_gpu_context, usage, 56); OFF(ndtk_gpu_context, width, 64);
OFF(ndtk_gpu_context, height, 68); OFF(ndtk_gpu_context, config_seq, 72); OFF(ndtk_gpu_context, reserved, 80);

SIZE(ndtk_gpu_frame, 32);
OFF(ndtk_gpu_frame, texture, 0); OFF(ndtk_gpu_frame, view, 8); OFF(ndtk_gpu_frame, width, 16);
OFF(ndtk_gpu_frame, height, 20); OFF(ndtk_gpu_frame, config_seq, 24); OFF(ndtk_gpu_frame, reconfigured, 28);

SIZE(ndtk_gpu_budget, 40);
OFF(ndtk_gpu_budget, pressure, 4); OFF(ndtk_gpu_budget, budget_bytes, 8); OFF(ndtk_gpu_budget, used_bytes, 16);

SIZE(ndtk_ui_event, 24);
OFF(ndtk_ui_event, result, 0); OFF(ndtk_ui_event, node, 8); OFF(ndtk_ui_event, value, 16);

SIZE(ndtk_ui_stats, 80);
OFF(ndtk_ui_stats, nodes, 4); OFF(ndtk_ui_stats, presents, 8); OFF(ndtk_ui_stats, skipped_draws, 16);
OFF(ndtk_ui_stats, full_repaints, 24); OFF(ndtk_ui_stats, invalid_handles, 32); OFF(ndtk_ui_stats, arena_capacity, 40);
OFF(ndtk_ui_stats, arena_high_water, 48); OFF(ndtk_ui_stats, arena_spills, 56); OFF(ndtk_ui_stats, reserved, 64);

/* The toolkit's handle kinds never collide with the audio service's (1..5). */
static_assert(NDTK_KIND_LOOP >= 0x40 && NDTK_KIND_THREAD < 0x80, "toolkit kinds are 0x40-0x7f");

int main(void)
{
	ndtk_handle h = NDTK_HANDLE_MAKE(NDTK_KIND_NODE, 0xabcdefu, 0x12345678u);
	if (NDTK_HANDLE_KIND(h) != NDTK_KIND_NODE) return 1;
	if (NDTK_HANDLE_GEN(h) != 0xabcdefu) return 2;
	if (NDTK_HANDLE_INDEX(h) != 0x12345678u) return 3;
	if (NDTK_HANDLE_GEN(NDTK_HANDLE_MAKE(NDTK_KIND_WINDOW, 0x1000000u, 1)) != 0) return 4;  /* 24-bit generation */
	if (NDTK_HANDLE_KIND(NDTK_HANDLE_NONE) != 0 || NDTK_HANDLE_GEN(NDTK_HANDLE_NONE) != 0) return 5;

	ndtk_event e = {0};
	e.kind = NDTK_EV_FRAME;
	e.u.frame.target_ns = 1;
	if (e.u.reserved_[8] != 1 && e.u.reserved_[15] != 1) return 6;  /* frame payload starts the union */
	return 0;
}
