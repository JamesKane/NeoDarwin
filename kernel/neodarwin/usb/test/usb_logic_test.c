/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: a host test of the console keyboard driver's C headers, built with the host's C compiler.
 *
 * The parts of the console USB keyboard that don't need a controller
 * (docs/kernel/usb-console.md): the boot keyboard's reports as terminal
 * bytes (US layout, modifiers, escape sequences, rollover, the key to
 * repeat); configuration, hub and string descriptors, well formed and
 * not; xHCI intervals, route strings, device context indices, PORTSC and
 * USBLEGCTLSTS values; and the DWC3 role-switch set-up against a register
 * model.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t model[0x10000 / 4];
static int writes;
#define ND_DWC3_READ(regs, offset)              (((uint32_t *)(regs))[(offset) / 4])
#define ND_DWC3_WRITE(regs, offset, value)      (writes++, ((uint32_t *)(regs))[(offset) / 4] = (value))

#include "nd_dwc3.h"
#include "nd_hid_kbd.h"
#include "nd_usb_desc.h"
#include "nd_xhci.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* Feeds one report; returns the bytes as a string (NUL-terminated). */
static const char *
feed(struct nd_hid_kbd *k, uint8_t mods, uint8_t k0, uint8_t k1, uint8_t k2)
{
	static char out[65];
	uint8_t report[8] = { mods, 0, k0, k1, k2, 0, 0, 0 };
	size_t n = nd_hid_kbd_report(k, report, sizeof(report), (uint8_t *)out, 64);
	out[n] = 0;
	return out;
}

static void
test_keyboard(void)
{
	struct nd_hid_kbd k;
	nd_hid_kbd_init(&k);
	CHECK(strcmp(feed(&k, 0, 0x04, 0, 0), "a") == 0);
	CHECK(k.repeatKey == 0x04);
	CHECK(strcmp(feed(&k, 0, 0x04, 0, 0), "") == 0);           /* still held: nothing new */
	CHECK(strcmp(feed(&k, 0, 0x04, 0x05, 0), "b") == 0);       /* rollover: b pressed */
	CHECK(k.repeatKey == 0x05);
	CHECK(strcmp(feed(&k, 0, 0x04, 0, 0), "") == 0);           /* b released: no repeat */
	CHECK(k.repeatKey == 0);
	CHECK(strcmp(feed(&k, 0, 0, 0, 0), "") == 0);
	/* shift, digits, punctuation */
	CHECK(strcmp(feed(&k, ND_HID_MOD_LSHIFT, 0x04, 0, 0), "A") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, ND_HID_MOD_RSHIFT, 0x1e, 0, 0), "!") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x27, 0x2d, 0x38), "0-/") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, ND_HID_MOD_LSHIFT, 0x21, 0, 0), "$") == 0);  /* the $((6*7)) the tests type */
	feed(&k, 0, 0, 0, 0);
	/* Enter, Backspace, Tab, Esc */
	CHECK(strcmp(feed(&k, 0, 0x28, 0, 0), "\r") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x2a, 0, 0), "\x7f") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x2b, 0, 0), "\t") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x29, 0, 0), "\x1b") == 0);
	feed(&k, 0, 0, 0, 0);
	/* arrows and editing keys */
	CHECK(strcmp(feed(&k, 0, 0x50, 0, 0), "\x1b[D") == 0);
	CHECK(k.repeatKey == 0x50);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x52, 0, 0), "\x1b[A") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x4c, 0, 0), "\x1b[3~") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x3e, 0, 0), "\x1b[15~") == 0);
	feed(&k, 0, 0, 0, 0);
	/* Ctrl and Alt */
	CHECK(strcmp(feed(&k, ND_HID_MOD_LCTRL, 0x06, 0, 0), "\x03") == 0);        /* ^C */
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, ND_HID_MOD_RCTRL, 0x2f, 0, 0), "\x1b") == 0);        /* ^[ */
	feed(&k, 0, 0, 0, 0);
	char ctrlSpace[2] = { 0 };
	uint8_t report[8] = { ND_HID_MOD_LCTRL, 0, 0x2c, 0, 0, 0, 0, 0 };
	CHECK(nd_hid_kbd_report(&k, report, 8, (uint8_t *)ctrlSpace, 2) == 1 && ctrlSpace[0] == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, ND_HID_MOD_LALT, 0x05, 0, 0), "\x1b" "b") == 0);     /* meta-b */
	feed(&k, 0, 0, 0, 0);
	/* modifiers alone send nothing and don't repeat */
	CHECK(strcmp(feed(&k, ND_HID_MOD_LSHIFT | ND_HID_MOD_LCTRL, 0, 0, 0), "") == 0);
	CHECK(k.repeatKey == 0);
	feed(&k, 0, 0, 0, 0);
	/* Caps Lock: letters only, shift inverts it */
	CHECK(strcmp(feed(&k, 0, ND_HID_KEY_CAPS_LOCK, 0, 0), "") == 0);
	CHECK(k.capsLock && k.repeatKey == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, 0, 0x04, 0x1e, 0), "A1") == 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(strcmp(feed(&k, ND_HID_MOD_LSHIFT, 0x04, 0, 0), "a") == 0);
	feed(&k, 0, 0, 0, 0);
	feed(&k, 0, ND_HID_KEY_CAPS_LOCK, 0, 0);
	feed(&k, 0, 0, 0, 0);
	CHECK(!k.capsLock);
	/* keypad, as with Num Lock on */
	CHECK(strcmp(feed(&k, 0, 0x59, 0x62, 0x58), "10\r") == 0);
	feed(&k, 0, 0, 0, 0);
	/* ErrorRollOver: ignored, the keys held stay held */
	feed(&k, 0, 0x04, 0, 0);
	CHECK(strcmp(feed(&k, 0, 1, 1, 1), "") == 0);
	CHECK(strcmp(feed(&k, 0, 0x04, 0, 0), "") == 0);
	CHECK(k.repeatKey == 0x04);
	feed(&k, 0, 0, 0, 0);
	/* the repeat key's bytes with the modifiers held now */
	feed(&k, 0, 0x0b, 0, 0);
	uint8_t bytes[ND_HID_KBD_MAX_BYTES];
	k.modifiers = ND_HID_MOD_LSHIFT;
	CHECK(nd_hid_kbd_bytes(&k, k.repeatKey, bytes) == 1 && bytes[0] == 'H');
	/* short reports and a full output buffer */
	CHECK(nd_hid_kbd_report(&k, report, 2, bytes, 8) == 0);
	nd_hid_kbd_init(&k);
	uint8_t many[8] = { 0, 0, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
	CHECK(nd_hid_kbd_report(&k, many, 8, bytes, 3) == 3 && memcmp(bytes, "abc", 3) == 0);
}

