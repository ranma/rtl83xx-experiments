;-----------------------------------------------------------------------------
; RTL8367 8051 firmware test.
;-----------------------------------------------------------------------------

; Local vars

; Stack
.equ MAIN_STACK, 0xb0

; Additional SFRs
.equ DPL2, 0x84
.equ DPH2, 0x85
.equ DPS, 0x86
.equ CKCON, 0x8e
.equ SPC_FNC, 0x8f
.equ CODE_BANK, 0x96
.equ DATA_BANK, 0x97
.equ INDACC_CMD, 0xa0
.equ INDACC_STATUS, 0xa1
.equ INDACC_ADDR_H, 0xa2
.equ INDACC_ADDR_L, 0xa3
.equ INDACC_WDATA_H, 0xa4
.equ INDACC_WDATA_L, 0xa5
.equ INDACC_RDATA_H, 0xa6
.equ INDACC_RDATA_L, 0xa7
.equ SSPL, 0xb9
.equ SSPH, 0xba
.equ SSIO, 0xbb

.equ SFR_EXEC_READ_REG, 1
.equ SFR_EXEC_WRITE_REG, 3
.equ SFR_EXEC_READ_SDS, 5
.equ SFR_EXEC_WRITE_SDS, 7
.equ SFR_EXEC_READ_SMI, 9
.equ SFR_EXEC_WRITE_SMI, 11

.equ RTL8367_0018_PORT0_EEECFG, 0x0018
.equ RTL8367_1300_CHIP_NUMBER, 0x1300
.equ RTL8367_1301_CHIP_VER, 0x1301
.equ RTL8367_1302_CHIP_MODE, 0x1302
.equ RTL8367_130C_MISC_CFG0, 0x130c
.equ RTL8367_130D_MISC_CFG1, 0x130d
.equ RTL8367_1322_CHIP_RESET, 0x1322
.equ RTL8367_1366_DW8051_RDY, 0x1336
.equ RTL8367_13C0_RTL_NO, 0x13c0
.equ RTL8367_13C1_RTL_VER, 0x13c1
.equ RTL8367_13C2_RTL_MAGIC_ID, 0x13c2
.equ RTL8367_1A15_NIC_RXCR1, 0x1a15
.equ RTL8367_1A16_NIC_TXCR, 0x1a16
.equ RTL8367_1A17_NIC_GCR, 0x1a17
.equ RTL8367_1A44_NIC_TXSTOPRL, 0x1a44
.equ RTL8367_1A45_NIC_TXSTOPRH, 0x1a45
.equ RTL8367_1A46_NIC_RXSTOPRL, 0x1a46
.equ RTL8367_1A47_NIC_RXSTOPRH, 0x1a47
.equ RTL8367_1A48_NIC_RXFSTR, 0x1a48
.equ RTL8367_1A5C_NIC_DROP_MODE, 0x1a5c
.equ RTL8367_1B00_LED_SYS_CONFIG, 0x1b00
.equ RTL8367_1B03_LED_CONFIGURATION, 0x1b03
.equ RTL8367_1B08_CPU_FORCE_LED0_CFG0, 0x1b08
.equ RTL8367_1B0A_CPU_FORCE_LED1_CFG0, 0x1b0a
.equ RTL8367_1B0C_CPU_FORCE_LED2_CFG0, 0x1b0c
.equ RTL8367_1B24_PARA_LED_IO_EN1, 0x1b24
.equ RTL8367_1B25_PARA_LED_IO_EN2, 0x1b25
.equ RTL8367_1D15_GPHY_OCP_MSB_0, 0x1d15
.equ RTL8367_1D19_GPIO_I_X0, 0x1d19
.equ RTL8367_1D1D_GPIO_O_X0, 0x1d1d
.equ RTL8367_1D21_GPIO_OE_X0, 0x1d21
.equ RTL8367_1D25_GPIO_MODE_X0, 0x1d25
.equ RTL8367_1D32_IO_MISC_FUNC, 0x1d32
.equ RTL8367_1D3F_TO_ECO2, 0x1d3f
.equ RTL8367_2000_PHY_BASE, 0x2000

