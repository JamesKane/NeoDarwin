/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: a host test of the kernel's C serial driver, in the same language. */
/*
 * nd_geni_uart.h against a model of a GENI serial engine in UART mode
 * (docs/kernel/serial.md). The model has the registers the driver touches,
 * with the behaviour FreeBSD's driver relies on (and that runs on the Radxa
 * Dragon Q8B):
 *   - M_CMD0 START_TX with UART_TX_TRANS_LEN starts a transmit command
 *     (GENI_STATUS M_CMD_ACTIVE); each TX FIFO word goes out one character
 *     time later, under the TX packing; when TRANS_LEN characters have gone
 *     the command ends and M_IRQ_STATUS CMD_DONE is set. M_CMD_CTRL CANCEL
 *     and ABORT end a command (CANCEL_DONE, ABORT_DONE); a "stuck" engine
 *     ignores CANCEL. M_IRQ_CLEAR clears status bits.
 *   - S_CMD0 START_READ starts the read command (S_CMD_ACTIVE); S_CMD_CTRL
 *     ABORT stops it. Characters arriving while it runs are packed into RX
 *     FIFO words under the RX packing: one per word for 1x8, else four, the
 *     last word partial with RX_FIFO_STATUS RX_LAST and RX_LAST_BYTE_VALID.
 *     Characters arriving without a read command, or into a full FIFO, are
 *     lost.
 *   - The FIFOs are 16 words deep (SE_HW_PARAM_0/1 report it).
 * Time advances one tick per register access; a character takes CHAR_TICKS.
 * The model flags protocol violations: a TX word written with no command or
 * beyond its length, a TX FIFO overrun, a command started while one runs, a
 * read of an empty RX FIFO, and a write to any register outside the set the
 * driver is meant to program (the clock and bit-rate registers above all).
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The model ---- */

#define WINDOW          0x4000
#define DEPTH           16
#define CHAR_TICKS      12
#define PROTO_UART      (2u << 8)
#define RX_LAST         (1u << 31)
#define RX_LBV_SHIFT    28
#define HW_PARAM_0      0x0e24
#define HW_PARAM_1      0x0e28

struct sim {
	uint32_t reg[WINDOW / 4];
	uint64_t now;
	uint64_t accesses;
	int violations;
	char why[256];

	/* transmit */
	bool m_active, stuck;
	uint32_t trans_len, tx_left;    /* characters the command still owes */
	uint32_t tx_fifo[DEPTH];
	int tx_n, tx_max;
	uint64_t tx_next;               /* when the head word finishes */
	uint8_t out[4096];
	int out_n;
	int commands;                   /* START_TX commands */
	int bad_lengths;                /* commands whose length was not 1 */

	/* receive */
	bool s_active;
	uint32_t rx_fifo[DEPTH];
	uint8_t rx_valid[DEPTH];        /* bytes in each word */
	int rx_n;
	uint8_t rx_pending[64];         /* characters on the wire, one per CHAR_TICKS */
	int rx_pending_n;
	uint64_t rx_next;
	int rx_lost;
	int s_starts;
};

static struct sim S;

static void
violation(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (S.violations++ == 0) {
		vsnprintf(S.why, sizeof S.why, fmt, ap);
	}
	va_end(ap);
}

static int
bytes_per_word(uint32_t cfg0)
{
	return cfg0 == 0x0000000f ? 1 : 4;
}

static void
m_done(uint32_t bits)
{
	S.m_active = false;
	S.reg[0x0610 / 4] |= bits;
}

static void
rx_push(uint8_t c)
{
	int bpw = bytes_per_word(S.reg[0x0284 / 4]);
	if (!S.s_active) {
		S.rx_lost++;
		return;
	}
	if (bpw > 1 && S.rx_n > 0 && S.rx_valid[S.rx_n - 1] < bpw) {
		int k = S.rx_valid[S.rx_n - 1]++;
		S.rx_fifo[S.rx_n - 1] |= (uint32_t)c << (8 * k);
		return;
	}
	if (S.rx_n == DEPTH) {
		S.rx_lost++;
		return;
	}
	S.rx_fifo[S.rx_n] = c;
	S.rx_valid[S.rx_n] = 1;
	S.rx_n++;
}

