#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#include "core.h"

#define ENABLE_DEBUG 1
#define ENABLE_DPS 1

#if ENABLE_DEBUG == 1
#define DEBUG(x) ({if (enable_trace) { x; }})
#else
#define DEBUG(x)
#endif

//#define XPAGE_SFR 0xa0
#define XPAGE_SFR 0x92

#define SP  ramsfr[0x81]
#if ENABLE_DPS == 1
#define DPH (*dph)
#define DPL (*dpl)
#define DPL0 ramsfr[0x82]
#define DPH0 ramsfr[0x83]
#define DPL1 ramsfr[0x84]
#define DPH1 ramsfr[0x85]
#else
#define DPL ramsfr[0x82]
#define DPH ramsfr[0x83]
#define DPL0 DPL
#define DPH0 DPH
#endif
#define DPTR ((DPH << 8) | DPL)
#define PSW ramsfr[0xd0]
#define PSW_C  (1 << 7)
#define PSW_AC (1 << 6)
#define PSW_OV (1 << 2)
#define PSW_P  (1 << 0)
#define ACC ramsfr[0xe0]
#define B   ramsfr[0xf0]
#define R0  reg[0]
#define R1  reg[1]
#define RX  reg[op & 7]
#define PUSH(x) writei(++SP, x)
#define POP() readi(SP--)
#define EA  (ramsfr[SFR_IE] & 0x80)

#define FETCH do { op = nextop(); goto *insnmap[op]; } while (0)

struct sfr_notifiers {
	int entries;
	struct notifier notifiers[8];
};

static bool enable_trace = 1;
static int trace_count = 0;

uint32_t ramsfrtrap[256 / 32];

static uint8_t *rompage[256];
static uint8_t *xrampage[256];
writex_fn *xramfnwr[256];
readx_fn *xramfnrd[256];
static uint8_t ramsfr[256 + 128];
static uint8_t *reg = ramsfr;
static uint8_t *dph = &DPH0;
static uint8_t *dpl = &DPL0;
static uint8_t pcx = 0;  /* PC extension / BANK */
static uint32_t irqlevel = 0; /* IRQ priority level bitmask */
static uint32_t irqpendl0 = 0; /* Level 0 pending irqs */
static uint32_t irqpendl1 = 0; /* Level 1 pending irqs */

static uint8_t ramsfrnotify[256];
static struct sfr_notifiers ramsfrnotifiers[256];

void opcode_a5(void) __attribute__((weak));

void opcode_a5(void)
{
	fprintf(stderr, "Fault: Undefined opcode 0xa5 not implemented!\n");
	exit(1);
}

uint8_t xram_p2addr(void) __attribute__((weak));

uint8_t xram_p2addr(void)
{
	return ramsfr[XPAGE_SFR];
}

void map_dptr(uint8_t *_dpl, uint8_t *_dph)
{
	dph = _dph;
	dpl = _dpl;
}

static uint8_t bitaddr[32] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x80, 0x88, 0x90, 0x98, 0xa0, 0xa8, 0xb0, 0xb8,
	0xc0, 0xc8, 0xd0, 0xd8, 0xe0, 0xe8, 0xf0, 0xf8,
};

static uint16_t pc;
static long retired;

uint8_t *get_regs(void)
{
	return reg;
}

uint16_t get_pc(void)
{
	return pc;
}

uint16_t get_dptr(void)
{
	return DPTR;
}

static void writex_ro(uint16_t addr, uint8_t *page, uint8_t data)
{
	fprintf(stderr, "Attempted write to read-only XRAM @%04x PC=%04x\n", addr, pc);
}

void map_rom(uint8_t *ptr, int start, int len, int ofs)
{
	DEBUG(printf("Mapping ROM: %02x@%02x+%04x.\n", len, start, ofs));
	int i;
	for (i = 0; i < len; i++) {
		uint8_t page = start + i;
		rompage[page] = &ptr[(i + ofs) * 0x100];
	}
}

void map_xram(uint8_t *ptr, int start, int len, int ofs)
{
	DEBUG(printf("Mapping XRAM: %02x@%02x+%04x.\n", len, start, ofs));
	int i;
	for (i = 0; i < len; i++) {
		uint8_t page = start + i;
		xrampage[page] = &ptr[(i + ofs) * 0x100];
		xramfnwr[page] = NULL;
	}
}