.equ RAM_END, 0xc000

;--------------------------------------------------------
; reset & interrupt vectors
;--------------------------------------------------------
	.area RESET (ABS,CODE)
	.org 0
	ljmp system_init


	.area CSEG (CODE)

serial_hex:
	xch A, B
	mov A, B
	swap A
	anl A, #0xf
	acall serial_hex_nibble
	mov A, B
	anl A, #0xf
	; fall through to serial_hex_nibble

serial_hex_nibble:
	clr C
	subb A, #0xa
	jc serial_hex_digit
	add A, #0x27 ; 'a' - '0' - 0xa
serial_hex_digit:
	add A, #0x3a ; '0' + 0xa
	; fall through to serial_write

serial_write:
	clr TI
	mov SBUF, A
serial_write_wait:
	jnb TI,serial_write_wait
	ret

serial_puts:
	clr A
	movc A, @A+DPTR
	jz serial_end
	inc DPTR
	acall serial_write
	sjmp serial_puts
serial_end:
	ret

serial_newline:
	mov A, #13
	acall serial_write
	mov A, #10
	acall serial_write
	ret

read_byte:
	mov A, R7
	jz read_code_byte
	dec A
	jz read_data_byte
read_stack_byte:
	mov A, DPL
	add A, #1
	mov SSPL, A
	mov A, DPH
	addc A, #0
	mov SSPH, A
	mov A, SSIO
	ret
read_code_byte:
	clr A
	movc A, @A+DPTR
	ret
read_data_byte:
	movx A, @DPTR
	ret

hexaddr:
	mov A, DPH
	acall serial_hex
	mov A, DPL
	acall serial_hex
	mov A, #':'
	acall serial_write
	ret

hexdump:
	setb F0
	mov R2, #16
hexdump_loop:
	jnb F0, hexdump_noaddr
	acall hexaddr
	clr F0
hexdump_noaddr:
	mov A, #' '
	acall serial_write
	acall read_byte
	acall serial_hex
	inc DPTR
	djnz R2, hexdump_nonewline
	mov R2, #16
	acall serial_newline
	setb F0
hexdump_nonewline:
	clr C
	mov A, R1
	subb A, #1
	mov R1, A
	mov A, R0
	subb A, #0
	mov R0, A
	orl A, R1
	jnz hexdump_loop
	ret

hexdump_partial:
	mov DPTR, #0x0000
	mov R0, #0x01
	mov R1, #0x00
	acall hexdump
	mov DPTR, #0x4000
	mov R0, #0x01
	mov R1, #0x00
	acall hexdump
	mov DPTR, #0x8000
	mov R0, #0x01
	mov R1, #0x00
	acall hexdump
	mov DPTR, #0xc000
	mov R0, #0x01
	mov R1, #0x00
	sjmp hexdump

reg_read:
	mov INDACC_ADDR_H, DPH
	mov INDACC_ADDR_L, DPL
	mov INDACC_CMD, #SFR_EXEC_READ_REG
reg_status_loop:
	mov A, INDACC_STATUS
	cjne A, #0, reg_status_loop
	ret

reg_write:
	mov INDACC_ADDR_H, DPH
	mov INDACC_ADDR_L, DPL
	mov INDACC_CMD, #SFR_EXEC_WRITE_REG
	sjmp reg_status_loop

reg_from_initlist_done:
	ret

reg_from_initlist:
	clr A
	movc A, @A+DPTR
	inc DPTR
	mov B, A
	clr ACC.7
	mov INDACC_ADDR_H, A
	clr A
	movc A, @A+DPTR
	inc DPTR
	mov INDACC_ADDR_L, A
	jb B.7, reg_from_initlist_rmw
	orl A, INDACC_ADDR_H
	jz reg_from_initlist_done

	clr A
	movc A, @A+DPTR
	inc DPTR
	mov INDACC_WDATA_H, A
	clr A
	movc A, @A+DPTR
	inc DPTR
	mov INDACC_WDATA_L, A