static void
tick(void)
{
	S.now++;
	/* The head TX word goes out one character time after it reached the FIFO head. */
	if (S.m_active && !S.stuck && S.tx_n > 0 && S.now >= S.tx_next) {
		uint32_t w = S.tx_fifo[0];
		int bpw = bytes_per_word(S.reg[0x0260 / 4]);
		for (int k = 0; k < bpw && S.tx_left > 0; k++) {
			if (S.out_n < (int)sizeof S.out) {
				S.out[S.out_n++] = (uint8_t)(w >> (8 * k));
			}
			S.tx_left--;
		}
		memmove(S.tx_fifo, S.tx_fifo + 1, (size_t)(S.tx_n - 1) * sizeof S.tx_fifo[0]);
		S.tx_n--;
		S.tx_next = S.now + CHAR_TICKS;
		if (S.tx_left == 0) {
			m_done(1u << 0);
		}
	}
	/* Characters on the wire land in the RX FIFO one character time apart. */
	if (S.rx_pending_n > 0 && S.now >= S.rx_next) {
		rx_push(S.rx_pending[0]);
		memmove(S.rx_pending, S.rx_pending + 1, (size_t)--S.rx_pending_n);
		S.rx_next = S.now + CHAR_TICKS;
	}
}

static uint32_t
sim_read(uintptr_t base, uint32_t off)
{
	(void)base;
	S.accesses++;
	tick();
	switch (off) {
	case 0x0040: /* GENI_STATUS */
		return (S.m_active ? 1u : 0) | (S.s_active ? 1u << 12 : 0);
	case 0x0068: /* FW_REVISION_RO */
		return S.reg[off / 4];
	case 0x0634: /* S_CMD_CTRL: the abort bit clears once done */
		return 0;
	case 0x0780: { /* RX_FIFO */
		if (S.rx_n == 0) {
			violation("read of an empty RX FIFO at tick %llu", (unsigned long long)S.now);
			return 0xdeadbeef;
		}
		uint32_t w = S.rx_fifo[0];
		memmove(S.rx_fifo, S.rx_fifo + 1, (size_t)(S.rx_n - 1) * sizeof S.rx_fifo[0]);
		memmove(S.rx_valid, S.rx_valid + 1, (size_t)(S.rx_n - 1));
		S.rx_n--;
		return w;
	}
	case 0x0804: { /* RX_FIFO_STATUS */
		uint32_t v = (uint32_t)S.rx_n;
		if (S.rx_n > 0) {
			int bpw = bytes_per_word(S.reg[0x0284 / 4]);
			int last = S.rx_valid[S.rx_n - 1];
			/* The last word is flagged once the stale timer or a full word ends it. */
			v |= RX_LAST | (uint32_t)(last == bpw ? 0 : last) << RX_LBV_SHIFT;
		}
		return v;
	}
	case 0x0800: /* TX_FIFO_STATUS */
		return (uint32_t)S.tx_n;
	case HW_PARAM_0:
	case HW_PARAM_1:
		return (uint32_t)DEPTH << 16;
	default:
		return S.reg[off / 4];
	}
}

/* The registers the driver may write: FreeBSD's init, param, putc and rx paths. */
static const uint32_t writable[] = {
	0x0020, 0x0024, 0x0028, 0x025c, 0x0254, 0x0258, 0x0260, 0x0264, 0x0268, 0x026c, 0x0270,
	0x0280, 0x0284, 0x0288, 0x028c, 0x0294, 0x02a4, 0x02a8, 0x0600, 0x0604, 0x0614, 0x0618,
	0x0630, 0x0634, 0x0644, 0x0648, 0x0700, 0x0810, 0x0814, 0x0c44, 0x0d44, 0x0e18, 0x0e1c,
	0x0e30,
};

