#ifndef CORE_H
#define CORE_H

union notifyval {
	void *ptr;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
};

struct notifier {
	void (*notify)(void *priv, union notifyval data);
	void *priv;
};

struct sfr_bit {
	uint8_t *data;
	uint8_t mask;
	uint8_t xorval;
};

void run_8051(void);
void map_rom(uint8_t *ptr, int start, int len, int ofs);
void map_xram(uint8_t *ptr, int start, int len, int ofs);
void map_xram_ro(uint8_t *ptr, int start, int len, int ofs);
void map_dptr(uint8_t *dpl, uint8_t *dph);
void map_irq(int idx, uint16_t vec, struct sfr_bit *enable, struct sfr_bit *source, struct sfr_bit *prio);
void watch_sfr(uint8_t *sfrptr, struct notifier *notifier);

uint8_t ramd_get(uint8_t *ram, uint8_t addr);
void ramd_put(uint8_t *ram, uint8_t addr, uint8_t val);
void poke_sfr(uint8_t addr, uint8_t val);
void poke_xram(uint16_t addr, uint8_t val);
void poke_pcx(uint8_t val);

uint8_t peek_code(uint16_t addr);
uint8_t peek_sfr(uint8_t addr);
uint8_t peek_iram(uint8_t addr);
uint8_t peek_xram(uint16_t addr);
uint8_t *sfr_ptr(uint8_t addr);

int writex_hook(uint16_t addr, uint8_t val);
int readx_hook(uint16_t addr, uint8_t *val);

uint16_t get_pc(void);
uint16_t get_dptr(void);
void clear_breakpoint(void);
void set_breakpoint(uint32_t mask, uint32_t val);
void set_trace(bool enable, int n);
bool get_trace(void);

extern uint32_t ramsfrtrap[256 / 32];

typedef uint8_t (readx_fn)(uint16_t addr, uint8_t *page);
typedef void (writex_fn)(uint16_t addr, uint8_t *page, uint8_t data);

extern writex_fn *xramfnwr[256];
extern readx_fn *xramfnrd[256];

#define SFR_ACC   0xe0
#define SFR_B     0xf0
#define SFR_SCON  0x98
#define SFR_SBUF  0x99
#define SFR_IE    0xa8
#define SFR_IP    0xb8
#define SFR_T2CON 0xc8
#define SFR_SP    0x81
#define SFR_DPL   0x82
#define SFR_DPH   0x83

#define SCON_RI 0x01
#define SCON_TI 0x02

#define SFRNOTRAP(x) (ramsfrtrap[(x) >> 5] &= ~(1 << ((x) & 0x1f)))

#endif /* CORE_H */