void map_xram_ro(uint8_t *ptr, int start, int len, int ofs)
{
	DEBUG(printf("Mapping XRAM read-only: %02x@%02x+%04x.\n", len, start, ofs));
	int i;
	for (i = 0; i < len; i++) {
		uint8_t page = start + i;
		xrampage[page] = &ptr[(i + ofs) * 0x100];
		xramfnwr[page] = &writex_ro;
	}
}

static uint8_t readx(uint16_t addr) {
	int high = addr >> 8;
	uint8_t *p = xrampage[high];
	readx_fn *fn = xramfnrd[high];
	if (fn) {
		return fn(addr, p);
	}
	if (p) {
		return p[addr & 0xff];
	} else {
		DEBUG(printf("readx(%04x) unmapped (pc=%04x)\n", addr, pc));
		return 0xff;
	}
}

static uint8_t readx8(uint8_t addr) {
	uint16_t hi = xram_p2addr() << 8;
	return readx(hi | addr);
}


static void writex(uint16_t addr, uint8_t val) {
	DEBUG(printf("writex(%04x, %02x)\n", addr, val));
	int high = addr >> 8;
	uint8_t *p = xrampage[high];
	writex_fn *fn = xramfnwr[high];
	if (fn) {
		return fn(addr, p, val);
	}
	if (p) {
		p[addr & 0xff] = val;
	} else {
		DEBUG(printf("writex(%04x, %02x) unmapped (pc=%04x)\n", addr, val, pc));
	}
}

static void writex8(uint8_t addr, uint8_t val) {
	uint16_t hi = xram_p2addr() << 8;
	return writex(hi | addr, val);
}

static uint8_t default_ramd_get(uint8_t *ramsfr, uint8_t addr)
{
	uint8_t tmp;
	switch (addr) {
	case 0xd0:  /* PSW */
		/* Update parity flag */
		tmp = (ACC >> 4) ^ (ACC & 0xf);
		tmp = (tmp >> 2) ^ (tmp & 0x3);
		tmp = (tmp >> 1) ^ (tmp & 0x1);
		PSW &= ~PSW_P;
		if (tmp & 1) {
			PSW |= PSW_P;
		}
		reg = &ramsfr[PSW & 0x18];
		break;
	}
	return ramd_get(ramsfr, addr);
}

static uint8_t readd(uint8_t addr) {
	if ((ramsfrtrap[addr >> 5] & (1 << (addr & 0x1f))) == 0) {
		return ramsfr[addr];
	} else {
		return default_ramd_get(ramsfr, addr);
	}
}

static void sfr_notify(uint8_t addr, uint8_t val)
{
	struct sfr_notifiers *notifiers = &ramsfrnotifiers[addr];
	if (notifiers == NULL || notifiers->entries == 0) {
		fprintf(stderr, "Invalid sfr_notify(0x%02x)\n", addr);
		return;
	}
	for (int i = 0; i < notifiers->entries; i++) {
		struct notifier *notifier = &notifiers->notifiers[i];
		union notifyval v = { 0 };
		v.u16 = (addr << 8) | val;
		notifier->notify(notifier->priv, v);
	}
}

static void default_ramd_put(uint8_t *ram, uint8_t addr, uint8_t val) {
	ramd_put(ramsfr, addr, val);
	switch (addr) {
	case 0xd0:  /* PSW */
		/* Move register window */
		reg = &ramsfr[PSW & 0x18];
		break;
	}
	if (ramsfrnotify[addr]) {
		sfr_notify(addr, val);
	}
}

static void writed(uint8_t addr, uint8_t val) {
	DEBUG(printf("writed(%02x, %02x)\n", addr, val));
	if ((ramsfrtrap[addr >> 5] & (1 << (addr & 0x1f))) == 0) {
		ramsfr[addr] = val;
	} else {
		default_ramd_put(ramsfr, addr, val);
	}
}

static uint8_t readi(uint8_t addr) {
	/* Skip over SFR regs */
	return ramsfr[addr + (addr & 0x80)];
}