static void
sim_write(uintptr_t base, uint32_t off, uint32_t v)
{
	(void)base;
	S.accesses++;
	tick();
	bool ok = false;
	for (size_t i = 0; i < sizeof writable / sizeof writable[0]; i++) {
		ok |= writable[i] == off;
	}
	if (!ok) {
		violation("write of 0x%x to register 0x%x, which the driver must not program", v, off);
	}
	switch (off) {
	case 0x0600: /* M_CMD0 */
		if (v >> 27 != 1) {
			violation("M_CMD0 opcode %u", v >> 27);
			break;
		}
		if (S.m_active) {
			violation("START_TX while a command is active");
		}
		S.commands++;
		if (S.reg[0x0270 / 4] != 1) {
			S.bad_lengths++;
		}
		S.trans_len = S.tx_left = S.reg[0x0270 / 4];
		S.m_active = S.tx_left != 0;
		S.tx_next = S.now + CHAR_TICKS;
		if (!S.m_active) {
			m_done(1u << 0);
		}
		break;
	case 0x0604: /* M_CMD_CTRL */
		if ((v & (1u << 2)) != 0 && !S.stuck) {
			S.tx_n = 0;
			m_done(1u << 4);
		}
		if ((v & (1u << 1)) != 0) {
			S.tx_n = 0;
			S.stuck = false;
			m_done(1u << 5);
		}
		break;
	case 0x0618: /* M_IRQ_CLEAR */
		S.reg[0x0610 / 4] &= ~v;
		break;
	case 0x0630: /* S_CMD0 */
		if (v >> 27 != 1) {
			violation("S_CMD0 opcode %u", v >> 27);
		}
		S.s_active = true;
		S.s_starts++;
		break;
	case 0x0634: /* S_CMD_CTRL */
		if ((v & (1u << 1)) != 0) {
			S.s_active = false;
		}
		break;
	case 0x0648: /* S_IRQ_CLEAR */
		break;
	case 0x0700: /* TX_FIFO */
		if (!S.m_active) {
			violation("TX FIFO word 0x%x with no transmit command", v);
			break;
		}
		if (S.tx_n == DEPTH) {
			violation("TX FIFO overrun");
			break;
		}
		if (S.tx_n + 1 > (int)S.tx_left) {
			violation("TX FIFO word beyond the command's length %u", S.trans_len);
		}
		if (S.tx_n == 0) {
			S.tx_next = S.now + CHAR_TICKS;
		}
		S.tx_fifo[S.tx_n++] = v;
		if (S.tx_n > S.tx_max) {
			S.tx_max = S.tx_n;
		}
		break;
	default:
		S.reg[off / 4] = v;
		break;
	}
}

/* UEFI's state at ExitBootServices: a UART engine, 4x8 packing, read command running. */
static void
sim_reset(void)
{
	memset(&S, 0, sizeof S);
	S.reg[0x0068 / 4] = PROTO_UART | 0x0003;
	S.reg[0x0260 / 4] = 0x0010e00e;  /* not 1x8: four characters per word */
	S.reg[0x0284 / 4] = 0x0010e00e;
	S.reg[0x0270 / 4] = 0;
	S.s_active = true;
}

/* Characters typed at the other end. */
static void
sim_type(const char *s)
{
	for (; *s != '\0'; s++) {
		S.rx_pending[S.rx_pending_n++] = (uint8_t)*s;
	}
	if (S.rx_next < S.now) {
		S.rx_next = S.now + CHAR_TICKS;
	}
}

/* Lets the model run without the driver (the other end keeps typing). */
static void
sim_idle(int ticks)
{
	while (ticks-- > 0) {
		tick();
	}
}

/* ---- The driver, compiled against the model ---- */

#define ND_GENI_READ(base, off)         sim_read((base), (off))
#define ND_GENI_WRITE(base, off, v)     sim_write((base), (off), (uint32_t)(v))
#define ND_GENI_POLL_LIMIT              2000u
#include "nd_geni_uart.h"

/* ---- Tests ---- */

static int failures;

#define CHECK(cond, ...)                                                   \
	do {                                                               \
		if (!(cond)) {                                             \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                      \
			fprintf(stderr, "\n");                             \
			failures++;                                        \
		}                                                          \
	} while (0)

#define CHECK_CLEAN()                                                      \
	CHECK(S.violations == 0, "%d protocol violation(s), the first: %s", S.violations, S.why)

static const struct nd_geni_uart U = { .base = 0x884000 };

static void
put(const char *s)
{
	for (; *s != '\0'; s++) {
		while (!nd_geni_uart_transmit_ready(&U)) {
		}
		nd_geni_uart_transmit(&U, (uint8_t)*s);
	}
}

/* Polls as serial_keyboard_poll does, up to `max` characters or `polls` empty polls. */
static int
get(char *buf, int max, int polls)
{
	int n = 0;
	while (n < max && polls > 0) {
		if (nd_geni_uart_receive_ready(&U)) {
			buf[n++] = (char)nd_geni_uart_receive(&U);
		} else {
			polls--;
		}
	}
	buf[n] = '\0';
	return n;
}