reg_from_initlist_write:
	mov INDACC_CMD, #SFR_EXEC_WRITE_REG
	acall reg_status_loop
	sjmp reg_from_initlist

reg_from_initlist_rmw:
	mov INDACC_CMD, #SFR_EXEC_READ_REG
	acall reg_status_loop

	clr A
	movc A, @A+DPTR
	inc DPTR
	anl A, INDACC_RDATA_H
	mov INDACC_WDATA_H, A
	clr A
	movc A, @A+DPTR
	inc DPTR
	anl A, INDACC_RDATA_L
	mov INDACC_WDATA_L, A
	clr A
	movc A, @A+DPTR
	inc DPTR
	orl A, INDACC_WDATA_H
	mov INDACC_WDATA_H, A
	clr A
	movc A, @A+DPTR
	inc DPTR
	orl A, INDACC_WDATA_L
	mov INDACC_WDATA_L, A
	sjmp reg_from_initlist_write

port_init_eee:
	mov DPTR, #RTL8367_0018_PORT0_EEECFG
	mov A, R7
	mov B, #0x20
	mul AB
	push B
	mov B, A
	mov A, DPL
	add A, B
	mov DPL, A
	mov A, DPH
	pop B
	addc A, B
	mov DPH, A
	acall reg_read
	mov A, INDACC_RDATA_H
	orl A, #0x0f  ; set EEE_RX, EEE_TX, EEE_GIGA_500M, EEE_100M
	mov INDACC_WDATA_H, A
	mov A, INDACC_RDATA_L
	mov INDACC_WDATA_L, A
	acall reg_write
	ret

phyocp_reg_addr:
	mov R6, DPH
	mov R5, DPL

	; copy ocpAddrPrefix bits ((addr & 0xfc00) >> 10) into RTL8367C_CFG_DW8051_OCPADR_MSB_MASK
	mov DPTR, #RTL8367_1D15_GPHY_OCP_MSB_0
	acall reg_read
	mov A, INDACC_RDATA_H
	mov INDACC_WDATA_H, A
	mov A, INDACC_RDATA_L
	anl A, #0xc0
	mov B, A
	mov A, R6
	rr A
	rr A
	anl A, #0x3f
	orl A, B
	mov INDACC_WDATA_L, A
	acall reg_write

	; PHY_BASE + port * 0x20
	mov DPTR, #RTL8367_2000_PHY_BASE
	mov A, R7
	mov B, #0x20
	mul AB
	push B
	mov B, A
	mov A, DPL
	add A, B
	mov DPL, A
	mov A, DPH
	pop B
	addc A, B
	mov DPH, A

	; ocpAddr5_1
	mov B, DPL
	mov A, R5
	rrc A
	anl A, #0x1f
	add A, B
	mov DPL, A
	mov A, DPH
	addc A, #0
	mov DPH, A

	; ocpAddr9_6
	mov A, R6
	rrc A
	mov R6, A
	mov A, R5
	rrc A
	mov R5, A
	mov A, R6
	rrc A
	mov R6, A
	mov A, R5
	rrc A
	swap A
	anl A, #0x0f
	add A, DPH
	mov DPH, A
	ret

port_init_phyocp:
	mov DPTR, #0xA428
	acall phyocp_reg_addr
	; regData &= ~(0x0200);
	acall reg_read
	mov A, INDACC_RDATA_H
	anl A, #0xfd
	mov INDACC_WDATA_H, A
	mov A, INDACC_RDATA_L
	mov INDACC_WDATA_L, A
	acall reg_write
	ret

port_init1:
	mov R7, #0
port_init1_loop:
	acall port_init_eee
	acall port_init_phyocp
	inc R7
	cjne R7, #6, port_init1_loop
	ret

dump_reg:
	acall hexaddr
	acall reg_read
	mov A, INDACC_RDATA_H
	acall serial_hex
	mov A, INDACC_RDATA_L
	acall serial_hex
	ajmp serial_newline

