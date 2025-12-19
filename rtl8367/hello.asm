;-----------------------------------------------------------------------------
; RTL8372 8051 firmware test.
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

.equ RTL8367_IO_MISC_FUNC, 0x1d32

.equ RAM_END, 0xc000

.org 0
reset:
	ljmp system_init

serial_hex:
	xch A, B
	mov A, B
	swap A
	anl A, #0xf
	lcall serial_hex_nibble
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
	movc A, @A + DPTR
	jz serial_end
	inc DPTR
	lcall serial_write
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

system_init:
	mov sp, #MAIN_STACK
	mov DATA_BANK, #0
	mov SSPL, #0x80
	mov SSPH, #0x80

	mov DPTR, #RTL8367_IO_MISC_FUNC
	lcall reg_read
	mov A, INDACC_RDATA_L
	setb ACC.2  ; Set UART_EN
	mov INDACC_WDATA_L, A
	mov A, INDACC_RDATA_H
	mov INDACC_WDATA_H, A
	lcall reg_write

	; Power-up clock seems to be 20.833MHz (125MHz / 6)

	mov RCAP2H, #0xff
	mov RCAP2L, #0xf5 ; 57600 baud @ 20.8333MHz
	mov T2CON, #0x34
	mov SCON, #0x52
	mov A, PCON
	setb ACC.7        ; Double the baud rate (div 16 instead of div 32)
	mov PCON, A
	mov TH2, #0xff
	mov TL2, #0xff

	; Say hello
	mov DPTR, #message_hello
	lcall serial_puts

	; Fill XRAM
	mov DATA_BANK, #0
	mov DPTR, #0
xram_fill_loop:
	mov A, DPL
	movx @DPTR, A
	inc DPTR
	mov A, DPH
	movx @DPTR, A
	inc DPTR
	mov A, DPH
	cjne A, #0xc0, xram_fill_loop

	; Can we use SPC_FNC to write to code ram?
	mov DPTR, #0
	mov A, #0x5a
	mov SPC_FNC, #1
	movx @DPTR, A
	inc DPTR
	mov A, #0xb4
	mov SPC_FNC, #1
	movx @DPTR, A
	mov SPC_FNC, #0


	; dump code memory
	mov R7, #0  ; hexdump code memory

	mov DPTR, #message_dumpcode
	lcall serial_puts

	mov CODE_BANK, #0
	mov DPTR, #message_bank0
	lcall serial_puts
	lcall hexdump_partial

	mov CODE_BANK, #1
	mov DPTR, #message_bank1
	lcall serial_puts
	lcall hexdump_partial

	mov CODE_BANK, #2
	mov DPTR, #message_bank2
	lcall serial_puts
	lcall hexdump_partial

	; dump xram memory
	mov R7, #1  ; hexdump XRAM

	mov DPTR, #message_dumpxram
	lcall serial_puts

	mov DATA_BANK, #0
	mov DPTR, #message_bank0
	lcall serial_puts
	lcall hexdump_partial

	mov DATA_BANK, #1
	mov DPTR, #message_bank1
	lcall serial_puts
	lcall hexdump_partial

	mov DATA_BANK, #2
	mov DPTR, #message_bank2
	lcall serial_puts
	lcall hexdump_partial

	; dump via second stack
	mov R7, #2  ; hexdump via SSIO

	mov DPTR, #message_dumpxstack
	lcall serial_puts
	lcall hexdump_partial

	; dump registers
	; register address seem to be word aligned
	mov DPTR, #0
dump_register_loop:
	lcall hexaddr
	lcall reg_read
	mov A, INDACC_WDATA_H
	lcall serial_hex
	mov A, INDACC_WDATA_L
	lcall serial_hex
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
	sjmp halt


message_hello:
.byte "Hello world!\r\n", 0
message_dumpcode:
.byte "Dumping code\r\n", 0
message_dumpxram:
.byte "Dumping XRAM\r\n", 0
message_dumpxstack:
.byte "Dumping via SSIO\r\n", 0
message_bank0:
.byte "bank0:\r\n", 0
message_bank1:
.byte "bank1:\r\n", 0
message_bank2:
.byte "bank2:\r\n", 0