static void
test_probe_and_init(void)
{
	sim_reset();
	CHECK(nd_geni_uart_probe(&U), "a UART engine is not recognised");
	S.reg[0x0068 / 4] = (3u << 8);  /* I2C firmware */
	CHECK(!nd_geni_uart_probe(&U), "an I2C engine is taken for a UART");
	S.reg[0x0068 / 4] = PROTO_UART;

	nd_geni_uart_init(&U);
	CHECK_CLEAN();
	CHECK(S.reg[0x0260 / 4] == 0xf && S.reg[0x0284 / 4] == 0xf, "packing not 1x8: TX 0x%x RX 0x%x",
	    S.reg[0x0260 / 4], S.reg[0x0284 / 4]);
	CHECK(S.reg[0x0264 / 4] == 0 && S.reg[0x0288 / 4] == 0, "packing CFG1 not cleared");
	CHECK(S.reg[0x0614 / 4] == 0 && S.reg[0x0644 / 4] == 0 && S.reg[0x0e18 / 4] == 0, "an interrupt left enabled");
	CHECK((S.reg[0x0258 / 4] & 1) == 0, "DMA mode left on");
	CHECK(S.reg[0x0268 / 4] == 8 && S.reg[0x028c / 4] == 8, "word length not 8");
	CHECK(S.reg[0x026c / 4] == 0, "not one stop bit");
	CHECK(S.reg[0x025c / 4] == 2, "CTS not ignored");
	CHECK(S.s_active, "the read command is not running after init");
	CHECK(S.commands == 0 && S.out_n == 0, "init transmitted");
}

static void
test_transmit(void)
{
	sim_reset();
	nd_geni_uart_init(&U);
	const char *msg = "NeoDarwin on GENI\r\n";
	put(msg);
	CHECK_CLEAN();
	CHECK(S.out_n == (int)strlen(msg) && memcmp(S.out, msg, strlen(msg)) == 0, "sent \"%.*s\"", S.out_n, S.out);
	CHECK(S.commands == (int)strlen(msg), "%d commands for %zu characters", S.commands, strlen(msg));
	CHECK(S.bad_lengths == 0, "%d commands of length other than 1", S.bad_lengths);
	CHECK(S.tx_max <= 1, "TX FIFO held %d words", S.tx_max);
	CHECK(!S.m_active, "a command still running after the last character returned");

	/* Every byte value, and a long run. */
	sim_reset();
	nd_geni_uart_init(&U);
	for (int c = 0; c < 256; c++) {
		nd_geni_uart_transmit(&U, (uint8_t)c);
	}
	CHECK_CLEAN();
	bool all = S.out_n == 256;
	for (int c = 0; all && c < 256; c++) {
		all = S.out[c] == c;
	}
	CHECK(all, "the 256 byte values did not come out in order (%d sent)", S.out_n);
}

static void
test_transmit_after_uefi(void)
{
	/* UEFI's last command, three characters, still going out at handover. */
	sim_reset();
	S.reg[0x0270 / 4] = 3;
	sim_write(0, 0x0600, 1u << 27);
	sim_write(0, 0x0700, 'o' | 'k' << 8 | '\n' << 16);
	S.violations = 0;  /* the model's own "writes" above are UEFI's */
	nd_geni_uart_init(&U);
	put("A");
	CHECK_CLEAN();
	CHECK(S.out_n == 4 && memcmp(S.out, "ok\nA", 4) == 0, "UEFI's output was cut or reordered: \"%.*s\"", S.out_n, S.out);
}