/* QEMU's usb-kbd: one interface, 3/1/1, endpoint 0x81, 8 bytes, 10 ms. */
static const uint8_t qemu_kbd[] = {
	9, 2, 34, 0, 1, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 3, 1, 1, 0,
	9, 0x21, 0x11, 0x01, 0, 1, 0x22, 63, 0,
	7, 5, 0x81, 3, 8, 0, 10,
};

/* The Q8B's keyboard (1a2c:506f): a boot keyboard and a second HID
 * interface (consumer and system control). */
static const uint8_t composite_kbd[] = {
	9, 2, 59, 0, 2, 1, 0, 0xa0, 50,
	9, 4, 0, 0, 1, 3, 1, 1, 0,
	9, 0x21, 0x10, 0x01, 0, 1, 0x22, 65, 0,
	7, 5, 0x81, 3, 8, 0, 10,
	9, 4, 1, 0, 1, 3, 0, 0, 0,
	9, 0x21, 0x10, 0x01, 0, 1, 0x22, 50, 0,
	7, 5, 0x82, 3, 8, 0, 10,
};

/* A hub: device class 9, one interface, status endpoint 0x81, 1 byte,
 * 255 ms (full speed) or 12 (2^11 microframes, high speed). */
static const uint8_t hub_cfg[] = {
	9, 2, 25, 0, 1, 1, 0, 0xe0, 0,
	9, 4, 0, 0, 1, 9, 0, 0, 0,
	7, 5, 0x81, 3, 1, 0, 12,
};

/* A mass storage stick: bulk only. */
static const uint8_t storage_cfg[] = {
	9, 2, 32, 0, 1, 1, 0, 0x80, 50,
	9, 4, 0, 0, 2, 8, 6, 0x50, 0,
	7, 5, 0x81, 2, 0, 2, 0,
	7, 5, 0x02, 2, 0, 2, 0,
};