static void writei(uint8_t addr, uint8_t val) {
	DEBUG(printf("writei(%02x, %02x)\n", addr, val));
	/* Skip over SFR regs */
	ramsfr[addr + (addr & 0x80)] = val;
}

static int readb(uint8_t bit) {
	uint8_t addr = bitaddr[bit >> 3];
	uint8_t mask = 1 << (bit & 7);
	return !!(readd(addr) & mask);
}

static void writeb(uint8_t bit, int value) {
	uint8_t mask = 1 << (bit & 7);
	uint8_t addr = bitaddr[bit >> 3];
	uint8_t tmp = readd(addr) & ~mask;
	if (value)
		tmp |= mask;
	writed(addr, tmp);
}

static uint8_t readc(uint16_t addr) {
	int high = addr >> 8;
	uint8_t *p = rompage[high];
	if (p) {
		return p[addr & 0xff];
	} else {
		printf("readc(%04x) unmapped (pc=%04x)\n", addr, pc);
		return 0xff;
	}
}

uint8_t peek_iram(uint8_t addr)
{
	return readi(addr);
}

void poke_iram(uint8_t addr, uint8_t val)
{
	writei(addr, val);
}

uint8_t peek_xram(uint16_t addr)
{
	return readx(addr);
}

void poke_xram(uint16_t addr, uint8_t val)
{
	writex(addr, val);
}

uint8_t peek_code(uint16_t addr)
{
	return readc(addr);
}

uint8_t peek_sfr(uint8_t addr)
{
	return readd(addr);
}

uint8_t *sfr_ptr(uint8_t addr)
{
	return &ramsfr[addr];
}

void poke_sfr(uint8_t addr, uint8_t val)
{
	writed(addr, val);
}

void poke_pcx(uint8_t val)
{
	pcx = val;
}

static uint8_t fetch(uint16_t ofs) {
	return readc(pc + ofs);
}