static void
test_stuck_sequencer(void)
{
	/* A command that never progresses, and a stale CMD_DONE: cancelled, then ours runs. */
	sim_reset();
	nd_geni_uart_init(&U);
	S.m_active = true;
	S.tx_left = 5;
	S.reg[0x0610 / 4] |= 1;
	uint64_t before = S.accesses;
	put("x");
	CHECK_CLEAN();
	CHECK(S.out_n == 1 && S.out[0] == 'x', "no output after cancelling a stuck command");
	CHECK(S.accesses - before < 3 * ND_GENI_POLL_LIMIT, "waited %llu accesses", (unsigned long long)(S.accesses - before));

	/* An engine that ignores CANCEL: aborted. */
	sim_reset();
	nd_geni_uart_init(&U);
	S.m_active = true;
	S.stuck = true;
	S.tx_left = 5;
	put("y");
	CHECK_CLEAN();
	CHECK(S.out_n == 1 && S.out[0] == 'y', "no output after aborting a stuck command");
	CHECK((S.reg[0x0610 / 4] & (1u << 5)) == 0, "ABORT_DONE left set");

	/* A dead engine: every character returns after a bounded wait. */
	sim_reset();
	nd_geni_uart_init(&U);
	S.stuck = true;
	before = S.accesses;
	put("zz");
	CHECK(S.accesses - before < 2 * 4 * ND_GENI_POLL_LIMIT, "a dead engine held the console for %llu accesses",
	    (unsigned long long)(S.accesses - before));
}

static void
test_receive(void)
{
	char buf[64];

	/* Nothing typed: nothing read, and the FIFO is never read empty. */
	sim_reset();
	nd_geni_uart_init(&U);
	CHECK(get(buf, 8, 50) == 0, "read \"%s\" from an idle line", buf);
	CHECK_CLEAN();

	/* Characters, one per FIFO word; the last word flagged RX_LAST. */
	sim_type("root\r");
	int n = get(buf, 32, 200);
	CHECK_CLEAN();
	CHECK(n == 5 && strcmp(buf, "root\r") == 0, "read \"%s\"", buf);
	CHECK(S.rx_lost == 0, "%d characters lost", S.rx_lost);

	/* A burst that fills the FIFO before the driver polls. */
	sim_type("0123456789abcdef");
	sim_idle(16 * CHAR_TICKS + 1);
	CHECK(S.rx_n == 16, "the model holds %d words", S.rx_n);
	n = get(buf, 32, 50);
	CHECK_CLEAN();
	CHECK(n == 16 && strcmp(buf, "0123456789abcdef") == 0, "read \"%s\"", buf);

	/* Typed and output interleaved, as a shell echoes. */
	sim_type("ls");
	put("$ ");
	n = get(buf, 8, 100);
	CHECK_CLEAN();
	CHECK(n == 2 && strcmp(buf, "ls") == 0, "read \"%s\" while transmitting", buf);
}

static void
test_receive_after_uefi(void)
{
	char buf[64];

	/*
	 * Keys pressed while UEFI owned the port, packed four to a word with a
	 * partial last word (RX_LAST, RX_LAST_BYTE_VALID 2): drained by init,
	 * not read back as one character per word.
	 */
	sim_reset();
	sim_type("abcdef");
	sim_idle(7 * CHAR_TICKS);
	CHECK(S.rx_n == 2 && S.rx_valid[1] == 2, "the model packed %d words", S.rx_n);
	CHECK((sim_read(0, 0x0804) >> RX_LBV_SHIFT & 7) == 2 && (sim_read(0, 0x0804) & RX_LAST) != 0,
	    "the model's partial last word");
	nd_geni_uart_init(&U);
	CHECK_CLEAN();
	CHECK(get(buf, 8, 50) == 0, "read UEFI's leftovers \"%s\"", buf);

	/* After init, what is typed arrives whole. */
	sim_type("q");
	CHECK(get(buf, 8, 100) == 1 && buf[0] == 'q', "read \"%s\"", buf);
	CHECK_CLEAN();
}

static void
test_receive_restart(void)
{
	char buf[8];

	/* The read command ends (a break, or firmware): the next poll restarts it. */
	sim_reset();
	nd_geni_uart_init(&U);
	int starts = S.s_starts;
	S.s_active = false;
	CHECK(!nd_geni_uart_receive_ready(&U), "ready with an empty FIFO");
	CHECK(S.s_active && S.s_starts == starts + 1, "the read command was not restarted");
	sim_type("k");
	CHECK(get(buf, 4, 100) == 1 && buf[0] == 'k', "read \"%s\" after the restart", buf);
	CHECK_CLEAN();
}

int
main(void)
{
	test_probe_and_init();
	test_transmit();
	test_transmit_after_uefi();
	test_stuck_sequencer();
	test_receive();
	test_receive_after_uefi();
	test_receive_restart();
	if (failures != 0) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("PASS\n");
	return 0;
}
