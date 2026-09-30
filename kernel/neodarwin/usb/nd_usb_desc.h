/*
 * SPDX-License-Identifier: BSD-2-Clause
 * NeoDarwin-Language: portability: USB descriptor parsing shared by the kernel's C++ xHCI driver and its host tests; Embedded Swift in kexts is not proven yet (P0-10).
 *
 * The USB 2.0 and 3.2 chapter 9 descriptors and requests NeoDarwin's
 * console keyboard driver needs (docs/kernel/usb-console.md), and the one
 * decision it makes from a configuration descriptor: which interface it
 * drives. A HID boot keyboard (class 3, subclass 1, protocol 1; HID 1.11
 * appendix B) or a hub (class 9; USB 2.0 §11.23), each with its interrupt
 * IN endpoint. Descriptors come from the device and are not trusted:
 * every length is checked against the buffer.
 */
#ifndef ND_USB_DESC_H
#define ND_USB_DESC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Descriptor types. */
enum {
	ND_USB_DT_DEVICE = 1,
	ND_USB_DT_CONFIG = 2,
	ND_USB_DT_STRING = 3,
	ND_USB_DT_INTERFACE = 4,
	ND_USB_DT_ENDPOINT = 5,
	ND_USB_DT_HID = 0x21,
	ND_USB_DT_HUB = 0x29,
	ND_USB_DT_SS_HUB = 0x2a,
	ND_USB_DT_SS_EP_COMPANION = 0x30,
};

/* Standard, class and hub requests (USB 2.0 §9.4, §11.24; HID 1.11 §7.2). */
enum {
	ND_USB_REQ_GET_STATUS = 0,
	ND_USB_REQ_CLEAR_FEATURE = 1,
	ND_USB_REQ_SET_FEATURE = 3,
	ND_USB_REQ_GET_DESCRIPTOR = 6,
	ND_USB_REQ_SET_CONFIGURATION = 9,
	ND_HID_REQ_SET_REPORT = 0x09,
	ND_HID_REQ_SET_IDLE = 0x0a,
	ND_HID_REQ_SET_PROTOCOL = 0x0b,
};

/* bmRequestType values. */
enum {
	ND_USB_RT_DEVICE_IN = 0x80,
	ND_USB_RT_DEVICE_OUT = 0x00,
	ND_USB_RT_CLASS_INTERFACE_OUT = 0x21,
	ND_USB_RT_CLASS_DEVICE_IN = 0xa0,
	ND_USB_RT_CLASS_OTHER_IN = 0xa3,
	ND_USB_RT_CLASS_OTHER_OUT = 0x23,
};

/* Hub port features and status (USB 2.0 §11.24.2). */
enum {
	ND_HUB_PORT_RESET = 4,
	ND_HUB_PORT_POWER = 8,
	ND_HUB_C_PORT_CONNECTION = 16,
	ND_HUB_C_PORT_ENABLE = 17,
	ND_HUB_C_PORT_SUSPEND = 18,
	ND_HUB_C_PORT_OVER_CURRENT = 19,
	ND_HUB_C_PORT_RESET = 20,
};
#define ND_HUB_PS_CONNECTION    (1u << 0)
#define ND_HUB_PS_ENABLE        (1u << 1)
#define ND_HUB_PS_RESET         (1u << 4)
#define ND_HUB_PS_POWER         (1u << 8)
#define ND_HUB_PS_LOW_SPEED     (1u << 9)
#define ND_HUB_PS_HIGH_SPEED    (1u << 10)
#define ND_HUB_PC_CONNECTION    (1u << 0)
#define ND_HUB_PC_ENABLE        (1u << 1)
#define ND_HUB_PC_SUSPEND       (1u << 2)
#define ND_HUB_PC_OVER_CURRENT  (1u << 3)
#define ND_HUB_PC_RESET         (1u << 4)

struct nd_usb_device_desc {
	uint8_t bcdUSB_lo, bcdUSB_hi;
	uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor, idProduct;
	uint8_t iManufacturer, iProduct;
	uint8_t bNumConfigurations;
};

static inline uint16_t
nd_usb_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/* The 18-byte device descriptor. False if it is too short or mistyped. */
static inline bool
nd_usb_parse_device(const uint8_t *d, size_t length, struct nd_usb_device_desc *out)
{
	if (length < 18 || d[0] < 18 || d[1] != ND_USB_DT_DEVICE) {
		return false;
	}
	out->bcdUSB_lo = d[2];
	out->bcdUSB_hi = d[3];
	out->bDeviceClass = d[4];
	out->bDeviceSubClass = d[5];
	out->bDeviceProtocol = d[6];
	out->bMaxPacketSize0 = d[7];
	out->idVendor = nd_usb_le16(d + 8);
	out->idProduct = nd_usb_le16(d + 10);
	out->iManufacturer = d[14];
	out->iProduct = d[15];
	out->bNumConfigurations = d[17];
	return true;
}

enum nd_usb_function {
	ND_USB_FUNCTION_NONE = 0,
	ND_USB_FUNCTION_KEYBOARD,       /* HID boot keyboard */
	ND_USB_FUNCTION_HUB,
};

/* What the driver drives in a configuration, and what it found besides. */
struct nd_usb_choice {
	enum nd_usb_function function;
	uint8_t configurationValue;
	uint8_t interfaceNumber;
	uint8_t alternateSetting;
	uint8_t endpointAddress;        /* interrupt IN */
	uint16_t maxPacketSize;         /* bits 10:0 */
	uint8_t mult;                   /* high-bandwidth: bits 12:11 */
	uint8_t bInterval;
	uint8_t maxBurst;               /* from a SuperSpeed companion */
	uint8_t interfaces;             /* interface descriptors seen */
	/* the first interface that isn't driven, for the log */
	uint8_t otherClass, otherSubClass, otherProtocol;
	bool other;
};

