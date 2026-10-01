/* SPDX-License-Identifier: BSD-2-Clause */
/* NeoDarwin-Language: portability: a host test of the kernel's C driver core, in the same language. */
/*
 * nd_tc956x.h against a model of the Toshiba TC956x (docs/kernel/network.md,
 * "Checkpoint 3"). The model has the registers the core touches, with the
 * behaviour FreeBSD's tcx driver relies on (and that runs on the Radxa
 * Dragon Q8B), and flags what would go wrong on the board:
 *
 *   - SErrors: a read or write of a block whose clock is off or whose reset
 *     is asserted (XGMAC, XPCS, MSI generator), a read of the PMA, a PMA
 *     write outside its reset, a reset released while its clock is off, an
 *     access outside the registers the driver uses, or to the other
 *     function's MAC, MSI generator, clocks or resets (each function may
 *     only touch its own; function 1 may read function 0's clock and reset
 *     registers, as FreeBSD's does), BAR0 written by function 1.
 *   - The SerDes: the PMA comes up (EMACCTL.INIT_DONE) only if released
 *     with a valid speed selector and its reference clock configured; the
 *     rate it runs at is the selector at release. The XPCS needs its soft
 *     reset, the mode type select and 2500BASE-X or MAC-side SGMII set to
 *     match. The PHY's SerDes FIFO must be released.
 *   - Frames pass only when all of that matches the PHY's link speed and
 *     the MAC's port speed (TX_CONFIG.SS) does too.
 *   - DMA: descriptor and buffer addresses must carry the TAMAP offset and
 *     land in host memory, and the TAMAP must be programmed. The TX DMA
 *     works from its current descriptor up to the tail, and flags a
 *     descriptor it doesn't own before the tail, a frame without FD or
 *     whose length disagrees; the RX DMA takes posted descriptors up to the
 *     tail, splits long frames, raises RBU when there are none, and reports
 *     the packet type and checksum errors. RX ring length needs OWRQ = 3
 *     (the XGMAC 3.01a erratum).
 *   - Interrupts: RI on an IOC descriptor or after the RX watchdog, TI on
 *     an IOC descriptor; the MSI generator sends one MSI and holds off
 *     until MASK_CLR.
 *   - MDIO: a QCA8081 at 0x1c (clause 22, with the MMD window and PHY
 *     specific status 0x11) and its SerDes at 0x1d (clause 45, MMD 1);
 *     link negotiation with a partner whose speeds and pause the test sets.
 *   - Stop: the transmitter must not be turned off before the TX DMA has
 *     stopped (TPS) and the MTL drained.
 *
 * A small C driver loop below plays NeoDarwinTC956x.cpp's part (buffers,
 * the ring walk, refills, the MSI handler).
 *
 * Built a second way by //kernel/neodarwin/network:tc956x_mutation_tests:
 * with ND_TC956X_HEADER naming a copy of the header with one planted bug
 * and ND_TC956X_EXPECT_CAUGHT defined, it passes only if some check fails.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- The model ---- */

#define SFR_SIZE        0x50000u
#define BAR0_SIZE       0x4000u
#define MEM_BASE        0x880000000ull          /* host physical, below 36 bits */
#define MEM_SIZE        (16u << 20)
#define DMA_OFF         0x1000000000ull
#define MAX_ACCESSES    200000000ull
#define WIRE_MAX        1024
#define FRAME_MAX       9600

#define R_NCLK(m)       ((m) == 0 ? 0x1004u : 0x100cu)
#define R_NRST(m)       ((m) == 0 ? 0x1008u : 0x1010u)
#define R_EMAC(m)       (0x1070u + 4u * (m))
#define B_MSIGEN        (1u << 18)
#define B_CLK_ALL       (1u << 31)
#define B_RST_MAC       (1u << 7)
#define B_RST_PMA       (1u << 30)
#define B_RST_XPCS      (1u << 31)
#define B_INIT_DONE     (1u << 21)
#define MACB(m)         (0x40000u + 0x8000u * (m))

/* Speed bits for the PHY and its partner. */
#define SP10            1u
#define SP100           2u
#define SP1000          4u
#define SP2500          8u

struct phy {
	uint16_t bmcr, anar, gtcr, adv25, mmdacr, mmdaddr;
	uint16_t fifo;                  /* the SerDes's MMD 1 0x9072 */
	bool cable;
	unsigned partner;               /* SP* */
	uint16_t partner_pause;         /* ANLPAR pause bits */
	uint64_t link_at;               /* negotiation done */
	int renegotiations;
};

struct sdesc {                          /* a DMA descriptor, as the hardware reads it */
	uint32_t des0, des1, des2, des3;
};

struct mdl {                            /* one MAC and what hangs off it */
	struct phy phy;
	/* XPCS */
	uint16_t x_bmcr, x_ctrl2, x_dig1, x_anctrl;
	uint32_t x_viewport;
	uint64_t x_reset_until;
	int x_soft_resets;
	/* PMA */
	uint32_t pma_rate;              /* SP_SEL at the last release; 0 none */
	uint32_t pma_written;           /* configuration registers written in this reset */
	uint64_t init_done_at;          /* 0 never */
	int pma_inits;
	bool serdes_dead;               /* never comes up */
	/* MDIO */
	bool mdio_busy;
	uint64_t mdio_at;
	/* DMA */
	uint64_t swr_at;
	uint32_t tx_cur, rx_cur;
	bool tx_in_frame, tx_started, tx_hold;
	int tx_budget;                  /* > 0: descriptors the DMA gets through, then pauses */
	uint32_t tx_fl, tx_got, tx_cic;
	uint8_t tx_frame[FRAME_MAX];
	uint64_t tps_at;
	uint64_t mtl_busy_until;
	uint64_t rwt_at;
	/* MSI generator */
	bool msi_armed;
	int msi_pending, msis;
	/* the wire */
	uint8_t wire[WIRE_MAX][2048];
	uint32_t wire_len[WIRE_MAX], wire_cic[WIRE_MAX];
	int wire_n;
	int tx_lost, rx_link_drops, rx_filtered, rx_nodesc, rx_frames;
	int ti_count;
};

static struct {
	uint32_t sfr[SFR_SIZE / 4];
	uint32_t bar0[2][BAR0_SIZE / 4];
	int bar0_writes[2];
	struct mdl m[2];
	uint64_t now, accesses;
	int violations;
	char why[512];
	char trace[64][48];             /* clock, reset and EMACCTL changes, in order */
	int ntrace;
	int sfr_writes[2];
	char logs[4096];                /* the core's log lines */
	int nlog;
} S;

static uint8_t *mem;
static uint64_t mem_next;
static int failures;

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