system_init:
	mov sp, #MAIN_STACK
	mov DATA_BANK, #0
	mov SSPL, #0x80
	mov SSPH, #0x80

	mov DPTR, #reg_initlist
	acall reg_from_initlist

	mov DPTR, #RTL8367_1366_DW8051_RDY
	acall reg_read
	; Switch from default 20.833MHz (rate 5) to 62.5MHz (rate 4) CPU clock.
	mov A, #0x41 ; RATE 4, DW8051_READY
	mov INDACC_WDATA_L, A
	mov A, INDACC_RDATA_H
	mov INDACC_WDATA_H, A
	acall reg_write

	; Power-up clock seems to be 20.833MHz (125MHz / 6)

	mov CKCON, #0x10  ; No XRAM stretch cycles, timers run at ck/12, timer1 at ck/4

	; Timer 0: Mode 1, 16bit counter
	; Timer 1: Mode 2, 8bit counter with auto-reload
	mov TMOD, #0x21
	mov TCON, #0x50  ; Enable timer 0 and timer 1, no interrupts
	mov TH1, #0xef ; 57600 baud @ 62.5MHz

	mov RCAP2H, #0xff
	;mov RCAP2L, #0xf5 ; 57600 baud @ 20.8333MHz
	mov RCAP2L, #0xef ; 115200 baud @ 62.5MHz
	;mov T2CON, #0x34 ; Timer2 as RX/TX clock; Timer 2 enabled
	mov T2CON, #0x00  ; Timer1 as RX/TX clock; Timer 2 disabled

	mov SCON, #0x50   ; Serial port mode 1, receive enable
	mov PCON, #0x80   ; Double the baud rate (div 16 instead of div 32)

	; Say hello
	mov DPTR, #message_hello
	acall serial_puts

	; Initialize switch
	acall port_init1
	mov DPTR, #reg_initlist2
	acall reg_from_initlist
	mov DPTR, #message_switch_done
	acall serial_puts

	; dump registers
	; note: unimplemented register addresses take longish to read (wait for a timeout)
	mov DPTR, #0
dump_register_loop:
	lcall hexaddr
	lcall reg_read
	mov A, INDACC_RDATA_H
	lcall serial_hex
	mov A, INDACC_RDATA_L
	lcall serial_hex
	lcall serial_newline
	inc DPTR
	mov A, DPL
	orl A, DPH
	jnz dump_register_loop

halt:
	mov DPTR, #message_halt
	acall serial_puts
haltloop:
	sjmp haltloop

reg_initlist:
	.word RTL8367_1D25_GPIO_MODE_X0 | 0x8000
	.word 0xffff
	.word 0x0040
	.word RTL8367_1D21_GPIO_OE_X0 | 0x8000
	.word 0xffff
	.word 0x0040
	.word RTL8367_1D1D_GPIO_O_X0 | 0x8000
	.word 0xffbf
	.word 0x0000
	.word RTL8367_1D32_IO_MISC_FUNC
	.word 0x0004  ; Set UART_EN
	.word RTL8367_1D3F_TO_ECO2
	.word 0x0100
	.word RTL8367_1B00_LED_SYS_CONFIG
	.word 0x1472  ; non-serial, eee lpi en, eee cap 10, led_poweron_0, led_poweron_1, led_poweron_2, led_select 2
	.word RTL8367_1B03_LED_CONFIGURATION
	.word 0x0096
	.word RTL8367_130C_MISC_CFG0
	.word 0x1070
	.word RTL8367_1366_DW8051_RDY
	; Switch from default 20.833MHz (rate 5) to 62.5MHz (rate 4) CPU clock.
	;mov A, #0x41 ; RATE 4, DW8051_READY
	.word 0x0041
	.word RTL8367_1B24_PARA_LED_IO_EN1
	.word 0xffff
	.word RTL8367_1B25_PARA_LED_IO_EN2
	.word 0xffff
	.word RTL8367_1B08_CPU_FORCE_LED0_CFG0
	.word 0
	.word RTL8367_1B0A_CPU_FORCE_LED1_CFG0
	.word 0
	.word RTL8367_1322_CHIP_RESET
	.word 0x0020 ; NIC_RST
	.word RTL8367_130C_MISC_CFG0
	.word 0x1470 ; +NIC_ENABLE
	.word RTL8367_1A46_NIC_RXSTOPRL
	.word 0xff
	.word RTL8367_1A47_NIC_RXSTOPRH
	.word 0x02
	.word RTL8367_1A44_NIC_TXSTOPRL
	.word 0xfe
	.word RTL8367_1A45_NIC_TXSTOPRH
	.word 0x03
	.word RTL8367_1A48_NIC_RXFSTR
	.word 4
	.word RTL8367_1A17_NIC_GCR
	.word 0
	.word RTL8367_1A5C_NIC_DROP_MODE
	.word 1
	.word RTL8367_1A16_NIC_TXCR
	.word 1
	.word RTL8367_1A15_NIC_RXCR1
	.word 3
	.word 0  ; EOL

