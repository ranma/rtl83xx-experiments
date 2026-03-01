#include <inttypes.h>
#include <stdio.h>
#include "rtl8367_sfr.h"
#include "rtl8367c_reg.h"

static void flash_init(void)
{
	SPI_IO_CONF = 0x18; /* DIO mode */
	SPI_S_RCMD = 0xbb;  /* Fast Read Dual I/O command */
	SPI_S_CONF = 4;     /* Read 4 bytes at a time */
}

static void clock_init(void)
{
	CKCON = 0x10;  /* No XRAM stretch cycles, timer0/timer2 run at ck/12, timer1 at ck/4 */
	/* Timer 0: Mode 1, 16bit counter
	 * Timer 1: Mode 2, 8bit counter with auto-reload */
	TMOD = 0x21;
	TCON = 0x50;  /* Enable timer 0 and timer 1, no interrupts */
	TH1 = 0xef;   /* 57600 baud @ 62.5MHz */
	T2CON = 0x00; /* Timer1 as RX/TX clock; Timer 2 disabled */
}

static void soc_cmd(uint8_t cmd)
{
	INDACC_CMD = cmd;
	while (INDACC_STATUS);
}

static void soc_init(void)
{
	/* Set UART_EN to configure pin mux */
	INDACC_ADDR = RTL8367C_REG_IO_MISC_FUNC;
	INDACC_WDATA = 0x0004;  /* UART_EN */
	soc_cmd(INDACC_CMD_WRITE_REG);
	/* Done early on in MFG FW, keep it that way? */
	INDACC_ADDR = RTL8367C_REG_REG_TO_ECO2;
	INDACC_WDATA = 0x0100;
	soc_cmd(INDACC_CMD_WRITE_REG);
	/* Switch from default 20.833MHz (rate 5) to 62.5MHz (rate 4) CPU clock
	 * SPIF_CK2 halves the SPI clock from 62.5MHz to 31.25MHz (as default "read data" cmd 3h is otherwise out of
	 * spec) */
	INDACC_ADDR = RTL8367C_REG_DW8051_RDY;
	INDACC_WDATA = 0x0141;  /* SPIF_CK2, DW8051_READY, DW8051_RATE_4 */
	soc_cmd(INDACC_CMD_WRITE_REG);
}

int putchar(int c) {
	if (c == '\n') {
		TI = 0;
		SBUF = '\r';
		while (!TI) /* assumes UART is initialized */
			;
	}
	TI = 0;
	SBUF = c;
	while (!TI) /* assumes UART is initialized */
		;
	return c;
}

static void uart_init(void)
{
	SCON = 0x50;  /* Serial port mode 1, receive enable */
	PCON = 0x80;  /* Double the baud rate (div 16 instead of div 32) */
}

void main(void) {
	flash_init();
	clock_init();
	soc_init();
	uart_init();
	while (1) {
		printf("Hello world!\n");
	}
}