static void
test_descriptors(void)
{
	struct nd_usb_choice c;
	CHECK(nd_usb_parse_config(qemu_kbd, sizeof(qemu_kbd), 0, &c));
	CHECK(c.function == ND_USB_FUNCTION_KEYBOARD && c.configurationValue == 1 && c.interfaceNumber == 0);
	CHECK(c.endpointAddress == 0x81 && c.maxPacketSize == 8 && c.bInterval == 10 && !c.other);
	CHECK(nd_usb_parse_config(composite_kbd, sizeof(composite_kbd), 0, &c));
	CHECK(c.function == ND_USB_FUNCTION_KEYBOARD && c.interfaceNumber == 0 && c.endpointAddress == 0x81);
	CHECK(c.other && c.otherClass == 3 && c.otherSubClass == 0 && c.interfaces == 2);
	CHECK(nd_usb_parse_config(hub_cfg, sizeof(hub_cfg), 9, &c));
	CHECK(c.function == ND_USB_FUNCTION_HUB && c.maxPacketSize == 1 && c.bInterval == 12);
	CHECK(nd_usb_parse_config(storage_cfg, sizeof(storage_cfg), 0, &c));
	CHECK(c.function == ND_USB_FUNCTION_NONE && c.other && c.otherClass == 8 && c.otherProtocol == 0x50);
	/* malformed: zero bLength, overrun, wrong type, too short */
	uint8_t bad[sizeof(qemu_kbd)];
	memcpy(bad, qemu_kbd, sizeof(bad));
	bad[9] = 0;
	CHECK(!nd_usb_parse_config(bad, sizeof(bad), 0, &c));
	memcpy(bad, qemu_kbd, sizeof(bad));
	bad[27] = 40;
	CHECK(!nd_usb_parse_config(bad, sizeof(bad), 0, &c));
	memcpy(bad, qemu_kbd, sizeof(bad));
	bad[1] = 1;
	CHECK(!nd_usb_parse_config(bad, sizeof(bad), 0, &c));
	CHECK(!nd_usb_parse_config(qemu_kbd, 8, 0, &c));
	/* truncated to wTotalLength: the endpoint beyond it isn't seen */
	memcpy(bad, qemu_kbd, sizeof(bad));
	bad[2] = 27;
	CHECK(nd_usb_parse_config(bad, sizeof(bad), 0, &c) && c.function == ND_USB_FUNCTION_NONE);
	/* a boot keyboard whose interrupt endpoint is OUT only is not driven */
	memcpy(bad, qemu_kbd, sizeof(bad));
	bad[29] = 0x01;
	CHECK(nd_usb_parse_config(bad, sizeof(bad), 0, &c) && c.function == ND_USB_FUNCTION_NONE);

	static const uint8_t dev[18] = { 18, 1, 0x00, 0x02, 0, 0, 0, 8, 0x27, 0x06, 0x01, 0x00, 0, 0, 1, 3, 0, 1 };
	struct nd_usb_device_desc d;
	CHECK(nd_usb_parse_device(dev, 18, &d) && d.idVendor == 0x0627 && d.idProduct == 1 && d.bMaxPacketSize0 == 8);
	CHECK(d.iProduct == 3 && d.bcdUSB_hi == 2);
	CHECK(!nd_usb_parse_device(dev, 17, &d));

	static const uint8_t hub[9] = { 9, 0x29, 4, 0x69, 0x00, 50, 100, 0, 0xff };
	struct nd_usb_hub_desc h;
	CHECK(nd_usb_parse_hub(hub, sizeof(hub), &h) && h.ports == 4 && h.powerOnToGood2ms == 50);
	CHECK(((h.characteristics >> 5) & 3) == 3);
	CHECK(!nd_usb_parse_hub(hub, 6, &h));

	static const uint8_t str[] = { 22, 3, 'Q', 0, 'E', 0, 'M', 0, 'U', 0, ' ', 0, 'K', 0, 'b', 0, 'd', 0, 0xac, 0x20, ' ', 0 };
	char s[16];
	nd_usb_string_ascii(str, sizeof(str), s, sizeof(s));
	CHECK(strcmp(s, "QEMU Kbd?") == 0);
	nd_usb_string_ascii(str, sizeof(str), s, 5);
	CHECK(strcmp(s, "QEMU") == 0);
	nd_usb_string_ascii(str, 1, s, sizeof(s));
	CHECK(s[0] == 0);
}

