/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: the boot keyboard's report-to-console state machine, shared by the kernel's C++ xHCI driver and its host tests; Embedded Swift in kexts is not proven yet (P0-10).
 *
 * A HID boot-protocol keyboard as a console (HID 1.11 appendix B; usages
 * from the HID Usage Tables' Keyboard/Keypad page 0x07), with a US layout:
 * each 8-byte report (modifiers, reserved, six key usages) is compared
 * with the last one, and each newly pressed key becomes the bytes a
 * terminal sends for it. Enter is CR, Backspace DEL (0x7f), the arrows,
 * Home, End, Insert, Delete, Page Up/Down and F1-F12 are the ANSI/xterm
 * escape sequences zsh's line editor binds, Ctrl makes control characters,
 * Alt prefixes ESC (meta), Caps Lock toggles letters' case. The keypad is
 * read as with Num Lock on.
 *
 * Typematic repeat is the caller's (it owns the timer): repeatKey is the
 * usage that should repeat, the last key pressed and still held, or 0.
 * nd_hid_kbd_bytes gives the bytes for it with the modifiers held now.
 */
#ifndef ND_HID_KBD_H
#define ND_HID_KBD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ND_HID_KBD_REPORT       8
#define ND_HID_KBD_MAX_BYTES    8       /* the longest sequence: ESC [ 1 5 ~ with an ESC prefix */

#define ND_HID_MOD_LCTRL        0x01
#define ND_HID_MOD_LSHIFT       0x02
#define ND_HID_MOD_LALT         0x04
#define ND_HID_MOD_LGUI         0x08
#define ND_HID_MOD_RCTRL        0x10
#define ND_HID_MOD_RSHIFT       0x20
#define ND_HID_MOD_RALT         0x40
#define ND_HID_MOD_RGUI         0x80
#define ND_HID_MOD_CTRL         (ND_HID_MOD_LCTRL | ND_HID_MOD_RCTRL)
#define ND_HID_MOD_SHIFT        (ND_HID_MOD_LSHIFT | ND_HID_MOD_RSHIFT)
#define ND_HID_MOD_ALT          (ND_HID_MOD_LALT | ND_HID_MOD_RALT)

#define ND_HID_KEY_ERROR_ROLLOVER 0x01
#define ND_HID_KEY_CAPS_LOCK    0x39

struct nd_hid_kbd {
	uint8_t modifiers;
	uint8_t keys[6];        /* the last report's keys */
	uint8_t repeatKey;      /* the key to repeat, 0 for none */
	bool capsLock;
};

static inline void
nd_hid_kbd_init(struct nd_hid_kbd *k)
{
	k->modifiers = 0;
	for (int i = 0; i < 6; i++) {
		k->keys[i] = 0;
	}
	k->repeatKey = 0;
	k->capsLock = false;
}

/* Usages 0x04-0x38: letters, digits, Enter, Esc, Backspace, Tab, space and
 * punctuation, unshifted and shifted (0 for keys handled elsewhere). */
static const char nd_hid_kbd_plain[0x39] = {
	0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
	'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '1', '2',
	'3', '4', '5', '6', '7', '8', '9', '0', '\r', 0x1b, 0x7f, '\t', ' ', '-', '=', '[',
	']', '\\', '#', ';', '\'', '`', ',', '.', '/',
};
static const char nd_hid_kbd_shifted[0x39] = {
	0, 0, 0, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L',
	'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '!', '@',
	'#', '$', '%', '^', '&', '*', '(', ')', '\r', 0x1b, 0x7f, '\t', ' ', '_', '+', '{',
	'}', '|', '~', ':', '"', '~', '<', '>', '?',
};

static inline size_t
nd_hid_kbd_put(uint8_t *out, size_t n, const char *s)
{
	while (*s != 0 && n < ND_HID_KBD_MAX_BYTES) {
		out[n++] = (uint8_t)*s++;
	}
	return n;
}

/* Whether a usage produces input and may repeat (not a modifier, Caps
 * Lock, the rollover error or an unmapped key). */
static inline bool
nd_hid_kbd_is_typing_key(uint8_t usage)
{
	if (usage >= 0x04 && usage <= 0x38) {
		return true;
	}
	if (usage >= 0x3a && usage <= 0x45) {       /* F1-F12 */
		return true;
	}
	if (usage >= 0x49 && usage <= 0x52) {       /* Insert ... Up */
		return true;
	}
	return usage >= 0x54 && usage <= 0x63;      /* keypad */
}

/*
 * The bytes a key sends with the given modifiers; out has room for
 * ND_HID_KBD_MAX_BYTES. Returns the count (0 for a key that sends nothing).
 */
static inline size_t
nd_hid_kbd_bytes(const struct nd_hid_kbd *k, uint8_t usage, uint8_t *out)
{
	bool shift = (k->modifiers & ND_HID_MOD_SHIFT) != 0;
	bool ctrl = (k->modifiers & ND_HID_MOD_CTRL) != 0;
	bool alt = (k->modifiers & ND_HID_MOD_ALT) != 0;
	size_t n = 0;
	if (alt) {
		out[n++] = 0x1b;                        /* meta: ESC prefix */
	}
	if (usage >= 0x04 && usage <= 0x38) {
		bool letter = usage <= 0x1d;
		bool upper = shift != (letter && k->capsLock);
		char c = upper ? nd_hid_kbd_shifted[usage] : nd_hid_kbd_plain[usage];
		if (ctrl) {
			if (c >= '@' && c <= '_') {
				c = (char)(c - '@');
			} else if (c >= 'a' && c <= 'z') {
				c = (char)(c - 'a' + 1);
			} else if (c == ' ' || c == '2' || c == '@') {
				c = 0;
			} else if (c == '6' || c == '^') {
				c = 0x1e;
			} else if (c == '-' || c == '_') {
				c = 0x1f;
			} else if (c == '/' || c == '?') {
				c = 0x7f;
			}
		}
		out[n++] = (uint8_t)c;
		return n;
	}
	const char *seq = NULL;
	switch (usage) {
	case 0x3a: seq = "\033OP"; break;           /* F1-F4 */
	case 0x3b: seq = "\033OQ"; break;
	case 0x3c: seq = "\033OR"; break;
	case 0x3d: seq = "\033OS"; break;
	case 0x3e: seq = "\033[15~"; break;         /* F5-F12 */
	case 0x3f: seq = "\033[17~"; break;
	case 0x40: seq = "\033[18~"; break;
	case 0x41: seq = "\033[19~"; break;
	case 0x42: seq = "\033[20~"; break;
	case 0x43: seq = "\033[21~"; break;
	case 0x44: seq = "\033[23~"; break;
	case 0x45: seq = "\033[24~"; break;
	case 0x49: seq = "\033[2~"; break;          /* Insert */
	case 0x4a: seq = "\033[H"; break;           /* Home */
	case 0x4b: seq = "\033[5~"; break;          /* Page Up */
	case 0x4c: seq = "\033[3~"; break;          /* Delete */
	case 0x4d: seq = "\033[F"; break;           /* End */
	case 0x4e: seq = "\033[6~"; break;          /* Page Down */
	case 0x4f: seq = "\033[C"; break;           /* Right */
	case 0x50: seq = "\033[D"; break;           /* Left */
	case 0x51: seq = "\033[B"; break;           /* Down */
	case 0x52: seq = "\033[A"; break;           /* Up */
	case 0x54: seq = "/"; break;                /* keypad */
	case 0x55: seq = "*"; break;
	case 0x56: seq = "-"; break;
	case 0x57: seq = "+"; break;
	case 0x58: seq = "\r"; break;
	case 0x59: seq = "1"; break;
	case 0x5a: seq = "2"; break;
	case 0x5b: seq = "3"; break;
	case 0x5c: seq = "4"; break;
	case 0x5d: seq = "5"; break;
	case 0x5e: seq = "6"; break;
	case 0x5f: seq = "7"; break;
	case 0x60: seq = "8"; break;
	case 0x61: seq = "9"; break;
	case 0x62: seq = "0"; break;
	case 0x63: seq = "."; break;
	default: return 0;
	}
	return nd_hid_kbd_put(out, n, seq);
}

/*
 * One boot report. Keys in it that weren't in the last one are pressed,
 * in report order: their bytes are appended to out (room for cap bytes;
 * what doesn't fit is dropped). The repeat key becomes the last key
 * pressed, or 0 when the key repeating is released. A report whose keys
 * are all ErrorRollOver (too many keys down) changes nothing but the
 * modifiers. Returns the number of bytes.
 */
static inline size_t
nd_hid_kbd_report(struct nd_hid_kbd *k, const uint8_t *report, size_t length, uint8_t *out, size_t cap)
{
	size_t n = 0;
	if (length < 3) {
		return 0;
	}
	k->modifiers = report[0];
	size_t count = length - 2 < 6 ? length - 2 : 6;
	const uint8_t *keys = report + 2;
	if (keys[0] == ND_HID_KEY_ERROR_ROLLOVER) {
		return 0;
	}
	for (size_t i = 0; i < count; i++) {
		uint8_t usage = keys[i];
		if (usage < 0x04) {
			continue;
		}
		bool held = false;
		for (int j = 0; j < 6; j++) {
			held = held || k->keys[j] == usage;
		}
		if (held) {
			continue;
		}
		if (usage == ND_HID_KEY_CAPS_LOCK) {
			k->capsLock = !k->capsLock;
			continue;
		}
		uint8_t bytes[ND_HID_KBD_MAX_BYTES];
		size_t b = nd_hid_kbd_bytes(k, usage, bytes);
		for (size_t j = 0; j < b && n < cap; j++) {
			out[n++] = bytes[j];
		}
		if (nd_hid_kbd_is_typing_key(usage)) {
			k->repeatKey = usage;
		}
	}
	if (k->repeatKey != 0) {
		bool stillDown = false;
		for (size_t i = 0; i < count; i++) {
			stillDown = stillDown || keys[i] == k->repeatKey;
		}
		if (!stillDown) {
			k->repeatKey = 0;
		}
	}
	for (int j = 0; j < 6; j++) {
		k->keys[j] = (size_t)j < count ? keys[j] : 0;
	}
	return n;
}

#endif /* ND_HID_KBD_H */