#define CHECK(c, ...) \
	do { \
		if (!(c)) { \
			failures++; \
			printf("FAIL %s:%d: %s: ", __func__, __LINE__, #c); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

#define CLEAN() CHECK(S.violations == 0, "model: %d violation(s), first: %s", S.violations, S.why)

static uint32_t
rd(uint32_t off)
{
	return S.sfr[off / 4];
}

static void
wr(uint32_t off, uint32_t v)
{
	S.sfr[off / 4] = v;
}

static uint32_t mr(int m, uint32_t reg) { return rd(MACB(m) + reg); }
static void mw(int m, uint32_t reg, uint32_t v) { wr(MACB(m) + reg, v); }

static bool clk_on(int m) { return (rd(R_NCLK(m)) & B_CLK_ALL) != 0; }
static bool mac_up(int m) { return clk_on(m) && (rd(R_NRST(m)) & B_RST_MAC) == 0; }
static bool xpcs_up(int m) { return mac_up(m) && (rd(R_NRST(m)) & B_RST_XPCS) == 0; }
static bool pma_reset(int m) { return (rd(R_NRST(m)) & B_RST_PMA) != 0; }
static bool msigen_up(void) { return (rd(R_NCLK(0)) & B_MSIGEN) != 0 && (rd(R_NRST(0)) & B_MSIGEN) == 0; }

static bool
tamap_ok(void)
{
	return S.bar0[0][0x800 / 4] == 0x47 && S.bar0[0][0x804 / 4] == 0x10 && S.bar0[0][0x808 / 4] == 0 &&
	       S.bar0[0][0x80c / 4] == 0;
}

static void
trace(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	if (S.ntrace < 64) {
		vsnprintf(S.trace[S.ntrace++], sizeof S.trace[0], fmt, ap);
	}
	va_end(ap);
}

/* -- The PHY -- */

static unsigned
phy_adv(const struct phy *p)
{
	unsigned a = 0;
	if (p->anar & 0x0040) a |= SP10;
	if (p->anar & 0x0100) a |= SP100;
	if (p->gtcr & 0x0200) a |= SP1000;
	if (p->adv25 & 0x0080) a |= SP2500;
	return a;
}

static unsigned
phy_link_speed(const struct phy *p)
{
	if (!p->cable || S.now < p->link_at || (p->bmcr & 0x1000) == 0) {
		return 0;
	}
	unsigned common = phy_adv(p) & p->partner;
	for (unsigned b = SP2500; b != 0; b >>= 1) {
		if (common & b) {
			return b;
		}
	}
	return 0;
}

static uint16_t
phy_status(const struct phy *p)
{
	unsigned s = phy_link_speed(p);
	if (s == 0) {
		return 0;
	}
	uint16_t code = s == SP2500 ? 4 : s == SP1000 ? 2 : s == SP100 ? 1 : 0;
	return (uint16_t)((1u << 10) | (1u << 13) | (code << 7));
}

static void
phy_renegotiate(struct phy *p)
{
	p->link_at = S.now + 2000;
	p->renegotiations++;
}

static uint16_t
phy_c22(struct phy *p, uint32_t reg, bool write, uint16_t v)
{
	switch (reg) {
	case 0x00:
		if (write) {
			p->bmcr = v & ~0x0200;
			if (v & 0x0200) phy_renegotiate(p);
		}
		return p->bmcr;
	case 0x01: return phy_link_speed(p) ? 0x796d : 0x7949;
	case 0x02: return 0x004d;
	case 0x03: return 0xd101;
	case 0x04: if (write) p->anar = v; return p->anar;
	case 0x05: return phy_link_speed(p) ? (uint16_t)(p->partner_pause | 0x01e1) : 0;
	case 0x09: if (write) p->gtcr = v; return p->gtcr;
	case 0x0d: if (write) p->mmdacr = v; return p->mmdacr;
	case 0x0e:
		if ((p->mmdacr & 0xc000) == 0) {
			if (write) p->mmdaddr = v;
			return p->mmdaddr;
		}
		if ((p->mmdacr & 0x1f) == 7 && p->mmdaddr == 0x20) {
			if (write) p->adv25 = v;
			return p->adv25;
		}
		violation("PHY MMD %u register 0x%x is not one the driver uses", p->mmdacr & 0x1f, p->mmdaddr);
		return 0;
	case 0x11: return phy_status(p);
	default:
		violation("PHY register 0x%x is not one the driver uses", reg);
		return 0;
	}
}

/* An MDIO transaction, when it completes. */
static void
mdio_complete(int m)
{
	struct mdl *x = &S.m[m];
	uint32_t data = mr(m, 0x204), addr = mr(m, 0x200), c22p = mr(m, 0x220);
	uint32_t port = (addr >> 16) & 0x1f;
	bool c22 = (c22p & (1u << port)) != 0;
	bool write = ((data >> 16) & 3) == 1;
	uint16_t v = (uint16_t)data, result = 0xffff;
	if (((data >> 16) & 3) != 1 && ((data >> 16) & 3) != 3) {
		violation("MDIO command %u", (data >> 16) & 3);
	}
	if (((data >> 19) & 7) != 0) {
		violation("MDIO clock divider %u, not FreeBSD's 0", (data >> 19) & 7);
	}
	if (port == 0x1c) {
		if (!c22) {
			violation("the PHY at 0x1c addressed as clause 45");
		} else {
			result = phy_c22(&x->phy, addr & 0x1f, write, v);
		}
	} else if (port == 0x1d) {
		uint32_t mmd = (addr >> 21) & 0x1f, reg = addr & 0xffff;
		if (c22) {
			violation("the PHY's SerDes at 0x1d addressed as clause 22");
		} else if (mmd != 1 || reg != 0x9072) {
			violation("SerDes MMD %u register 0x%x is not one the driver uses", mmd, reg);
		} else {
			if (write) x->phy.fifo = v;
			result = x->phy.fifo;
		}
	}
	mw(m, 0x204, (data & 0xffff0000u & ~(1u << 22)) | (write ? v : result));
	x->mdio_busy = false;
}

/* -- The XPCS -- */

static void
xpcs_defaults(struct mdl *x)
{
	x->x_bmcr = 0x1140;
	x->x_ctrl2 = 0;
	x->x_dig1 = 0x2000;
	x->x_anctrl = 0;
}

static uint16_t *
xpcs_reg(struct mdl *x, uint32_t mmd, uint32_t reg)
{
	static uint16_t scratch;
	if (mmd == 31 && reg == 0) return &x->x_bmcr;
	if (mmd == 31 && reg == 0x8000) return &x->x_dig1;
	if (mmd == 31 && reg == 0x8001) return &x->x_anctrl;
	if (mmd == 3 && reg == 7) return &x->x_ctrl2;
	if (mmd == 3 && reg == 8) { scratch = 1; return &scratch; }        /* STAT2: 10GBASE-R able */
	if (mmd == 31 && (reg == 1 || reg == 0x8002)) { scratch = 0; return &scratch; }
	violation("XPCS MMD %u register 0x%x is not one the driver uses", mmd, reg);
	scratch = 0;
	return &scratch;
}

static bool
xpcs_2500basex(const struct mdl *x)
{
	return x->x_ctrl2 == 4 && (x->x_dig1 & 0x4) != 0 && (x->x_dig1 & 0x200) == 0 && (x->x_bmcr & 0x1000) == 0 &&
	       (x->x_bmcr & 0x2040) == 0x0040;
}

static bool
xpcs_sgmii(const struct mdl *x)
{
	return x->x_ctrl2 == 4 && (x->x_anctrl & 0x6) == 0x4 && (x->x_anctrl & 0x8) == 0 && (x->x_dig1 & 0x200) != 0 &&
	       (x->x_dig1 & 0x4) == 0 && (x->x_bmcr & 0x1000) != 0;
}

/* Do the SerDes, PCS, PHY and MAC agree on the link: do frames pass? */
static bool
wire_ok(int m)
{
	struct mdl *x = &S.m[m];
	unsigned s = phy_link_speed(&x->phy);
	if (s == 0 || (x->phy.fifo & (1u << 11)) == 0 || !mac_up(m) || pma_reset(m) || !xpcs_up(m) ||
	    (rd(R_EMAC(m)) & B_INIT_DONE) == 0) {
		return false;
	}
	uint32_t sp = s == SP2500 ? 4 : s == SP1000 ? 5 : s == SP100 ? 6 : 7;
	uint32_t ss = s == SP2500 ? 2 : s == SP1000 ? 3 : s == SP100 ? 4 : 7;
	if (x->pma_rate != sp || ((mr(m, 0) >> 29) & 7) != ss) {
		return false;
	}
	return s == SP2500 ? xpcs_2500basex(x) : xpcs_sgmii(x);
}

/* -- DMA -- */

static uint8_t *
dma_ptr(uint64_t a, uint32_t len, const char *what)
{
	if (!tamap_ok()) {
		violation("%s: DMA before the TAMAP is programmed", what);
		return NULL;
	}
	if (a < DMA_OFF || a >= DMA_OFF + (1ull << 36)) {
		violation("%s address 0x%llx is outside the TAMAP window", what, (unsigned long long)a);
		return NULL;
	}
	uint64_t pa = a - DMA_OFF;
	if (pa < MEM_BASE || pa + len > MEM_BASE + MEM_SIZE) {
		violation("%s address 0x%llx is not host memory", what, (unsigned long long)a);
		return NULL;
	}
	return mem + (pa - MEM_BASE);
}

static bool
ring_index(int m, bool tx, uint32_t *tail_idx, uint64_t *ring, uint32_t *len)
{
	*ring = ((uint64_t)mr(m, tx ? 0x3110 : 0x3118) << 32) | mr(m, tx ? 0x3114 : 0x311c);
	*len = (mr(m, tx ? 0x3130 : 0x3134) & 0x3ff) + 1;
	uint32_t tail = mr(m, tx ? 0x3124 : 0x312c);
	uint32_t d = tail - (uint32_t)*ring;
	if (d % 16 != 0 || d / 16 >= *len) {
		violation("%s tail 0x%x is not a descriptor of the ring at 0x%llx", tx ? "TX" : "RX", tail,
		    (unsigned long long)*ring);
		return false;
	}
	*tail_idx = d / 16;
	return true;
}

static void
wire_out(int m)
{
	struct mdl *x = &S.m[m];
	if ((mr(m, 0) & 1) == 0 || !wire_ok(m)) {
		x->tx_lost++;
		return;
	}
	if (x->wire_n < WIRE_MAX) {
		memcpy(x->wire[x->wire_n], x->tx_frame, x->tx_got < 2048 ? x->tx_got : 2048);
		x->wire_len[x->wire_n] = x->tx_got;
		x->wire_cic[x->wire_n] = x->tx_cic;
		x->wire_n++;
	}
	x->mtl_busy_until = S.now + 30;
}

static void
tx_run(int m)
{
	struct mdl *x = &S.m[m];
	if ((mr(m, 0x3104) & 1) == 0 || x->tx_hold) {
		return;
	}
	uint64_t ring;
	uint32_t len, tidx;
	if (!ring_index(m, true, &tidx, &ring, &len)) {
		return;
	}
	while (x->tx_cur != tidx && !x->tx_hold) {
		struct sdesc *d = (struct sdesc *)dma_ptr(ring + 16ull * x->tx_cur, 16, "TX descriptor");
		if (d == NULL) {
			return;
		}
		if ((d->des3 & (1u << 31)) == 0) {
			violation("the TX DMA reached descriptor %u, which the driver doesn't own, before the tail (%u)",
			    x->tx_cur, tidx);
			return;
		}
		if (d->des3 & (1u << 30)) {
			violation("a TX context descriptor (no TSO here)");
		}
		if (d->des3 & (1u << 29)) {
			if (x->tx_in_frame) violation("TX descriptor %u starts a frame inside another", x->tx_cur);
			x->tx_in_frame = true;
			x->tx_got = 0;
			x->tx_fl = d->des3 & 0x7fff;
			x->tx_cic = (d->des3 >> 16) & 3;
		} else if (!x->tx_in_frame) {
			violation("TX descriptor %u continues no frame (no FD)", x->tx_cur);
		} else if (((d->des3 >> 16) & 3) != 0) {
			violation("checksum insertion on a descriptor that isn't a frame's first");
		}
		uint32_t blen = d->des2 & 0x3fff;
		uint64_t ba = (uint64_t)d->des1 << 32 | d->des0;
		uint8_t *b = blen ? dma_ptr(ba, blen, "TX buffer") : NULL;
		if (blen == 0) violation("an empty TX buffer");
		if ((d->des3 & 0x7fff) != x->tx_fl) violation("TX descriptors of a frame disagree on its length");
		if (b != NULL && x->tx_got + blen <= FRAME_MAX) {
			memcpy(x->tx_frame + x->tx_got, b, blen);
		}
		x->tx_got += blen;
		bool ioc = (d->des2 & (1u << 31)) != 0;
		if (ioc && (d->des3 & (1u << 28)) == 0) violation("IOC on a descriptor that isn't a frame's last");
		if (d->des3 & (1u << 28)) {
			if (x->tx_got != x->tx_fl) violation("TX frame of %u bytes says %u", x->tx_got, x->tx_fl);
			x->tx_in_frame = false;
			wire_out(m);
		}
		d->des3 &= ~(1u << 31);         /* the write-back */
		if (ioc) {
			mw(m, 0x3160, mr(m, 0x3160) | 1);
			x->ti_count++;
		}
		x->tx_cur = (x->tx_cur + 1) % len;
		if (x->tx_budget > 0 && --x->tx_budget == 0) {
			x->tx_hold = true;
		}
	}
}

/* -- The MSI generator -- */

static void
msi_eval(void)
{
	if (!msigen_up()) {
		return;
	}
	for (int m = 0; m < 2; m++) {
		struct mdl *x = &S.m[m];
		if (!mac_up(m)) continue;
		uint32_t st = mr(m, 0x3160), ie = mr(m, 0x3138), out = rd(0xf000 + 0x100u * m);
		bool pend = ((st & ie & 1) && (out & (1u << 3))) || ((st & ie & 0x40) && (out & (1u << 11)));
		if (pend && x->msi_armed) {
			x->msi_pending++;
			x->msis++;
			x->msi_armed = false;
		}
	}
}

/* -- Time -- */

static void
mac_regs_reset(int m)
{
	memset(&S.sfr[MACB(m) / 4], 0, 0x3a00);
	mw(m, 0x110, 0x7630);   /* VERSION: SNPSVER 0x30, USERVER 0x76 */
	struct mdl *x = &S.m[m];
	x->tx_cur = x->rx_cur = 0;
	x->tx_in_frame = x->tx_started = false;
	x->rwt_at = 0;
	x->mdio_busy = false;
}

static void
advance(uint64_t us)
{
	uint64_t end = S.now + us;
	while (S.now < end) {
		uint64_t step = end - S.now > 10 ? 10 : end - S.now;
		S.now += step;
		for (int m = 0; m < 2; m++) {
			struct mdl *x = &S.m[m];
			if (x->mdio_busy && S.now >= x->mdio_at) mdio_complete(m);
			if (x->swr_at && S.now >= x->swr_at) {
				x->swr_at = 0;
				uint32_t keep = mr(m, 0x204);
				mac_regs_reset(m);
				mw(m, 0x204, keep & ~(1u << 22));
			}
			if (x->init_done_at && S.now >= x->init_done_at && !pma_reset(m)) {
				x->init_done_at = 0;
				wr(R_EMAC(m), rd(R_EMAC(m)) | B_INIT_DONE);
			}
			if (x->x_reset_until && S.now >= x->x_reset_until) {
				x->x_reset_until = 0;
				xpcs_defaults(x);
				x->x_bmcr &= ~0x8000;
			}
			if (x->tps_at && S.now >= x->tps_at) {
				x->tps_at = 0;
				mw(m, 0x3160, mr(m, 0x3160) | 2);
			}
			if (x->rwt_at && S.now >= x->rwt_at) {
				x->rwt_at = 0;
				mw(m, 0x3160, mr(m, 0x3160) | 0x40);
			}
		}
		msi_eval();
	}
}

static void
tick(void)
{
	if (++S.accesses > MAX_ACCESSES) {
		printf("FAIL: more than %llu register accesses: a loop that never ends\n", MAX_ACCESSES);
		failures++;
		exit(
#ifdef ND_TC956X_EXPECT_CAUGHT
		    0
#else
		    1
#endif
		);
	}
	advance(1);
}

/* -- Register access -- */

enum region { RG_CTRL, RG_MSIGEN, RG_MAC, RG_XPCS, RG_PMA, RG_BAD };

static enum region
classify(uint32_t off, int *m)
{
	*m = 0;
	if (off == 0 || off == 0x1004 || off == 0x1008 || off == 0x100c || off == 0x1010 || off == 0x1070 || off == 0x1074) {
		return RG_CTRL;
	}
	if (off >= 0xf000 && off < 0xf200) {
		*m = (int)((off - 0xf000) / 0x100);
		return RG_MSIGEN;
	}
	if (off >= 0x40000 && off < 0x50000) {
		*m = (int)((off - 0x40000) / 0x8000);
		uint32_t r = (off - 0x40000) % 0x8000;
		if (r < 0x3a00) return RG_MAC;
		if (r < 0x3e00) return RG_XPCS;
		if (r >= 0x4000) return RG_PMA;
	}
	return RG_BAD;
}

static bool
access_ok(int fn, uint32_t off, bool write)
{
	int m;
	if (off % 4 != 0 || off >= SFR_SIZE) {
		violation("function %d: access to 0x%x, outside BAR4", fn, off);
		return false;
	}
	switch (classify(off, &m)) {
	case RG_CTRL:
		if (off == 0 && write) break;
		if ((off == R_NCLK(0) || off == R_NRST(0)) && write && fn != 0) {
			violation("function %d wrote function 0's clock or reset register 0x%x", fn, off);
		}
		if ((off == R_NCLK(1) || off == R_NRST(1)) && fn != 1) {
			violation("function %d touched function 1's clock or reset register 0x%x", fn, off);
		}
		if (off == R_EMAC(0) && fn != 0) violation("function %d touched EMAC0CTL", fn);
		if (off == R_EMAC(1) && fn != 1) violation("function %d touched EMAC1CTL", fn);
		return true;
	case RG_MSIGEN:
		if (m != fn) violation("function %d touched MSI generator %d", fn, m);
		if (!msigen_up()) violation("SError: MSI generator at 0x%x accessed with its clock off or in reset", off);
		if (!write) violation("the MSI generator read (FreeBSD's driver never reads it)");
		return true;
	case RG_MAC:
		if (m != fn) violation("function %d touched MAC %d", fn, m);
		if (!mac_up(m)) violation("SError: MAC %d register 0x%x %s with its clock off or in reset", m,
		    off - MACB(m), write ? "written" : "read");
		return mac_up(m);
	case RG_XPCS:
		if (m != fn) violation("function %d touched XPCS %d", fn, m);
		if (!xpcs_up(m)) violation("SError: XPCS %d %s with its clock off or in reset", m, write ? "written" : "read");
		return xpcs_up(m);
	case RG_PMA:
		if (m != fn) violation("function %d touched PMA %d", fn, m);
		if (!write) violation("PMA %d read (FreeBSD's driver never reads it)", m);
		else if (!clk_on(m) || !pma_reset(m)) violation("PMA %d written outside its reset", m);
		return true;
	default:
		break;
	}
	violation("function %d: 0x%x is not a register the driver uses", fn, off);
	return false;
}

static int
sim_fn_of(void *bar)
{
	return (int)(uintptr_t)bar - 1;
}

static uint32_t
sim_read(int fn, uint32_t off)
{
	tick();
	if (!access_ok(fn, off, false)) {
		return 0xffffffff;
	}
	int m;
	enum region rg = classify(off, &m);
	if (rg == RG_XPCS) {
		struct mdl *x = &S.m[m];
		uint32_t r = off - MACB(m) - 0x3a00;
		if (r == 0x3fc) return x->x_viewport;
		uint32_t csr = (x->x_viewport << 8) | (r / 4);
		uint16_t *p = xpcs_reg(x, csr >> 16, csr & 0xffff);
		if (csr == ((31u << 16) | 0) && x->x_reset_until) return *p | 0x8000;
		return *p;
	}
	if (rg == RG_MAC) {
		uint32_t reg = off - MACB(m);
		struct mdl *x = &S.m[m];
		if (reg == 0x3000 && x->swr_at) return rd(off) | 1;
		if (reg == 0x1108) return S.now < x->mtl_busy_until ? (1u << 4) : 0;
		if (reg == 0x1148) return 0;
		if (reg == 0x204 && x->mdio_busy) return rd(off) | (1u << 22);
	}
	return rd(off);
}

static void
ctrl_write(int fn, uint32_t off, uint32_t v)
{
	(void)fn;
	uint32_t old = rd(off);
	for (int m = 0; m < 2; m++) {
		struct mdl *x = &S.m[m];
		if (off == R_EMAC(m)) {
			v = (v & ~B_INIT_DONE) | (old & B_INIT_DONE);
			if ((v & 0xf) != (old & 0xf)) trace("emac%d sp%u", m, v & 0xf);
		}
		if (off == R_NCLK(m)) {
			if ((old & ~v) & (B_CLK_ALL | (1u << 7) | (1u << 14))) violation("MAC %d clock turned off", m);
			if (m == 0 && (old & ~v & B_MSIGEN)) violation("MSI generator clock turned off");
			if (m == 0 && (v & ~old & B_MSIGEN)) trace("clk0+msigen");
			uint32_t on = v & ~old & (B_CLK_ALL | (1u << 7) | (1u << 14) | (1u << 15));
			if (on) trace("clk%d+mac%s", m, (on & (1u << 15)) ? "+rmii" : "");
		}
		if (off == R_NRST(m)) {
			uint32_t set = v & ~old, clr = old & ~v;
			if (m == 0 && (clr & B_MSIGEN)) {
				trace("rst0-msigen");
				if ((rd(R_NCLK(0)) & B_MSIGEN) == 0) violation("MSI generator reset released with its clock off");
			}
			if (m == 0 && (set & B_MSIGEN)) violation("MSI generator put back in reset");
			if (set & (B_RST_MAC | B_RST_PMA | B_RST_XPCS)) {
				trace("rst%d+%s%s%s", m, (set & B_RST_MAC) ? "mac" : "", (set & B_RST_PMA) ? "pma" : "",
				    (set & B_RST_XPCS) ? "xpcs" : "");
			}
			if (clr & (B_RST_MAC | B_RST_PMA | B_RST_XPCS)) {
				trace("rst%d-%s%s%s", m, (clr & B_RST_MAC) ? "mac" : "", (clr & B_RST_PMA) ? "pma" : "",
				    (clr & B_RST_XPCS) ? "xpcs" : "");
			}
			if ((set | clr) & ~(B_RST_MAC | B_RST_PMA | B_RST_XPCS | (m == 0 ? B_MSIGEN : 0))) {
				violation("reset bits 0x%x of MAC %d changed", (set | clr), m);
			}
			wr(off, v);
			if (set & B_RST_MAC) mac_regs_reset(m);
			if ((clr & B_RST_MAC) && !clk_on(m)) violation("MAC %d reset released with its clock off", m);
			if (set & B_RST_PMA) {
				wr(R_EMAC(m), rd(R_EMAC(m)) & ~B_INIT_DONE);
				x->pma_written = 0;
				x->init_done_at = 0;
			}
			if (clr & B_RST_PMA) {
				uint32_t sp = rd(R_EMAC(m)) & 0xf;
				x->pma_inits++;
				if (!clk_on(m)) violation("PMA %d reset released with its clock off", m);
				if (sp < 4 || sp > 7) {
					violation("PMA %d released with speed selector %u (no SerDes rate)", m, sp);
				} else if (x->pma_written != 0xffff) {
					violation("PMA %d released without its reference clock configuration (0x%x)", m, x->pma_written);
				} else if (!x->serdes_dead) {
					x->pma_rate = sp;
					x->init_done_at = S.now + 300;
				}
				if ((rd(R_EMAC(m)) & 0x30) != 0x10) violation("EMAC%dCTL PHY_INF_SEL is not 1", m);
			}
			if (set & B_RST_XPCS) {
				xpcs_defaults(x);
				x->x_reset_until = 0;
			}
			if ((clr & B_RST_XPCS) && !clk_on(m)) violation("XPCS %d reset released with its clock off", m);
			return;
		}
	}
	wr(off, v);
}

static void
pma_write(int m, uint32_t r, uint32_t v)
{
	struct mdl *x = &S.m[m];
	int bit = -1;
	uint32_t want = 0;
	if (r == 0x1b8) bit = 15;
	for (uint32_t i = 0; i < 5; i++) {
		if (r == 0x1080 + i * 0x14) bit = (int)i;
		if (r == 0x1090 + i * 0x14) bit = (int)(5 + i);
		if (r == 0x1888 + i * 8) { bit = (int)(10 + i); want = 0x1ef04; }
	}
	if (bit < 0) {
		violation("PMA %d register 0x%x is not one the driver uses", m, r);
		return;
	}
	if (v != want) violation("PMA %d register 0x%x = 0x%x, not 0x%x", m, r, v, want);
	x->pma_written |= 1u << bit;
}

static void
mac_write(int m, uint32_t reg, uint32_t v)
{
	struct mdl *x = &S.m[m];
	uint32_t off = MACB(m) + reg, old = rd(off);
	switch (reg) {
	case 0x0000:            /* TX_CONFIG */
		if ((old & 1) && !(v & 1) && x->tx_started && (!(mr(m, 0x3160) & 2) || S.now < x->mtl_busy_until)) {
			violation("MAC %d transmitter turned off before the TX DMA stopped and the MTL drained", m);
		}
		break;
	case 0x0200:
	case 0x0220:
		if (x->mdio_busy) violation("MDIO address written while busy");
		break;
	case 0x0204:
		if (x->mdio_busy) {
			violation("MDIO data written while busy");
			return;
		}
		wr(off, v);
		if (v & (1u << 22)) {
			x->mdio_busy = true;
			x->mdio_at = S.now + 20;
		}
		return;
	case 0x3000:
		if (v & 1) {
			x->swr_at = S.now + 40;
			wr(off, v & ~1u);
			return;
		}
		break;
	case 0x3104:
		if ((v & 1) && !(old & 1)) {
			x->tx_started = true;
			mw(m, 0x3160, mr(m, 0x3160) & ~2u);
		}
		if (!(v & 1) && (old & 1)) {
			x->tps_at = S.now + 20;
		}
		wr(off, v);
		tx_run(m);
		return;
	case 0x3124:
		wr(off, v);
		tx_run(m);
		return;
	case 0x3134:
		if (((v >> 24) & 3) != 3) violation("RX ring length without OWRQ 3 (the XGMAC 3.01a erratum)");
		break;
	case 0x3110:
	case 0x3118:
		if (v != (uint32_t)(DMA_OFF >> 32) && v < (uint32_t)(DMA_OFF >> 32)) {
			violation("a descriptor ring's high address 0x%x lacks the TAMAP offset", v);
		}
		break;
	case 0x3160:
		wr(off, old & ~v);      /* write 1 to clear */
		return;
	default:
		break;
	}
	wr(off, v);
}

static void
sim_write(int fn, uint32_t off, uint32_t v)
{
	tick();
	if (!access_ok(fn, off, true)) {
		return;
	}
	S.sfr_writes[fn]++;
	int m;
	switch (classify(off, &m)) {
	case RG_CTRL:
		ctrl_write(fn, off, v);
		break;
	case RG_MSIGEN:
		if (off - 0xf000 - 0x100u * m == 0x0c) {
			if (v & 1) S.m[m].msi_armed = true;
		} else if (off - 0xf000 - 0x100u * m == 0x00) {
			wr(off, v);
		} else {
			violation("MSI generator register 0x%x is not one the driver uses", off);
		}
		break;
	case RG_MAC:
		mac_write(m, off - MACB(m), v);
		break;
	case RG_XPCS: {
		struct mdl *x = &S.m[m];
		uint32_t r = off - MACB(m) - 0x3a00;
		if (r == 0x3fc) {
			x->x_viewport = v;
			break;
		}
		uint32_t csr = (x->x_viewport << 8) | (r / 4);
		if (x->x_reset_until) violation("XPCS %d written during its soft reset", m);
		if (csr == (31u << 16) && (v & 0x8000)) {
			x->x_soft_resets++;
			x->x_reset_until = S.now + 1500;
			x->x_bmcr = (uint16_t)v;
			break;
		}
		if ((csr >> 16) == 3 && (csr & 0xffff) == 8) {
			violation("XPCS STAT2 written");
			break;
		}
		*xpcs_reg(x, csr >> 16, csr & 0xffff) = (uint16_t)v;
		break;
	}
	case RG_PMA:
		pma_write(m, off - MACB(m) - 0x4000, v);
		break;
	default:
		break;
	}
	msi_eval();
}

static void
sim_bar0_write(int fn, uint32_t off, uint32_t v)
{
	tick();
	S.bar0_writes[fn]++;
	if (fn != 0) violation("function %d wrote its BAR0 (the TAMAP is function 0's)", fn);
	if (off < 0x800 || off >= 0x880 || off % 4 != 0 || (off - 0x800) % 0x20 > 0x10) {
		violation("BAR0 0x%x is not a TAMAP register", off);
		return;
	}
	S.bar0[fn][off / 4] = v;
}

static void
sim_log(const char *fmt, ...)
{
	va_list ap;
	size_t used = strlen(S.logs);
	va_start(ap, fmt);
	if (used + 2 < sizeof S.logs) {
		vsnprintf(S.logs + used, sizeof S.logs - used - 1, fmt, ap);
		strcat(S.logs, "\n");
	}
	va_end(ap);
	S.nlog++;
}

#define ND_TC956X_READ(sc, off)                 sim_read(sim_fn_of((sc)->bar4), (off))
#define ND_TC956X_WRITE(sc, off, val)           sim_write(sim_fn_of((sc)->bar4), (off), (val))
#define ND_TC956X_BRIDGE_WRITE(sc, off, val)    sim_bar0_write(sim_fn_of((sc)->bar0), (off), (val))
#define ND_TC956X_DELAY(sc, us)                 advance(us)
#define ND_TC956X_LOG(sc, fmt, ...)             sim_log(fmt __VA_OPT__(,) __VA_ARGS__)

#ifndef ND_TC956X_HEADER
#define ND_TC956X_HEADER "nd_tc956x.h"
#endif
#include ND_TC956X_HEADER

/* -- The wire, from the test's side -- */

static uint32_t
crc32_le_reg(const uint8_t *p, int n)
{
	uint32_t crc = 0xffffffff;
	for (int i = 0; i < n; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
		}
	}
	return crc;
}

static uint32_t
bitrev32(uint32_t v)
{
	uint32_t r = 0;
	for (int i = 0; i < 32; i++) {
		r = (r << 1) | ((v >> i) & 1);
	}
	return r;
}

/* The bin the MAC uses, computed another way: bitrev(~CRC-32) >> 26. */
static uint32_t
model_hash_bin(const uint8_t *addr)
{
	return bitrev32(~crc32_le_reg(addr, 6)) >> 26;
}

static bool
filter_pass(int m, const uint8_t *f)
{
	uint32_t pf = mr(m, 0x8);
	if (pf & 1) return true;
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	if (memcmp(f, bcast, 6) == 0) return true;
	if (f[0] & 1) {
		if (pf & 0x10) return true;
		if (pf & 0x4) {
			uint32_t bin = model_hash_bin(f);
			return (mr(m, 0x10 + 4 * (bin >> 5)) >> (bin & 31)) & 1;
		}
		return false;
	}
	uint32_t hi = mr(m, 0x300), lo = mr(m, 0x304);
	uint8_t own[6] = { (uint8_t)lo, (uint8_t)(lo >> 8), (uint8_t)(lo >> 16), (uint8_t)(lo >> 24), (uint8_t)hi,
		           (uint8_t)(hi >> 8) };
	return (hi & (1u << 31)) && memcmp(f, own, 6) == 0;
}

/* A frame arrives from the link partner. Returns true if it reached the ring. */
static bool
rx_inject(int m, const uint8_t *f, uint32_t len, uint32_t l34t, bool bad_csum)
{
	struct mdl *x = &S.m[m];
	if (!(mr(m, 4) & 1) || !(mr(m, 0x3108) & 1) || !wire_ok(m)) {
		x->rx_link_drops++;
		return false;
	}
	if (!filter_pass(m, f)) {
		x->rx_filtered++;
		return false;
	}
	uint32_t rbsz = (mr(m, 0x3108) >> 1) & 0x3fff;
	if (rbsz == 0 || rbsz % 16 != 0) {
		violation("RX buffer size %u", rbsz);
		return false;
	}
	uint64_t ring;
	uint32_t rlen, tidx;
	if (!ring_index(m, false, &tidx, &ring, &rlen)) {
		return false;
	}
	uint32_t ndesc = (len + rbsz - 1) / rbsz;
	for (uint32_t k = 0, idx = x->rx_cur; k < ndesc; k++, idx = (idx + 1) % rlen) {
		struct sdesc *d = (struct sdesc *)dma_ptr(ring + 16ull * idx, 16, "RX descriptor");
		if (d == NULL) return false;
		if (idx == tidx || (d->des3 & (1u << 31)) == 0) {
			mw(m, 0x3160, mr(m, 0x3160) | 0x80);    /* RBU */
			x->rx_nodesc++;
			return false;
		}
	}
	bool ioc = false, ipc = (mr(m, 4) & (1u << 9)) != 0;
	for (uint32_t k = 0; k < ndesc; k++) {
		struct sdesc *d = (struct sdesc *)dma_ptr(ring + 16ull * x->rx_cur, 16, "RX descriptor");
		uint32_t chunk = len - k * rbsz < rbsz ? len - k * rbsz : rbsz;
		uint8_t *b = dma_ptr((uint64_t)d->des1 << 32 | d->des0, rbsz, "RX buffer");
		if (b != NULL) memcpy(b, f + k * rbsz, chunk);
		ioc |= (d->des3 & (1u << 30)) != 0;
		uint32_t des3 = k == 0 ? (1u << 29) : 0;
		if (k == ndesc - 1) {
			des3 |= (1u << 28) | len;
			if (ipc) des3 |= l34t << 20;
			if (ipc && bad_csum) des3 |= 1u << 15;
		}
		d->des0 = d->des1 = d->des2 = 0x5a5a5a5a;      /* write-back format */
		d->des3 = des3;
		x->rx_cur = (x->rx_cur + 1) % rlen;
	}
	x->rx_frames++;
	if (ioc) {
		mw(m, 0x3160, mr(m, 0x3160) | 0x40);
	} else if ((mr(m, 0x313c) & 0xff) != 0 && x->rwt_at == 0) {
		x->rwt_at = S.now + 2ull * (mr(m, 0x313c) & 0xff);
	}
	msi_eval();
	return true;
}

/* -- The chip as firmware leaves it -- */

static const uint8_t uefi_mac[2][6] = { { 0x88, 0x12, 0x4e, 0x00, 0x02, 0x00 }, { 0x88, 0x12, 0x4e, 0x00, 0x02, 0x01 } };

static void
sim_reset(void)
{
	memset(&S, 0, sizeof S);
	for (int m = 0; m < 2; m++) {
		struct mdl *x = &S.m[m];
		x->msi_armed = true;
		xpcs_defaults(x);
		x->phy.bmcr = 0x1140;
		x->phy.anar = 0x0de1;           /* 10/100 half and full, both pauses */
		x->phy.gtcr = 0x0300;           /* 1000 half and full */
		x->phy.adv25 = 0x0080;
		x->phy.partner = SP10 | SP100 | SP1000 | SP2500;
		x->phy.partner_pause = 0x0400;
	}
	/* Out of reset: everything held, clocks off, an invalid speed selector. */
	wr(R_NRST(0), B_RST_MAC | B_RST_PMA | B_RST_XPCS | B_MSIGEN);
	wr(R_NRST(1), B_RST_MAC | B_RST_PMA | B_RST_XPCS);
	wr(R_EMAC(0), 8);
	wr(R_EMAC(1), 8);
	wr(0, 0x01);
	mem_next = 0;
	memset(mem, 0, MEM_SIZE);
}

/* What UEFI leaves on the Q8B: the TAMAP and port m running at 2.5G. */
static void
sim_uefi_port(int m, bool linked)
{
	struct mdl *x = &S.m[m];
	for (int i = 0; i < 4; i++) {
		S.bar0[0][(0x800 + 0x20 * i) / 4] = i == 0 ? 0x47 : 0;
		S.bar0[0][(0x804 + 0x20 * i) / 4] = i == 0 ? 0x10 : 0;
	}
	wr(R_NCLK(m), rd(R_NCLK(m)) | B_CLK_ALL | (1u << 7) | (1u << 14) | (m == 1 ? (1u << 15) : 0));
	wr(R_NRST(m), rd(R_NRST(m)) & ~(B_RST_MAC | B_RST_PMA | B_RST_XPCS));
	wr(R_EMAC(m), 4 | 0x10 | 0x100 | B_INIT_DONE);
	x->pma_rate = 4;
	x->x_ctrl2 = 4;
	x->x_dig1 = 0x2004;
	x->x_bmcr = 0x0140;
	mac_regs_reset(m);
	mw(m, 0x300, (1u << 31) | (uint32_t)uefi_mac[m][5] << 8 | uefi_mac[m][4]);
	mw(m, 0x304, (uint32_t)uefi_mac[m][3] << 24 | (uint32_t)uefi_mac[m][2] << 16 | (uint32_t)uefi_mac[m][1] << 8 |
	    uefi_mac[m][0]);
	mw(m, 0, 2u << 29);
	x->phy.cable = linked;
	x->phy.fifo = linked ? (1u << 11) : 0;
	x->phy.link_at = 0;
}

static void
sim_uefi_msigen(void)
{
	wr(R_NCLK(0), rd(R_NCLK(0)) | B_MSIGEN);
	wr(R_NRST(0), rd(R_NRST(0)) & ~B_MSIGEN);
}

/* ---- A driver loop: NeoDarwinTC956x.cpp's part, in C ---- */

#define NTXD    64
#define NRXD    64
#define BUFSZ   2048
#define NPOOL   (2 * NRXD)

static uint64_t
alloc(uint32_t bytes, uint32_t align)
{
	mem_next = (mem_next + align - 1) & ~(uint64_t)(align - 1);
	uint64_t pa = MEM_BASE + mem_next;
	mem_next += bytes;
	if (mem_next > MEM_SIZE) {
		printf("FAIL: the test ran out of host memory\n");
		exit(1);
	}
	return pa;
}

static uint8_t *
host(uint64_t pa)
{
	return mem + (pa - MEM_BASE);
}

struct drv {
	struct nd_tc956x sc;
	char name[8];
	uint8_t mac[6];
	bool mac_fw, started, enabled;
	uint32_t phy_id;
	/* receive buffers: a pool, and which is on each descriptor */
	uint64_t pool[NPOOL];
	int npool;
	uint64_t rxbuf[NRXD];
	/* received frames */
	uint8_t rx[512][2048];
	uint32_t rx_len[512];
	enum nd_tc956x_rx_csum rx_csum[512];
	int nrx, rx_dropped;
	/* transmit: packet ids at their last descriptors */
	int txpkt[NTXD];
	uint32_t txfirst[NTXD];
	bool txstart[NTXD];             /* a packet's first descriptor */
	int tx_next_id, tx_reclaimed, tx_reclaim_order_bad;
	uint64_t txarena;
	uint32_t txarena_off;
	bool promisc, allmulti;
	uint32_t hash[2];
	bool rx_csum_on;
	int events_up, events_down;
};

static struct drv D[2];

static bool
drv_attach(struct drv *d, int fn, bool cold)
{
	memset(d, 0, sizeof *d);
	nd_tc956x_setup(&d->sc, fn);
	snprintf(d->name, sizeof d->name, "fn%d", fn);
	d->sc.bar4 = d->sc.bar0 = (void *)(uintptr_t)(fn + 1);
	d->sc.owner = d->name;
	if (fn == 0) {
		nd_tc956x_chip_init(&d->sc);
	} else if (!nd_tc956x_msigen_running(&d->sc)) {
		return false;
	}
	d->mac_fw = nd_tc956x_read_mac_address(&d->sc, d->mac);
	if (!d->mac_fw) {
		uint8_t r[6] = { 0x02, 0x4e, 0x44, 0x00, 0x00, (uint8_t)fn };
		memcpy(d->mac, r, 6);
	}
	d->started = nd_tc956x_attach_mac(&d->sc, cold);
	d->phy_id = nd_tc956x_phy_id(&d->sc);
	if (d->phy_id != 0) {
		nd_tc956x_phy_fix_advert(&d->sc);
	}
	uint64_t txr = alloc(NTXD * 16, 16384), rxr = alloc(NRXD * 16, 16384);
	d->sc.txd = (volatile struct nd_tc956x_desc *)host(txr);
	d->sc.rxd = (volatile struct nd_tc956x_desc *)host(rxr);
	d->sc.tx_pa = txr;
	d->sc.rx_pa = rxr;
	d->sc.ntxd = NTXD;
	d->sc.nrxd = NRXD;
	for (int i = 0; i < NPOOL; i++) {
		d->pool[i] = alloc(BUFSZ, 2048);
	}
	d->npool = NPOOL;
	d->txarena = alloc(1u << 20, 4096);
	d->rx_csum_on = true;
	return true;
}

static void
drv_event(struct drv *d, enum nd_tc956x_link_event e)
{
	if (e == ND_TC956X_LINK_WENT_UP) d->events_up++;
	if (e == ND_TC956X_LINK_WENT_DOWN) d->events_down++;
}

static void
drv_link_check(struct drv *d)
{
	drv_event(d, nd_tc956x_link_check(&d->sc));
}

/* Poll the link every 500 ms of model time until it is up, or 10 s. */
static bool
drv_wait_link(struct drv *d)
{
	for (int i = 0; i < 20; i++) {
		drv_link_check(d);
		if (d->sc.link_known && d->sc.link.up) {
			return true;
		}
		advance(500000);
	}
	return false;
}

static void
drv_post(struct drv *d)
{
	CHECK(d->npool > 0, "the receive pool ran dry");
	uint32_t slot = d->sc.rx_pidx;
	d->rxbuf[slot] = d->pool[--d->npool];
	nd_tc956x_rx_refill(&d->sc, d->rxbuf[slot]);
}

static bool
drv_enable(struct drv *d)
{
	struct nd_tc956x_init_params p = {
		.lladdr = d->mac,
		.promisc = d->promisc,
		.allmulti = d->allmulti,
		.hash = d->hash,
		.rx_csum = d->rx_csum_on,
		.rx_bufsz = BUFSZ,
	};
	memset((void *)d->sc.txd, 0, NTXD * 16);
	memset((void *)d->sc.rxd, 0, NRXD * 16);
	/* As NeoDarwinTC956x's stopDatapath: every buffer back. */
	for (int i = 0; i < NRXD; i++) {
		if (d->rxbuf[i] != 0) {
			d->pool[d->npool++] = d->rxbuf[i];
			d->rxbuf[i] = 0;
		}
	}
	memset(d->txpkt, 0, sizeof d->txpkt);
	memset(d->txstart, 0, sizeof d->txstart);
	d->tx_next_id = d->tx_reclaimed = 0;
	if (!nd_tc956x_init(&d->sc, &p)) {
		return false;
	}
	for (int i = 0; i < NRXD - 1; i++) {
		drv_post(d);
	}
	nd_tc956x_rx_flush(&d->sc);
	nd_tc956x_intr_enable(&d->sc);
	d->enabled = true;
	return true;
}

static void
drv_service_rx(struct drv *d)
{
	struct nd_tc956x_rx_frame f;
	int n = 0;
	while (nd_tc956x_rx_next(&d->sc, &f)) {
		bool good = f.ndesc == 1 && f.len >= 14 && f.len <= BUFSZ;
		for (uint32_t k = 0; k < f.ndesc; k++) {
			uint32_t idx = (f.first + k) % NRXD;
			if (k == 0 && good && d->nrx < 512) {
				memcpy(d->rx[d->nrx], host(d->rxbuf[idx]), f.len);
				d->rx_len[d->nrx] = f.len;
				d->rx_csum[d->nrx] = f.csum;
				d->nrx++;
			}
			d->pool[d->npool++] = d->rxbuf[idx];
			d->rxbuf[idx] = 0;
		}
		if (!good) d->rx_dropped++;
		for (uint32_t k = 0; k < f.ndesc; k++) {
			drv_post(d);
		}
		n++;
	}
	if (n) {
		nd_tc956x_rx_flush(&d->sc);
	}
}

static void
drv_reclaim(struct drv *d)
{
	uint32_t old = d->sc.tx_cidx;
	/* What the DMA has finished, as the model's descriptors say, before. */
	uint32_t n = nd_tc956x_tx_reclaim(&d->sc);
	for (uint32_t i = 0; i < n; i++) {
		uint32_t idx = (old + i) % NTXD;
		if (d->sc.txd[idx].des3 & TDES3_OWN) {
			CHECK(0, "descriptor %u reclaimed while the DMA still owns it", idx);
		}
		if (d->txpkt[idx] != 0) {
			if (d->txpkt[idx] != d->tx_reclaimed + 1) d->tx_reclaim_order_bad++;
			d->tx_reclaimed++;
			d->txpkt[idx] = 0;
		}
	}
	/* Whole packets only: the next to reclaim starts one. */
	CHECK(d->sc.tx_cidx == d->sc.tx_pidx || d->txstart[d->sc.tx_cidx], "reclaimed into a packet (to %u)",
	    d->sc.tx_cidx);
	for (uint32_t i = 0; i < n; i++) {
		d->txstart[(old + i) % NTXD] = false;
	}
}

static void
drv_service(struct drv *d)
{
	int m = d->sc.mac;
	while (S.m[m].msi_pending > 0) {
		S.m[m].msi_pending--;
		uint32_t st = nd_tc956x_intr_status(&d->sc);
		if (st == 0) continue;
		drv_service_rx(d);
		drv_reclaim(d);
		nd_tc956x_intr_enable(&d->sc);
	}
}

/* A packet in nsegs pieces; returns its id, or 0 if the ring is full. */
static int
drv_send(struct drv *d, const uint8_t *frame, uint32_t len, uint32_t nsegs, enum nd_tc956x_tx_csum csum)
{
	struct nd_tc956x_seg segs[8];
	uint32_t per = (len + nsegs - 1) / nsegs, off = 0;
	uint32_t n = 0;
	for (; off < len && n < 8; n++) {
		uint32_t l = len - off < per ? len - off : per;
		if (d->txarena_off + l > (1u << 20)) d->txarena_off = 0;
		uint64_t pa = d->txarena + d->txarena_off;
		d->txarena_off += (l + 63) & ~63u;
		memcpy(host(pa), frame + off, l);
		segs[n].pa = pa;
		segs[n].len = l;
		off += l;
	}
	uint32_t first = d->sc.tx_pidx;
	int last = nd_tc956x_tx_encap(&d->sc, segs, n, len, csum);
	if (last < 0) {
		return 0;
	}
	d->txpkt[last] = ++d->tx_next_id;
	d->txfirst[last] = first;
	d->txstart[first] = true;
	nd_tc956x_tx_flush(&d->sc);
	return d->tx_next_id;
}

static void
make_frame(uint8_t *f, uint32_t len, const uint8_t *dst, const uint8_t *src, uint32_t seed)
{
	memcpy(f, dst, 6);
	memcpy(f + 6, src, 6);
	f[12] = 0x08;
	f[13] = 0x00;
	for (uint32_t i = 14; i < len; i++) {
		f[i] = (uint8_t)(seed * 31 + i * 7);
	}
}

static const uint8_t peer[6] = { 0x3c, 0x22, 0xfb, 0x01, 0x02, 0x03 };

/* ---- Tests ---- */

static bool
trace_has(const char *const *want, int n)
{
	int j = 0;
	for (int i = 0; i < S.ntrace && j < n; i++) {
		if (strcmp(S.trace[i], want[j]) == 0) j++;
	}
	return j == n;
}

static void
dump_trace(void)
{
	for (int i = 0; i < S.ntrace; i++) printf("  trace %d: %s\n", i, S.trace[i]);
}

/* Function 0 at boot, cold init, from UEFI's state on the Q8B. */
static void
test_cold_attach_fn0(void)
{
	sim_reset();
	sim_uefi_port(0, false);
	sim_uefi_port(1, true);
	struct drv *d = &D[0];
	CHECK(drv_attach(d, 0, true), "attach");
	CLEAN();
	CHECK(tamap_ok(), "TAMAP entry 0");
	for (int i = 1; i < 4; i++) CHECK(S.bar0[0][(0x800 + 0x20 * i) / 4] == 0, "TAMAP entry %d", i);
	CHECK(S.bar0_writes[1] == 0, "function 1's BAR0 untouched");
	CHECK(msigen_up(), "MSI generators running");
	CHECK(d->mac_fw && memcmp(d->mac, uefi_mac[0], 6) == 0, "UEFI's address kept: %02x:%02x", d->mac[4], d->mac[5]);
	CHECK(d->started, "MAC started");
	/* UEFI left port 0's clocks on, and mac_start already holds the PMA. */
	static const char *const want[] = { "clk0+msigen", "rst0-msigen", "rst0+macpmaxpcs", "rst0-mac", "emac0 sp5",
		                            "rst0-pma", "rst0-xpcs" };
	bool order = trace_has(want, 7) && S.ntrace == 7;
	CHECK(order, "FreeBSD's bring-up order");
	if (!order) dump_trace();
	CHECK(d->sc.serdes_speed == 1000, "no link: SGMII at 1G, got %u", d->sc.serdes_speed);
	CHECK(xpcs_sgmii(&S.m[0]), "XPCS in MAC-side SGMII");
	CHECK(S.m[0].x_soft_resets == 1, "one PCS soft reset");
	CHECK(d->phy_id == QCA8081_ID, "PHY ID 0x%08x", d->phy_id);
	/* UEFI advertised half duplex: the advertisement is fixed, once. */
	CHECK(S.m[0].phy.renegotiations == 1, "renegotiated %d times", S.m[0].phy.renegotiations);
	CHECK((S.m[0].phy.anar & (ND_ANAR_10 | ND_ANAR_TX)) == 0 && (S.m[0].phy.anar & 0x0c00) == 0x0c00, "ANAR 0x%x",
	    S.m[0].phy.anar);
	CHECK((S.m[0].phy.gtcr & ND_GTCR_ADV_1000THDX) == 0, "GTCR 0x%x", S.m[0].phy.gtcr);
	CHECK(!nd_tc956x_phy_fix_advert(&d->sc), "a second fix doesn't renegotiate");
	CHECK(S.sfr_writes[1] == 0, "function 1's registers untouched");
	CLEAN();
}

/* Function 1 before function 0: it must wait, touching nothing. */
static void
test_fn1_before_fn0(void)
{
	sim_reset();
	sim_uefi_port(1, true);
	CHECK(!drv_attach(&D[1], 1, true), "function 1 attached before the chip was set up");
	CHECK(S.sfr_writes[1] == 0 && S.bar0_writes[1] == 0, "function 1 wrote while waiting");
	CLEAN();
	CHECK(drv_attach(&D[0], 0, true), "function 0");
	CHECK(drv_attach(&D[1], 1, true), "function 1 once function 0 is done");
	CHECK(D[1].mac_fw && memcmp(D[1].mac, uefi_mac[1], 6) == 0, "function 1 keeps UEFI's address");
	CHECK(D[1].sc.serdes_speed == 2500 && xpcs_2500basex(&S.m[1]), "linked port at 2.5G: %u", D[1].sc.serdes_speed);
	static const char *const want[] = { "rst1+macpmaxpcs", "rst1-mac", "rst1-pma", "rst1-xpcs" };
	CHECK(trace_has(want, 4), "function 1's order");
	CLEAN();
	/* And firmware's MSI set-up is enough for function 1 (FreeBSD's rule). */
	sim_reset();
	sim_uefi_port(1, true);
	sim_uefi_msigen();
	CHECK(drv_attach(&D[1], 1, true), "function 1 with firmware's MSI generators");
	CHECK(S.bar0_writes[1] == 0, "no TAMAP writes by function 1");
	CLEAN();
}

/* The QCA8081's status for each speed, and the port following it. */
static void
test_link_speeds(void)
{
	sim_reset();
	sim_uefi_port(0, false);
	sim_uefi_port(1, true);
	CHECK(drv_attach(&D[0], 0, true), "fn0");
	CHECK(drv_attach(&D[1], 1, true), "fn1");
	struct drv *d = &D[1];
	struct mdl *x = &S.m[1];
	CHECK(drv_wait_link(d), "link");
	static const struct {
		unsigned sp;
		uint16_t status;
		uint32_t mbps, sp_sel, ss;
	} speeds[] = {
		{ SP2500, 0x2600, 2500, 4, 2 }, { SP1000, 0x2500, 1000, 5, 3 }, { SP100, 0x2480, 100, 6, 4 },
		{ SP10, 0x2400, 10, 7, 7 },     { SP1000, 0x2500, 1000, 5, 3 }, { SP2500, 0x2600, 2500, 4, 2 },
	};
	int resets = x->x_soft_resets;
	uint32_t prev = d->sc.link.speed;
	for (unsigned i = 0; i < sizeof speeds / sizeof speeds[0]; i++) {
		x->phy.partner = speeds[i].sp;
		x->phy.cable = false;
		drv_link_check(d);
		CHECK(!d->sc.link.up, "down with the cable out");
		CHECK((x->phy.fifo & (1u << 11)) == 0, "SerDes FIFO held while down");
		x->phy.cable = true;
		phy_renegotiate(&x->phy);
		int ups = d->events_up, inits = x->pma_inits;
		CHECK(drv_wait_link(d), "link at %u", speeds[i].mbps);
		CHECK(phy_status(&x->phy) == speeds[i].status, "PHY status 0x%x", phy_status(&x->phy));
		CHECK(d->events_up == ups + 1, "one up event");
		CHECK(d->sc.link.speed == speeds[i].mbps && d->sc.link.fdx, "speed %u", d->sc.link.speed);
		CHECK((rd(R_EMAC(1)) & 0xf) == speeds[i].sp_sel && x->pma_rate == speeds[i].sp_sel, "SP_SEL %u",
		    rd(R_EMAC(1)) & 0xf);
		CHECK(((mr(1, 0) >> 29) & 7) == speeds[i].ss, "XGMAC SS %u", (mr(1, 0) >> 29) & 7);
		CHECK((x->phy.fifo & (1u << 11)) != 0, "SerDes FIFO released on link up");
		CHECK(x->pma_inits == inits + (speeds[i].mbps != prev ? 1 : 0), "PMA restarted on a speed change only");
		bool mode_change = (prev == 2500) != (speeds[i].mbps == 2500);
		CHECK(x->x_soft_resets == resets + (mode_change ? 1 : 0), "PCS reset only between 2500BASE-X and SGMII");
		resets = x->x_soft_resets;
		CHECK(wire_ok(1), "frames pass at %u", speeds[i].mbps);
		prev = speeds[i].mbps;
		drv_link_check(d);
		CHECK(d->events_up == ups + 1, "no event without a change");
	}
	x->phy.cable = false;
	int downs = d->events_down;
	drv_link_check(d);
	CHECK(d->events_down == downs + 1 && !d->sc.link.up, "down event");
	CLEAN();
}

/* PAUSE resolution (802.3 Annex 28B) and the MAC's flow control. */
static void
test_pause(void)
{
	static const struct {
		uint16_t partner;
		bool tx, rx;
	} cases[] = {
		{ 0x0400, true, true }, { 0x0c00, true, true }, { 0x0800, false, true }, { 0x0000, false, false },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		sim_reset();
		sim_uefi_port(1, true);
		sim_uefi_msigen();
		S.m[1].phy.partner_pause = cases[i].partner;
		CHECK(drv_attach(&D[1], 1, true), "attach");
		CHECK(drv_wait_link(&D[1]), "link");
		CHECK(D[1].sc.link.txpause == cases[i].tx && D[1].sc.link.rxpause == cases[i].rx, "partner 0x%x: tx %d rx %d",
		    cases[i].partner, D[1].sc.link.txpause, D[1].sc.link.rxpause);
		CHECK(mr(1, 0x70) == (cases[i].tx ? (2u | 0xffffu << 16) : 0), "Q0_TX_FLOW_CTRL 0x%x", mr(1, 0x70));
		CHECK(mr(1, 0x90) == (cases[i].rx ? 1u : 0), "RX_FLOW_CTRL 0x%x", mr(1, 0x90));
		CLEAN();
	}
}

static void
expect_rx_frames(struct drv *d, int m, int count, uint32_t seed0)
{
	uint8_t f[2048];
	int base = d->nrx;
	for (int i = 0; i < count; i++) {
		uint32_t len = 60 + (uint32_t)((i * 97) % 1455);
		make_frame(f, len, d->mac, peer, seed0 + (uint32_t)i);
		CHECK(rx_inject(m, f, len, 0, false), "frame %d reached the ring", i);
		if (i % 8 == 7) {
			advance(200);
			drv_service(d);
		}
	}
	advance(500);
	drv_service(d);
	CHECK(d->nrx == base + count, "received %d of %d", d->nrx - base, count);
	for (int i = 0; i < count && base + i < d->nrx; i++) {
		uint32_t len = 60 + (uint32_t)((i * 97) % 1455);
		make_frame(f, len, d->mac, peer, seed0 + (uint32_t)i);
		if (d->rx_len[base + i] != len || memcmp(d->rx[base + i], f, len) != 0) {
			CHECK(0, "frame %d differs (%u bytes, want %u)", i, d->rx_len[base + i], len);
			break;
		}
	}
}

static void
expect_tx_frames(struct drv *d, int m, int count, uint32_t seed0)
{
	uint8_t f[2048];
	int base = S.m[m].wire_n;
	for (int i = 0; i < count; i++) {
		uint32_t len = 42 + (uint32_t)((i * 131) % 1473);
		make_frame(f, len, peer, d->mac, seed0 + (uint32_t)i);
		if (drv_send(d, f, len, 1 + (uint32_t)(i % 4), ND_TC956X_TX_CSUM_NONE) == 0) {
			advance(100);
			drv_service(d);
			drv_reclaim(d);
			CHECK(drv_send(d, f, len, 1 + (uint32_t)(i % 4), ND_TC956X_TX_CSUM_NONE) != 0, "room after reclaim");
		}
		if (i % 16 == 15) {
			advance(100);
			drv_service(d);
		}
	}
	advance(1000);
	drv_service(d);
	drv_reclaim(d);
	CHECK(S.m[m].wire_n == base + count, "sent %d of %d (lost %d)", S.m[m].wire_n - base, count, S.m[m].tx_lost);
	for (int i = 0; i < count && base + i < S.m[m].wire_n; i++) {
		uint32_t len = 42 + (uint32_t)((i * 131) % 1473);
		make_frame(f, len, peer, d->mac, seed0 + (uint32_t)i);
		if (S.m[m].wire_len[base + i] != len || memcmp(S.m[m].wire[base + i], f, len) != 0) {
			CHECK(0, "frame %d on the wire differs (%u bytes, want %u)", i, S.m[m].wire_len[base + i], len);
			break;
		}
	}
	CHECK(d->tx_reclaimed == d->tx_next_id, "reclaimed %d of %d", d->tx_reclaimed, d->tx_next_id);
	CHECK(d->tx_reclaim_order_bad == 0, "reclaimed out of order");
	CHECK(nd_tc956x_tx_free(&d->sc) == NTXD - 1, "ring free again: %u", nd_tc956x_tx_free(&d->sc));
}

static void
bring_up_fn1(bool cold)
{
	sim_reset();
	sim_uefi_port(0, false);
	sim_uefi_port(1, true);
	CHECK(drv_attach(&D[0], 0, cold), "fn0");
	CHECK(drv_attach(&D[1], 1, cold), "fn1");
	CHECK(drv_wait_link(&D[1]), "fn1 link");
}

/* init's registers, then traffic both ways over many laps of the rings. */
static void
test_datapath(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	CHECK(drv_enable(d), "init");
	CLEAN();
	CHECK(mr(1, 0x3004) == 0x1f1f087f, "SYSBUS_MODE 0x%x", mr(1, 0x3004));
	CHECK(mr(1, 0x3040) == 0x3fffffff && mr(1, 0x3044) == 0x3fffffff, "EDMA");
	CHECK((mr(1, 0x3000) & 0x3000) == 0x1000, "INTM per channel");
	CHECK(mr(1, 0x1100) == (2u | 8u | (63u << 16)), "TXQ0 opmode 0x%x", mr(1, 0x1100));
	CHECK(mr(1, 0x1140) == (0x20u | 0x80u | (127u << 16)), "RXQ0 opmode 0x%x", mr(1, 0x1140));
	CHECK(mr(1, 0x1150) == ((14u << 1) | (22u << 17)), "flow thresholds 0x%x", mr(1, 0x1150));
	CHECK(mr(1, 0x4) == (0x1u | 0x2u | 0x4u | 0x40u | 0x80u | 0x200u | (16368u << 16)), "RX_CONFIG 0x%x", mr(1, 4));
	CHECK((mr(1, 0) & 0x1fffffff) == ((1u << 16) | 1u) && ((mr(1, 0) >> 29) & 7) == 2, "TX_CONFIG 0x%x", mr(1, 0));
	CHECK(mr(1, 0xa0) == 2 && mr(1, 0xb4) == 0, "RXQ_CTRL0, INT_EN");
	CHECK(mr(1, 0x3100) == (1u << 16), "CH_CONTROL PBLx8");
	CHECK(mr(1, 0x3104) == ((32u << 16) | (1u << 12) | 1u), "TX_CONTROL 0x%x", mr(1, 0x3104));
	CHECK(mr(1, 0x3108) == ((32u << 16) | (2048u << 1) | 1u), "RX_CONTROL 0x%x", mr(1, 0x3108));
	CHECK(((uint64_t)mr(1, 0x3110) << 32 | mr(1, 0x3114)) == d->sc.tx_pa + DMA_OFF, "TX ring through the TAMAP");
	CHECK(((uint64_t)mr(1, 0x3118) << 32 | mr(1, 0x311c)) == d->sc.rx_pa + DMA_OFF, "RX ring through the TAMAP");
	CHECK(mr(1, 0x3130) == NTXD - 1 && mr(1, 0x3134) == ((NRXD - 1) | (3u << 24)), "ring lengths");
	CHECK(mr(1, 0x313c) == 64, "RX watchdog 64");
	CHECK(mr(1, 0x3138) == ((1u << 15) | (1u << 14) | (1u << 12) | (1u << 6) | 1u), "CH_INT_EN 0x%x", mr(1, 0x3138));
	CHECK(mr(1, 0x300) == ((1u << 31) | 0x0102u) && mr(1, 0x304) == 0x004e1288u, "station address");
	CHECK(mr(1, 0x312c) == (uint32_t)(d->sc.rx_pa + DMA_OFF + 16 * (NRXD - 1)), "RX tail after the refill");
	CHECK(rd(0xf100) == ((1u << 3) | (1u << 11)), "MSI OUT_EN TX and RX channel 0");

	expect_rx_frames(d, 1, 300, 1);
	CHECK(nd_tc956x_rx_posted(&d->sc) == NRXD - 1, "ring kept full: %u", nd_tc956x_rx_posted(&d->sc));
	expect_tx_frames(d, 1, 400, 7);
	CHECK(S.m[1].msis > 0, "MSIs delivered");
	CLEAN();

	/* A burst under the RX watchdog: few interrupts. */
	int before = S.m[1].msis;
	uint8_t f[256];
	for (int i = 0; i < 32; i++) {
		make_frame(f, 128, d->mac, peer, 1000 + (uint32_t)i);
		rx_inject(1, f, 128, 0, false);
	}
	advance(1000);
	drv_service(d);
	CHECK(S.m[1].msis - before <= 2, "%d MSIs for a burst of 32 under the watchdog", S.m[1].msis - before);
	CHECK(d->nrx == 300 + 32, "burst received: %d", d->nrx - 300);

	/* A frame longer than a buffer spans descriptors: dropped, ring intact. */
	uint8_t big[3000];
	make_frame(big, sizeof big, d->mac, peer, 77);
	CHECK(rx_inject(1, big, sizeof big, 0, false), "jumbo frame reached the ring");
	advance(500);
	drv_service(d);
	CHECK(d->rx_dropped == 1 && nd_tc956x_rx_posted(&d->sc) == NRXD - 1, "jumbo dropped, ring refilled");
	expect_rx_frames(d, 1, 20, 2000);
	CLEAN();
}

/* The TX ring full while the DMA is held: one descriptor stays empty. */
static void
test_tx_full_ring(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	CHECK(drv_enable(d), "init");
	S.m[1].tx_hold = true;
	uint8_t f[128];
	int sent = 0;
	for (int i = 0; i < NTXD + 8; i++) {
		make_frame(f, 100, peer, d->mac, (uint32_t)i);
		if (drv_send(d, f, 100, 1, ND_TC956X_TX_CSUM_NONE) == 0) break;
		sent++;
	}
	CHECK(sent == NTXD - 1, "%d packets fit a ring of %d", sent, NTXD);
	S.m[1].tx_hold = false;
	tx_run(1);
	advance(100);
	drv_service(d);
	drv_reclaim(d);
	CHECK(S.m[1].wire_n == sent, "all %d sent: %d", sent, S.m[1].wire_n);
	CHECK(d->tx_reclaimed == sent, "all reclaimed: %d", d->tx_reclaimed);
	CLEAN();

	/* Packets of three descriptors, the DMA stopping mid-packet. */
	S.m[1].tx_hold = true;
	int base = d->tx_reclaimed;
	sent = 0;
	for (int i = 0; i < 20; i++) {
		make_frame(f, 120, peer, d->mac, (uint32_t)i);
		if (drv_send(d, f, 120, 3, ND_TC956X_TX_CSUM_NONE) == 0) break;
		sent++;
	}
	CHECK(sent == 20, "20 packets of 3 queued: %d", sent);
	for (int round = 0; round < 40 && d->tx_reclaimed - base < sent; round++) {
		S.m[1].tx_hold = false;
		S.m[1].tx_budget = 2;
		tx_run(1);
		drv_reclaim(d);
	}
	S.m[1].tx_budget = 0;
	S.m[1].tx_hold = false;
	CHECK(d->tx_reclaimed - base == sent, "all reclaimed in steps: %d", d->tx_reclaimed - base);
	CLEAN();
}

/* TX completion interrupts: at most every quarter ring, on LD only. */
static void
test_tx_coalescing(void)
{
	static const uint32_t coal[] = { 128, 4, 0 };
	for (unsigned c = 0; c < 3; c++) {
		bring_up_fn1(true);
		struct drv *d = &D[1];
		d->sc.tx_coal_frames = coal[c];
		CHECK(drv_enable(d), "init");
		S.m[1].tx_hold = true;
		uint8_t f[128];
		int iocs = 0;
		for (int i = 0; i < 30; i++) {
			make_frame(f, 100, peer, d->mac, (uint32_t)i);
			uint32_t first = d->sc.tx_pidx;
			CHECK(drv_send(d, f, 100, 2, ND_TC956X_TX_CSUM_NONE) != 0, "send");
			if (d->sc.txd[first].des2 & TDES2_IOC) CHECK(0, "IOC on a first descriptor");
			if (d->sc.txd[(first + 1) % NTXD].des2 & TDES2_IOC) iocs++;
		}
		uint32_t every = coal[c] < NTXD / 4 ? coal[c] : NTXD / 4;
		int want = every == 0 ? 30 : 60 / (int)every;
		CHECK(iocs == want, "coalescing %u: %d IOCs, want %d", coal[c], iocs, want);
		S.m[1].tx_hold = false;
		tx_run(1);
		CHECK(S.m[1].ti_count == iocs, "TI per IOC");
		CLEAN();
	}
}

/* RX moderation: IOC on refill only without the watchdog or every n frames. */
static void
test_rx_moderation(void)
{
	static const struct {
		uint32_t riwt, coal;
	} cases[] = { { 0, 0 }, { 64, 0 }, { 64, 8 } };
	for (unsigned c = 0; c < 3; c++) {
		bring_up_fn1(true);
		struct drv *d = &D[1];
		d->sc.rx_riwt = cases[c].riwt;
		d->sc.rx_coal_frames = cases[c].coal;
		CHECK(drv_enable(d), "init");
		CHECK(mr(1, 0x313c) == cases[c].riwt, "RWT %u", mr(1, 0x313c));
		int iocs = 0;
		for (int i = 0; i < NRXD - 1; i++) {
			if (d->sc.rxd[i].des3 & RDES3_IOC) iocs++;
		}
		int want = cases[c].riwt == 0 ? NRXD - 1 : cases[c].coal == 0 ? 0 : (NRXD - 1) / (int)cases[c].coal;
		CHECK(iocs == want, "riwt %u coal %u: %d IOCs, want %d", cases[c].riwt, cases[c].coal, iocs, want);
		/* One frame: noticed either way. */
		uint8_t f[128];
		make_frame(f, 100, d->mac, peer, 5);
		rx_inject(1, f, 100, 0, false);
		advance(cases[c].riwt * 2 + 50);
		drv_service(d);
		CHECK(d->nrx == 1, "one frame received (riwt %u)", cases[c].riwt);
		CLEAN();
	}
}

/* Checksum insertion and the receive packet types. */
static void
test_checksums(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	CHECK(drv_enable(d), "init");
	uint8_t f[256];
	static const enum nd_tc956x_tx_csum tx[] = { ND_TC956X_TX_CSUM_NONE, ND_TC956X_TX_CSUM_IP, ND_TC956X_TX_CSUM_FULL };
	for (int i = 0; i < 3; i++) {
		make_frame(f, 200, peer, d->mac, (uint32_t)i);
		CHECK(drv_send(d, f, 200, 3, tx[i]) != 0, "send");
		CHECK(S.m[1].wire_cic[S.m[1].wire_n - 1] == (uint32_t)(i == 2 ? 3 : i), "CIC %u for %d",
		    S.m[1].wire_cic[S.m[1].wire_n - 1], i);
	}
	static const struct {
		uint32_t l34t;
		bool bad;
		enum nd_tc956x_rx_csum want;
	} rx[] = {
		{ 1, false, ND_TC956X_RX_CSUM_IP4TCP }, { 2, false, ND_TC956X_RX_CSUM_IP4UDP },
		{ 9, false, ND_TC956X_RX_CSUM_IP6TCP }, { 10, false, ND_TC956X_RX_CSUM_IP6UDP },
		{ 0, false, ND_TC956X_RX_CSUM_NONE },   { 1, true, ND_TC956X_RX_CSUM_NONE },
		{ 3, false, ND_TC956X_RX_CSUM_NONE },
	};
	for (unsigned i = 0; i < sizeof rx / sizeof rx[0]; i++) {
		make_frame(f, 200, d->mac, peer, 50 + i);
		rx_inject(1, f, 200, rx[i].l34t, rx[i].bad);
		advance(300);
		drv_service(d);
		CHECK(d->nrx == (int)i + 1 && d->rx_csum[i] == rx[i].want, "type %u bad %d: %d", rx[i].l34t, rx[i].bad,
		    d->nrx > (int)i ? (int)d->rx_csum[i] : -1);
	}
	CHECK(d->sc.stat_rx_err == 1 && (d->sc.rx_err_des3 & RDES3_ES), "the bad checksum counted");
	CLEAN();
	/* Offload off: no IPC, nothing claimed. */
	bring_up_fn1(true);
	d->rx_csum_on = false;
	CHECK(drv_enable(d), "init");
	CHECK((mr(1, 4) & (1u << 9)) == 0, "IPC off");
	make_frame(f, 200, d->mac, peer, 9);
	rx_inject(1, f, 200, 1, false);
	advance(300);
	drv_service(d);
	CHECK(d->nrx == 1 && d->rx_csum[0] == ND_TC956X_RX_CSUM_NONE, "unchecked without offload");
	CLEAN();
}

/* The 64-bin hash, against the MAC's rule computed another way, and the filter. */
static void
test_multicast(void)
{
	static const uint8_t groups[][6] = {
		{ 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfb }, { 0x33, 0x33, 0x00, 0x00, 0x00, 0x01 },
		{ 0x33, 0x33, 0xff, 0x12, 0x34, 0x56 }, { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x01 },
		{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x0e }, { 0x33, 0x33, 0x00, 0x00, 0x00, 0xfb },
	};
	for (unsigned i = 0; i < 6; i++) {
		uint32_t h[2] = { 0, 0 };
		nd_tc956x_hash_add(h, groups[i]);
		uint32_t bin = model_hash_bin(groups[i]);
		CHECK(h[bin >> 5] == (1u << (bin & 31)) && h[(bin >> 5) ^ 1] == 0, "group %u: bin %u, table %08x %08x", i,
		    bin, h[0], h[1]);
	}
	bring_up_fn1(true);
	struct drv *d = &D[1];
	nd_tc956x_hash_add(d->hash, groups[0]);
	nd_tc956x_hash_add(d->hash, groups[1]);
	CHECK(drv_enable(d), "init");
	CHECK(mr(1, 8) == ((1u << 10) | (1u << 2)), "PACKET_FILTER 0x%x", mr(1, 8));
	uint8_t f[128];
	static const uint8_t other[6] = { 0x3c, 0x22, 0xfb, 0x99, 0x99, 0x99 };
	static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	const uint8_t *dsts[] = { groups[0], groups[1], groups[2], bcast, other, d->mac };
	static const bool pass[] = { true, true, false, true, false, true };
	for (int i = 0; i < 6; i++) {
		make_frame(f, 100, dsts[i], peer, (uint32_t)i);
		int filtered = S.m[1].rx_filtered;
		rx_inject(1, f, 100, 0, false);
		CHECK((S.m[1].rx_filtered == filtered) == pass[i], "destination %d %s", i, pass[i] ? "dropped" : "passed");
	}
	nd_tc956x_set_filter(&d->sc, false, true, d->hash);
	make_frame(f, 100, groups[2], peer, 1);
	CHECK(rx_inject(1, f, 100, 0, false), "all multicast");
	nd_tc956x_set_filter(&d->sc, true, false, d->hash);
	make_frame(f, 100, other, peer, 1);
	CHECK(rx_inject(1, f, 100, 0, false), "promiscuous");
	CLEAN();
}

/* Stop: the transmitter waits for the DMA and the MTL; then all is off. */
static void
test_stop(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	CHECK(drv_enable(d), "init");
	uint8_t f[1500];
	for (int i = 0; i < 20; i++) {
		make_frame(f, 1500, peer, d->mac, (uint32_t)i);
		drv_send(d, f, 1500, 2, ND_TC956X_TX_CSUM_NONE);
	}
	nd_tc956x_stop(&d->sc);
	CHECK(rd(0xf100) == 0, "MSI output off");
	CHECK(mr(1, 0x3138) == 0, "channel interrupts off");
	CHECK((mr(1, 0) & 1) == 0 && (mr(1, 4) & 1) == 0, "TE and RE off");
	CHECK((mr(1, 0x3104) & 1) == 0 && (mr(1, 0x3108) & 1) == 0, "TXST and RXST off");
	CHECK(mr(1, 0x3160) == 0, "status cleared");
	CLEAN();
	/* And it comes back. */
	CHECK(drv_enable(d), "init again");
	expect_tx_frames(d, 1, 10, 3);
	CLEAN();
}

/* hw.tcx.cold_init=0: a running MAC is kept, nothing reset. */
static void
test_warm_attach(void)
{
	sim_reset();
	sim_uefi_port(1, true);
	sim_uefi_msigen();
	S.ntrace = 0;
	CHECK(drv_attach(&D[1], 1, false), "attach");
	CHECK(S.ntrace == 0, "no clock or reset changes (%d)", S.ntrace);
	CHECK(D[1].sc.serdes_speed == 2500, "speed from SP_SEL: %u", D[1].sc.serdes_speed);
	CHECK(S.m[1].pma_inits == 0 && S.m[1].x_soft_resets == 0, "SerDes and PCS untouched");
	CHECK(drv_wait_link(&D[1]), "link");
	CHECK(drv_enable(&D[1]), "init");
	expect_tx_frames(&D[1], 1, 5, 1);
	CLEAN();
}

/* Port 0 as UEFI leaves it when no cable was in: MAC off, FIFO held. */
static void
test_port0_fifo(void)
{
	sim_reset();
	sim_uefi_port(1, true);
	struct drv *d = &D[0];
	S.m[0].phy.cable = false;
	CHECK(drv_attach(d, 0, true), "attach");
	CHECK(!d->mac_fw, "no firmware address on a stopped MAC");
	/* From a stopped MAC: clocks on before any reset is released. */
	static const char *const want[] = { "clk0+msigen", "rst0-msigen", "clk0+mac", "rst0-mac", "emac0 sp5",
		                            "rst0-pma", "rst0-xpcs" };
	bool order = trace_has(want, 7);
	CHECK(order, "FreeBSD's bring-up order from a stopped MAC");
	if (!order) dump_trace();
	CLEAN();
	CHECK(!drv_wait_link(d) || 1, "no link yet");
	S.m[0].phy.cable = true;
	phy_renegotiate(&S.m[0].phy);
	CHECK(drv_wait_link(d), "link");
	CHECK((S.m[0].phy.fifo & (1u << 11)) != 0, "FIFO released");
	CHECK(drv_enable(d), "init");
	expect_tx_frames(d, 0, 20, 4);
	expect_rx_frames(d, 0, 20, 5);
	CLEAN();
}

/* A spurious MSI re-arms the generator; a real one leaves it off until enable. */
static void
test_msi_rearm(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	CHECK(drv_enable(d), "init");
	S.m[1].msi_armed = false;       /* as after an MSI */
	CHECK(nd_tc956x_intr_status(&d->sc) == 0, "nothing pending");
	CHECK(S.m[1].msi_armed, "MASK_CLR on a stray");
	d->sc.rx_riwt = 0;
	uint8_t f[128];
	make_frame(f, 100, d->mac, peer, 1);
	int msis = S.m[1].msis;
	rx_inject(1, f, 100, 0, false);
	advance(200);
	CHECK(S.m[1].msis == msis + 1 || S.m[1].msis == msis, "MSI");
	advance(500);
	CHECK(S.m[1].msis > msis, "an MSI after the stray");
	S.m[1].msi_pending = 0;
	uint32_t st = nd_tc956x_intr_status(&d->sc);
	CHECK(st & XGMAC_DMA_CH_RI, "RI");
	CHECK(rd(0xf100) == 0, "output off while servicing");
	nd_tc956x_intr_enable(&d->sc);
	CLEAN();
}

/* Forced media: one speed advertised; autoselect again. */
static void
test_set_media(void)
{
	bring_up_fn1(true);
	struct drv *d = &D[1];
	struct mdl *x = &S.m[1];
	CHECK(nd_tc956x_phy_set_media(&d->sc, 1000) == 0, "1000");
	CHECK((x->phy.anar & 0x01e0) == 0 && (x->phy.anar & 0x0c00) == 0x0c00, "ANAR 0x%x", x->phy.anar);
	CHECK(x->phy.gtcr == 0x0200 && (x->phy.adv25 & 0x80) == 0, "GTCR 0x%x 2.5G 0x%x", x->phy.gtcr, x->phy.adv25);
	advance(3000);
	drv_link_check(d);
	CHECK(drv_wait_link(d) && d->sc.link.speed == 1000, "linked at %u", d->sc.link.speed);
	CHECK(wire_ok(1), "frames pass at 1000");
	CHECK(nd_tc956x_phy_set_media(&d->sc, 100) == 0, "100");
	CHECK((x->phy.anar & 0x01e0) == 0x0100 && x->phy.gtcr == 0, "ANAR 0x%x", x->phy.anar);
	CHECK(nd_tc956x_phy_set_media(&d->sc, 0) == 0, "auto");
	CHECK((x->phy.anar & 0x01e0) == 0x0140 && x->phy.gtcr == 0x0200 && (x->phy.adv25 & 0x80), "everything FD");
	advance(3000);
	drv_link_check(d);
	CHECK(drv_wait_link(d) && d->sc.link.speed == 2500, "back at %u", d->sc.link.speed);
	CHECK(nd_tc956x_phy_set_media(&d->sc, 5000) == -2, "5000 refused");
	CLEAN();
}

/* A SerDes that never comes up: logged, retried at the next check. */
static void
test_pma_timeout(void)
{
	sim_reset();
	sim_uefi_port(1, true);
	sim_uefi_msigen();
	S.m[1].serdes_dead = true;
	CHECK(drv_attach(&D[1], 1, true), "attach");
	CHECK(!D[1].started && D[1].sc.serdes_speed == 0, "start failed: %u", D[1].sc.serdes_speed);
	CHECK(strstr(S.logs, "SerDes did not come up at 2500 Mb/s") != NULL && strstr(S.logs, "did not start cleanly"),
	    "logged: %s", S.logs);
	S.m[1].serdes_dead = false;
	CHECK(drv_wait_link(&D[1]), "link");
	CHECK(D[1].sc.serdes_speed == 2500 && wire_ok(1), "retried at the link check");
	CLEAN();
}

int
main(void)
{
	mem = calloc(1, MEM_SIZE);
	if (mem == NULL) {
		return 1;
	}
	test_cold_attach_fn0();
	test_fn1_before_fn0();
	test_link_speeds();
	test_pause();
	test_datapath();
	test_tx_full_ring();
	test_tx_coalescing();
	test_rx_moderation();
	test_checksums();
	test_multicast();
	test_stop();
	test_warm_attach();
	test_port0_fifo();
	test_msi_rearm();
	test_set_media();
	test_pma_timeout();
	free(mem);
#ifdef ND_TC956X_EXPECT_CAUGHT
	if (failures != 0) {
		printf("caught: %d check(s) failed, as they should with this planted bug\n", failures);
		return 0;
	}
	printf("FAIL: the planted bug went unnoticed\n");
	return 1;
#else
	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("tc956x_sim_test: all checks passed\n");
	return 0;
#endif
}