reg_initlist2:
	.word 0x13eb, 0x15bb ; 13eb_UTP_FIB_DET
	.word 0x1303, 0x06d6 ; 1303_CHIP_DEBUG0
	.word 0x1304, 0x0700 ; 1304_CHIP_DEBUG1
	.word 0x13e2, 0x003f ; 13e2_CHIP_DEBUG2
	.word 0x13F9, 0x0090 ; 13f9_EXT_TXC_DLY
	.word 0x121e, 0x03CA ; 121e_FLOWCTRL_ALL_ON
	.word 0x1233, 0x0352 ; 1233_FLOWCTRL_JUMBO_SYS_ON
	.word 0x1237, 0x00a0 ; 1237_FLOWCTRL_JUMBO_PORT_ON
	.word 0x123a, 0x0030 ; 123A_FLOWCTRL_JUMBO_PORT_PRIVATE_OFF
	.word 0x1239, 0x0084 ; 1239_FLOWCTRL_JUMBO_PORT_PRIVATE_ON
	.word 0x0301, 0x1000 ; 0301_SCHEDULE_WFQ_BURST_SIZE
	.word 0x1349, 0x001F ; 1349_BYPASS_ABLTY_LOCK
	.word 0x18e0 | 0x8000 ; RLDP_CTRL0 "Realtek Loop Detection Protocol"
	.word 0xfffe, 0x0000 ; Clear RLDP_ENABLE
	.word 0x122b | 0x8000 ; RRCP_CTRL0 "Realtek Remote Control Protocol"
	.word 0xffff, 0x4000 ; Set COL_SEL
	.word 0x1305 | 0x8000 ; DIGITAL_INTERFACE_SELECT
	.word 0x3fff, 0xc000 ; Set ORG_COL, ORG_CRS
	; Set Old max packet length to 16K
	.word 0x1200 | 0x8000 ; MAX_LENGTH_LIMINT_IPG
	.word 0xffff, 0x6000 ; Set MAX_LENTH_CTR
	.word 0x0884 | 0x8000 ; MAX_LEN_RX_TX
	.word 0xffff, 0x0003 ; Set MAX_LEN_RX_TX
	.word 0x06eb | 0x8000 ; ACL_ACCESS_MODE
	.word 0xffff, 0x0001 ; Set ACL_ACCESS_MODE
	.word 0x03fa, 0x0007 ; 03fa_LINE_RATE_HSG_H
	.word 0x08c8 | 0x8000 ; 08c8_PORT_SECURITY_CTRL
	.word 0xffff, 0x00c0 ; Set UNKNOWN_UNICAST_DA_BEHAVE
	.word 0x0a30 | 0x8000 ; 0a30_LUT_CFG
	.word 0xffff, 0x0008 ; Set LUT_IPMC_LOOKUP_OP
	.word 0x1d32 | 0x8000 ; 1d32_IO_MISC_FUNC
	.word 0xffff, 0x0002 ; Set INT_EN

	.word 0  ; EOL

message_hello:
.asciz "Hello world!\r\n"
message_switch_done:
.asciz "Switch initialized!\r\n"
message_halt:
.asciz "Halting.\r\n"