/*
 * Walks a whole configuration descriptor (wTotalLength bytes, or as many as
 * were read). The first boot keyboard interface wins; else a hub interface
 * (or a hub device class). Each interface's endpoints are the endpoint
 * descriptors after it; the first interrupt IN one is taken. Returns false
 * when the descriptor is malformed (a zero or overrunning bLength, or no
 * configuration header), true otherwise, with choice->function NONE when
 * nothing is driven.
 */
static inline bool
nd_usb_parse_config(const uint8_t *d, size_t length, uint8_t deviceClass, struct nd_usb_choice *choice)
{
	struct nd_usb_choice c = { ND_USB_FUNCTION_NONE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false };
	if (length < 9 || d[0] < 9 || d[1] != ND_USB_DT_CONFIG) {
		return false;
	}
	size_t total = nd_usb_le16(d + 2);
	if (total < length) {
		length = total;
	}
	c.configurationValue = d[5];
	enum nd_usb_function current = ND_USB_FUNCTION_NONE;    /* the interface being walked */
	bool chosen = false;
	uint8_t iface = 0, alt = 0;
	bool lastWasChosenEndpoint = false;
	for (size_t off = d[0]; off + 2 <= length;) {
		uint8_t bLength = d[off];
		uint8_t type = d[off + 1];
		if (bLength < 2 || off + bLength > length) {
			return false;
		}
		const uint8_t *p = d + off;
		if (type == ND_USB_DT_INTERFACE && bLength >= 9) {
			c.interfaces++;
			iface = p[2];
			alt = p[3];
			uint8_t cls = p[5], sub = p[6], proto = p[7];
			current = ND_USB_FUNCTION_NONE;
			if (cls == 3 && sub == 1 && proto == 1) {
				current = ND_USB_FUNCTION_KEYBOARD;
			} else if (cls == 9 || (deviceClass == 9 && cls == 0)) {
				current = ND_USB_FUNCTION_HUB;
			} else if (!c.other) {
				c.other = true;
				c.otherClass = cls;
				c.otherSubClass = sub;
				c.otherProtocol = proto;
			}
			/* a keyboard replaces a hub chosen earlier; nothing replaces a keyboard */
			if (current != ND_USB_FUNCTION_NONE && chosen &&
			    !(current == ND_USB_FUNCTION_KEYBOARD && c.function == ND_USB_FUNCTION_HUB)) {
				current = ND_USB_FUNCTION_NONE;
			}
			lastWasChosenEndpoint = false;
		} else if (type == ND_USB_DT_ENDPOINT && bLength >= 7 && current != ND_USB_FUNCTION_NONE) {
			uint8_t address = p[2], attributes = p[3];
			uint16_t wMaxPacketSize = nd_usb_le16(p + 4);
			if ((address & 0x80) != 0 && (attributes & 3) == 3 && (wMaxPacketSize & 0x7ff) != 0) {
				c.function = current;
				c.interfaceNumber = iface;
				c.alternateSetting = alt;
				c.endpointAddress = address;
				c.maxPacketSize = wMaxPacketSize & 0x7ff;
				c.mult = (uint8_t)((wMaxPacketSize >> 11) & 3);
				c.bInterval = p[6];
				c.maxBurst = 0;
				chosen = true;
				current = ND_USB_FUNCTION_NONE;         /* this interface is done */
				lastWasChosenEndpoint = true;
				off += bLength;
				continue;
			}
		} else if (type == ND_USB_DT_SS_EP_COMPANION && bLength >= 6 && lastWasChosenEndpoint) {
			c.maxBurst = p[2] > 15 ? 15 : p[2];
		}
		if (type != ND_USB_DT_SS_EP_COMPANION) {
			lastWasChosenEndpoint = false;
		}
		off += bLength;
	}
	*choice = c;
	return true;
}

/* A USB 2.0 hub descriptor (§11.23.2.1): ports, characteristics (the TT
 * think time in bits 6:5), power-on-to-good time in 2 ms units. */
struct nd_usb_hub_desc {
	uint8_t ports;
	uint16_t characteristics;
	uint8_t powerOnToGood2ms;
};

static inline bool
nd_usb_parse_hub(const uint8_t *d, size_t length, struct nd_usb_hub_desc *out)
{
	if (length < 7 || d[0] < 7 || (d[1] != ND_USB_DT_HUB && d[1] != ND_USB_DT_SS_HUB)) {
		return false;
	}
	out->ports = d[2];
	out->characteristics = nd_usb_le16(d + 3);
	out->powerOnToGood2ms = d[5];
	return true;
}

/* A string descriptor's UTF-16LE text as ASCII ('?' for the rest), NUL
 * terminated, at most cap - 1 characters. */
static inline void
nd_usb_string_ascii(const uint8_t *d, size_t length, char *out, size_t cap)
{
	size_t n = 0;
	if (cap == 0) {
		return;
	}
	if (length >= 2 && d[1] == ND_USB_DT_STRING) {
		size_t end = d[0] < length ? d[0] : length;
		for (size_t i = 2; i + 1 < end && n + 1 < cap; i += 2) {
			uint16_t ch = nd_usb_le16(d + i);
			out[n++] = (ch >= 0x20 && ch < 0x7f) ? (char)ch : '?';
		}
	}
	while (n > 0 && out[n - 1] == ' ') {
		n--;
	}
	out[n] = 0;
}

#endif /* ND_USB_DESC_H */
