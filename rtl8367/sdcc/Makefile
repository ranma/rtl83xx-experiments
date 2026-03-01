all: hello.bin

clean:
	rm -f *.bin *.rel *.lst *.mem *.omf *.cdb *.lk *.map *.sym *.rst *.ihx *.asm

hello.bin: hello.ihx

.SUFFIXES: .s .bin .c .ihx .rel

.ihx.bin:
	hex2bin $<

.s.rel:
	sdas8051 -ols $<

.c.rel:
	sdcc -c $(SDCCFLAGS) $<

.rel.ihx:
	sdcc $(SDCCFLAGS) $+

SDCCFLAGS := --std=c11 --model-small --opt-code-size