static void
test_xhci(void)
{
	/* intervals: full speed 10 ms -> 8 ms (2^6 x 125 us), 1 ms, 255 ms -> 128 ms; high speed 2^(b-1) */
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_FULL, 10) == 6);
	CHECK(nd_xhci_interval_us(6) == 8000);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_LOW, 1) == 3);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_FULL, 255) == 10);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_FULL, 0) == 3);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_HIGH, 12) == 11);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_HIGH, 1) == 0);
	CHECK(nd_xhci_interrupt_interval(ND_XHCI_SPEED_SUPER, 40) == 15);
	/* route strings: a hub on a root port (depth 0), its port 3; a hub
	 * there, its port 2; ports above 15 are 15 */
	CHECK(nd_xhci_route(0, 0, 3) == 0x3);
	CHECK(nd_xhci_route(0x3, 1, 2) == 0x23);
	CHECK(nd_xhci_route(0x23, 2, 20) == 0xf23);
	CHECK(nd_xhci_route(0x54321, 5, 1) == 0x54321);
	/* device context indices */
	CHECK(nd_xhci_dci(0x00) == 1 && nd_xhci_dci(0x81) == 3 && nd_xhci_dci(0x02) == 4 && nd_xhci_dci(0x8f) == 31);
	/* EP0 packet sizes */
	CHECK(nd_xhci_default_mps0(ND_XHCI_SPEED_LOW) == 8 && nd_xhci_default_mps0(ND_XHCI_SPEED_HIGH) == 64);
	CHECK(nd_xhci_mps0_from_descriptor(ND_XHCI_SPEED_FULL, 64) == 64);
	CHECK(nd_xhci_mps0_from_descriptor(ND_XHCI_SPEED_FULL, 7) == 8);
	CHECK(nd_xhci_mps0_from_descriptor(ND_XHCI_SPEED_SUPER, 9) == 512);
	/* PORTSC: a write keeps power and the wake enables, never PED or a change */
	uint32_t sc = ND_XHCI_PORTSC_CCS | ND_XHCI_PORTSC_PED | ND_XHCI_PORTSC_PP | ND_XHCI_PORTSC_CSC |
	    ND_XHCI_PORTSC_PRC | ND_XHCI_PORTSC_WCE | (3u << 10);
	CHECK(nd_xhci_portsc_neutral(sc) == (ND_XHCI_PORTSC_PP | ND_XHCI_PORTSC_WCE));
	CHECK(ND_XHCI_PORTSC_SPEED(sc) == ND_XHCI_SPEED_HIGH);
	/* USBLEGCTLSTS: SMI enables off, events acknowledged, reserved bits kept */
	CHECK(nd_xhci_legctl_quiet(0xe001e011u | (1u << 7)) == (0xe0000000u | (1u << 16) | (1u << 7)));
	/* scratchpad count: Hi in 25:21, Lo in 31:27 */
	CHECK(ND_XHCI_HCS2_SCRATCHPADS((1u << 21) | (3u << 27)) == 35);
	CHECK(sizeof(struct nd_xhci_trb) == 16);
}

static void
test_dwc3(void)
{
	uint32_t id, thr;
	/* not a DWC3: nothing touched */
	memset(model, 0, sizeof(model));
	writes = 0;
	CHECK(nd_dwc3_urs_setup(model, sizeof(model), &id, &thr) == ND_DWC3_NOT_DWC3 && writes == 0);
	/* a window too small to hold GSNPSID */
	CHECK(nd_dwc3_urs_setup(model, 0xc000, &id, &thr) == ND_DWC3_NOT_DWC3 && id == 0);
	/* the Q8B's USB-C cores: DWC_usb31 in host mode, threshold left on by UEFI */
	model[ND_DWC3_GSNPSID / 4] = 0x3331310a;
	model[ND_DWC3_GCTL / 4] = ND_DWC3_GCTL_PRTCAPDIR_HOST | 0x00200000;
	model[ND_DWC3_GRXTHRCFG / 4] = 0x04f30000;
	CHECK(nd_dwc3_urs_setup(model, sizeof(model), &id, &thr) == ND_DWC3_READY);
	CHECK(id == 0x3331310a && thr == 0x04f30000 && model[ND_DWC3_GRXTHRCFG / 4] == 0x00f30000 && writes == 1);
	/* already off: no write */
	writes = 0;
	CHECK(nd_dwc3_urs_setup(model, sizeof(model), &id, &thr) == ND_DWC3_READY && writes == 0);
	/* DWC_usb3: bit 29 */
	model[ND_DWC3_GSNPSID / 4] = 0x5533300a;
	model[ND_DWC3_GRXTHRCFG / 4] = 0x24000000;
	CHECK(nd_dwc3_urs_setup(model, sizeof(model), &id, &thr) == ND_DWC3_READY && model[ND_DWC3_GRXTHRCFG / 4] == 0x04000000);
	/* device mode: refused, nothing written */
	writes = 0;
	model[ND_DWC3_GCTL / 4] = 0x2u << 12;
	model[ND_DWC3_GRXTHRCFG / 4] = 0x24000000;
	CHECK(nd_dwc3_urs_setup(model, sizeof(model), &id, &thr) == ND_DWC3_NOT_HOST && writes == 0);
}

int
main(void)
{
	test_keyboard();
	test_descriptors();
	test_xhci();
	test_dwc3();
	if (failures != 0) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