static const uint8_t insn_bytes[256] = {
1, 2, 3, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
3, 2, 3, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
3, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
3, 2, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 3, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 1, 2, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
2, 2, 2, 1, 1, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
3, 2, 2, 1, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
2, 2, 2, 1, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
2, 2, 2, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
2, 2, 2, 1, 1, 3, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
1, 2, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

static const char* insn_str[256] = {
	"NOP",
	"AJMP  addr11",
	"LJMP  addr16",
	"RR    A",
	"INC   A",
	"INC   iram",
	"INC   @R0",
	"INC   @R1",
	"INC   R0",
	"INC   R1",
	"INC   R2",
	"INC   R3",
	"INC   R4",
	"INC   R5",
	"INC   R6",
	"INC   R7",
	"JBC   bit, ofs",
	"ACALL addr11",
	"LCALL addr16",
	"RRC   A",
	"DEC   A",
	"DEC   iram",
	"DEC   @R0",
	"DEC   @R1",
	"DEC   R0",
	"DEC   R1",
	"DEC   R2",
	"DEC   R3",
	"DEC   R4",
	"DEC   R5",
	"DEC   R6",
	"DEC   R7",
	"JB    bit, ofs",
	"AJMP  addr11",
	"RET",
	"RL    A",
	"ADD   A, imm8",
	"ADD   A, iram",
	"ADD   A, @R0",
	"ADD   A, @R1",
	"ADD   A, R0",
	"ADD   A, R1",
	"ADD   A, R2",
	"ADD   A, R3",
	"ADD   A, R4",
	"ADD   A, R5",
	"ADD   A, R6",
	"ADD   A, R7",
	"JNB   bit, ofs",
	"ACALL addr11",
	"RETI",
	"RLC   A",
	"ADDC  A, imm8",
	"ADDC  A, iram",
	"ADDC  A, @R0",
	"ADDC  A, @R1",
	"ADDC  A, R0",
	"ADDC  A, R1",
	"ADDC  A, R2",
	"ADDC  A, R3",
	"ADDC  A, R4",
	"ADDC  A, R5",
	"ADDC  A, R6",
	"ADDC  A, R7",
	"JC    ofs",
	"AJMP  addr11",
	"ORL   iram, A",
	"ORL   iram, imm8",
	"ORL   A, imm8",
	"ORL   A, iram",
	"ORL   A, @R0",
	"ORL   A, @R1",
	"ORL   A, R0",
	"ORL   A, R1",
	"ORL   A, R2",
	"ORL   A, R3",
	"ORL   A, R4",
	"ORL   A, R5",
	"ORL   A, R6",
	"ORL   A, R7",
	"JNC   ofs",
	"ACALL addr11",
	"ANL   iram, A",
	"ANL   iram, imm8",
	"ANL   A, imm8",
	"ANL   A, iram",
	"ANL   A, @R0",
	"ANL   A, @R1",
	"ANL   A, R0",
	"ANL   A, R1",
	"ANL   A, R2",
	"ANL   A, R3",
	"ANL   A, R4",
	"ANL   A, R5",
	"ANL   A, R6",
	"ANL   A, R7",
	"JZ    ofs",
	"AJMP  addr11",
	"XRL   iram, A",
	"XRL   iram, imm8",
	"XRL   A, imm8",
	"XRL   A, iram",
	"XRL   A, @R0",
	"XRL   A, @R1",
	"XRL   A, R0",
	"XRL   A, R1",
	"XRL   A, R2",
	"XRL   A, R3",
	"XRL   A, R4",
	"XRL   A, R5",
	"XRL   A, R6",
	"XRL   A, R7",
	"JNZ   ofs",
	"ACALL addr11",
	"ORL   C, bit",
	"JMP   @A+DPTR",
	"MOV   A, imm8",
	"MOV   iram, imm8",
	"MOV   @R0, imm8",
	"MOV   @R1, imm8",
	"MOV   R0, imm8",
	"MOV   R1, imm8",
	"MOV   R2, imm8",
	"MOV   R3, imm8",
	"MOV   R4, imm8",
	"MOV   R5, imm8",
	"MOV   R6, imm8",
	"MOV   R7, imm8",
	"SJMP  ofs",
	"AJMP  addr11",
	"ANL   C, bit",
	"MOVC  A, @A+PC",
	"DIV   AB",
	"MOV   iram, iram",
	"MOV   iram, @R0",
	"MOV   iram, @R1",
	"MOV   iram, R0",
	"MOV   iram, R1",
	"MOV   iram, R2",
	"MOV   iram, R3",
	"MOV   iram, R4",
	"MOV   iram, R5",
	"MOV   iram, R6",
	"MOV   iram, R7",
	"MOV   DPTR, imm8",
	"ACALL addr11",
	"MOV   bit, C",
	"MOVC  A, @A+DPTR",
	"SUBB  A, imm8",
	"SUBB  A, iram",
	"SUBB  A, @R0",
	"SUBB  A, @R1",
	"SUBB  A, R0",
	"SUBB  A, R1",
	"SUBB  A, R2",
	"SUBB  A, R3",
	"SUBB  A, R4",
	"SUBB  A, R5",
	"SUBB  A, R6",
	"SUBB  A, R7",
	"ORL   C, /bit",
	"AJMP  addr11",
	"MOV   C, bit",
	"INC   DPTR",
	"MUL   AB",
	"ill",
	"MOV   @R0, iram",
	"MOV   @R1, iram",
	"MOV   R0, iram",
	"MOV   R1, iram",
	"MOV   R2, iram",
	"MOV   R3, iram",
	"MOV   R4, iram",
	"MOV   R5, iram",
	"MOV   R6, iram",
	"MOV   R7, iram",
	"ANL   C, /bit",
	"ACALL addr11",
	"CPL   bit",
	"CPL   C",
	"CJNE  A, imm8, ofs",
	"CJNE  A, iram, ofs",
	"CJNE  @R0, imm8, ofs",
	"CJNE  @R1, imm8, ofs",
	"CJNE  R0, imm8, ofs",
	"CJNE  R1, imm8, ofs",
	"CJNE  R2, imm8, ofs",
	"CJNE  R3, imm8, ofs",
	"CJNE  R4, imm8, ofs",
	"CJNE  R5, imm8, ofs",
	"CJNE  R6, imm8, ofs",
	"CJNE  R7, imm8, ofs",
	"PUSH  iram",
	"AJMP  addr11",
	"CLR   bit",
	"CLR   C",
	"SWAP  A",
	"XCH   A, iram",
	"XCH   A, @R0",
	"XCH   A, @R1",
	"XCH   A, R0",
	"XCH   A, R1",
	"XCH   A, R2",
	"XCH   A, R3",
	"XCH   A, R4",
	"XCH   A, R5",
	"XCH   A, R6",
	"XCH   A, R7",
	"POP   iram",
	"ACALL addr11",
	"SETB  bit",
	"SETB  C",
	"DA    A",
	"DJNZ  iram, ofs",
	"XCHD  A, @R0",
	"XCHD  A, @R1",
	"DJNZ  R0, ofs",
	"DJNZ  R1, ofs",
	"DJNZ  R2, ofs",
	"DJNZ  R3, ofs",
	"DJNZ  R4, ofs",
	"DJNZ  R5, ofs",
	"DJNZ  R6, ofs",
	"DJNZ  R7, ofs",
	"MOVX  A, @DPTR",
	"AJMP  addr11",
	"MOVX  A, @R0",
	"MOVX  A, @R1",
	"CLR   A",
	"MOV   A, iram",
	"MOV   A, @R0",
	"MOV   A, @R1",
	"MOV   A, R0",
	"MOV   A, R1",
	"MOV   A, R2",
	"MOV   A, R3",
	"MOV   A, R4",
	"MOV   A, R5",
	"MOV   A, R6",
	"MOV   A, R7",
	"MOVX  @DPTR, A",
	"ACALL addr11",
	"MOVX  @R0, A",
	"MOVX  @R1, A",
	"CPL   A",
	"MOV   iram, A",
	"MOV   @R0, A",
	"MOV   @R1, A",
	"MOV   R0, A",
	"MOV   R1, A",
	"MOV   R2, A",
	"MOV   R3, A",
	"MOV   R4, A",
	"MOV   R5, A",
	"MOV   R6, A",
	"MOV   R7, A",
};

#if ENABLE_DEBUG == 1
static void debug(void) {
	char insn_hex[10] = "      ";
	static uint8_t saved_reg[15] = { 0 };
	int print_dptr = 0;
	uint8_t insn = fetch(0);
	uint8_t insn1 = fetch(1);
	uint8_t insn2 = fetch(2);
	int insn_len = insn_bytes[insn];
	if (insn_len > 1) {
		sprintf(insn_hex, " %02x %02x", insn1, insn2);
		if (insn_len == 2) {
			insn_hex[4] = ' ';
			insn_hex[5] = ' ';
		}
	}
	printf("%08x; @%02x%04x: %02x%s %-20s; ", retired, (int)pcx, (int)pc, insn, insn_hex, insn_str[insn]);
	for (int i = 0; i < 8; i++) {
		if (saved_reg[i] != reg[i]) {
			printf(" R%d=%02x", i, reg[i]);
			saved_reg[i] = reg[i];
		}
	}
	if (saved_reg[8] != ACC) {
		printf(" ACC=%02x", ACC);
		saved_reg[8] = ACC;
	}
	if (saved_reg[9] != B) {
		printf(" B=%02x", B);
		saved_reg[9] = B;
	}
	if (saved_reg[10] != SP) {
		printf(" SP=%02x", SP);
		saved_reg[10] = SP;
	}
	if (saved_reg[11] != DPH0) {
		print_dptr = 1;
		printf(" DPH0=%02x", DPH0);
		saved_reg[11] = DPH0;
	}
	if (saved_reg[12] != DPL0) {
		print_dptr = 1;
		printf(" DPL0=%02x", DPL0);
		saved_reg[12] = DPL0;
	}
#if ENABLE_DPS == 1
	if (saved_reg[13] != DPH1) {
		print_dptr = 1;
		printf(" DPH1=%02x", DPH1);
		saved_reg[13] = DPH1;
	}
	if (saved_reg[14] != DPL1) {
		print_dptr = 1;
		printf(" DPL1=%02x", DPL1);
		saved_reg[14] = DPL1;
	}
#endif
	if (print_dptr) {
		printf(" DPTR=%04x", DPTR);
	}
	printf("\n");
}
#else
static inline void debug(void) {
}
#endif

struct irq_data {
	uint16_t vec;
	uint8_t idx;
	struct sfr_bit *enable;
	struct sfr_bit *source;
	struct sfr_bit *prio;
	struct notifier notifier;
};

static struct irq_data irqs[32];

static bool sfr_bitval(struct sfr_bit *bit) {
	if (!bit) {
		return false;
	}
	return (*bit->data ^ bit->xorval) & bit->mask;
}

static void sfr_notify_irq(void *priv, union notifyval data) {
	struct irq_data *irqdata = (struct irq_data *)priv;

	uint32_t mask = 1 << irqdata->idx;
	bool enable = sfr_bitval(irqdata->enable);
	bool source = sfr_bitval(irqdata->source);
	bool pending = enable && source;
	printf("sfr_notify_irq(%d, SFR%02x=0x%02x): en=%d src=%d pend=%d\n",
		irqdata->vec, data.u16 >> 8, data.u16 & 0xff, enable, source, pending);

	irqpendl0 &= ~mask;
	irqpendl1 &= ~mask;
	if (pending) {
		if (sfr_bitval(irqdata->prio)) {
			irqpendl1 |= mask;
		} else {
			irqpendl0 |= mask;
		}
	}
}

void watch_sfr(uint8_t *sfrptr, struct notifier *notifier) {
	uint8_t sfr = sfrptr - ramsfr;
	printf("watch_sfr(0x%02x)\n", sfr);
	struct sfr_notifiers *notifiers = &ramsfrnotifiers[sfr];
	if (notifiers->entries >= sizeof(notifiers->notifiers) / sizeof(*notifiers->notifiers)) {
		fprintf(stderr, "watch_sfr: out of slots for 0x%02x\n", sfr);
		exit(1);
	}
	notifiers->notifiers[notifiers->entries] = *notifier;
	notifiers->entries++;
	ramsfrnotify[sfr] = notifiers->entries;
};

void map_irq(int idx, uint16_t vec, struct sfr_bit *enable, struct sfr_bit *source, struct sfr_bit *prio) {
	if (irqs[idx].source) {
		fprintf(stderr, "map_irq: %d already mapped!\n", idx);
		exit(1);
	}
	irqs[idx].idx = idx;
	irqs[idx].vec = vec;
	irqs[idx].enable = enable;
	irqs[idx].source = source;
	irqs[idx].prio = prio;
	irqs[idx].notifier.notify = sfr_notify_irq;
	irqs[idx].notifier.priv = &irqs[idx];
	if (enable) {
		watch_sfr(enable->data, &irqs[idx].notifier);
	}
	if (source) {
		watch_sfr(source->data, &irqs[idx].notifier);
	}
	if (prio) {
		watch_sfr(prio->data, &irqs[idx].notifier);
	}
}

static void irqlevel_reti(void) {
	switch (irqlevel & 15) {
	case 15: irqlevel &= ~8;
	case 14: irqlevel &= ~8;
	case 13: irqlevel &= ~8;
	case 12: irqlevel &= ~8;
	case 11: irqlevel &= ~8;
	case 10: irqlevel &= ~8;
	case 9:  irqlevel &= ~8;
	case 8:  irqlevel &= ~8;
	case 7:  irqlevel &= ~4;
	case 6:  irqlevel &= ~4;
	case 5:  irqlevel &= ~4;
	case 4:  irqlevel &= ~4;
	case 3:  irqlevel &= ~2;
	case 2:  irqlevel &= ~2;
	case 1:  irqlevel &= ~1;
	case 0:  irqlevel = 0;
	}
}

static int irq_vec(uint32_t mask, int level)
{
	uint32_t lmask = 1 << level;
	if (!mask)
		return -1;
	if (irqlevel >= lmask)
		return -1;
	irqlevel |= lmask;

	int bit = 31 - __builtin_clz(mask);
	int vec = irqs[bit].vec;
	printf("irq_vec: mask=%x l=%d bit=%d vec=0x%04x\n", mask, level, bit, vec);
	return vec;
}

static void handle_irq() {
	int vec = -1;
	if (irqpendl1) {
		vec = irq_vec(irqpendl1, 1);
	} else if (irqpendl0) {
		vec = irq_vec(irqpendl0, 0);
	}
	if (vec <= 0) {
		return;
	}
	PUSH(pc & 0xff);
	PUSH(pc >> 8);
	pc = vec;
}

static uint32_t bpmask = 0;
static uint32_t bpval = -1;

void handle_breakpoint(uint32_t pc) __attribute__((weak));
void handle_breakpoint(uint32_t pc) { }

void clear_breakpoint() {
	bpmask = 0;
	bpval = -1;
}

void set_breakpoint(uint32_t mask, uint32_t val) {
	bpmask = mask;
	bpval = val;
}

void set_trace(bool enable, int n) {
	enable_trace = enable;
	trace_count = n;
}

bool get_trace() {
	return enable_trace;
}

static size_t nextop() {
	if (enable_trace) {
		if (trace_count) {
			trace_count--;
			if (trace_count == 0) {
				enable_trace = 0;
			}
		}
		debug();
	}
	retired++;
	uint32_t irqpend = irqpendl0 | irqpendl1;
	if (unlikely(irqpend && EA)) {
		handle_irq();
	}
	uint8_t op = fetch(0);
	uint32_t full_pc = pc + (pcx << 16);
	if (unlikely((full_pc & bpmask) == bpval)) {
		handle_breakpoint(full_pc);
	}
	return op;
}

static uint8_t alu_cmp(uint8_t op1, uint8_t op2)
{
	PSW &= ~PSW_C;
	PSW |= op1 < op2 ? PSW_C : 0;
	return op1 - op2;
}

static uint8_t alu_addc(uint8_t op1, uint8_t op2)
{
	int c = !!(PSW & PSW_C);
	uint8_t res = op1 + op2 + c;
	int16_t sres = (int8_t)op1 + (int8_t)op2 + c;
	PSW &= ~(PSW_C | PSW_AC | PSW_OV);
	PSW |= op1 + op2 + c> 0xff ? PSW_C : 0;
	PSW |= (op1 & 0xf) + (op2 & 0xf) + c> 0xf ? PSW_AC : 0;
	PSW |= sres > 127 || sres < -128 ? PSW_OV : 0;
	return res;
}

static uint8_t alu_add(uint8_t op1, uint8_t op2)
{
	PSW &= ~PSW_C;
	return alu_addc(op1, op2);
}

static uint8_t alu_subb(uint8_t op1, uint8_t op2)
{
	int c = !!(PSW & PSW_C);
	uint8_t res = op1 - op2 - c;
	int16_t sres = (int8_t)op1 - (int8_t)op2 - c;
	PSW &= ~(PSW_C | PSW_AC | PSW_OV);
	PSW |= (op2 + c) > op1 ? PSW_C : 0;
	PSW |= ((op2 + c) & 0xf) > (op1 & 0xf) ? PSW_AC : 0;
	PSW |= sres > 127 || sres < -128 ? PSW_OV : 0;
	return res;
}

#define alu_orl(op1, op2) ((op1) | (op2))
#define alu_anl(op1, op2) ((op1) & (op2))
#define alu_xrl(op1, op2) ((op1) ^ (op2))

void run_8051(void)
{
	uint16_t reladdr, tmp16;
	uint8_t op, tmp1, tmp2;
	int i;
#include "core_gen.c"
unimpl:
	printf("ACC=%02x DPTR=%04x", ACC, DPTR);
	for (i=0; i<8; i++) {
		printf(" R%d=%02x", i, reg[i]);
	}
	printf("\n");
	fprintf(stderr, "Unimplemented opcode %02x @%04x, retired=%lx\n", op, pc, retired);
	return;
}

void init_8051(void) {
	memset(ramsfrtrap, 0xff, sizeof(ramsfrtrap));
	memset(ramsfrtrap, 0x00, sizeof(ramsfrtrap) / 2);
	SFRNOTRAP(0x81);  // SP
	SFRNOTRAP(0x82);  // DPL
	SFRNOTRAP(0x83);  // DPH
	SFRNOTRAP(0xe0);  // ACC
	SFRNOTRAP(0xf0);  // B
}

