all: hello.bin

clean:
	rm *.bin *.rel *.lst

hello.bin: hello.ihx

.SUFFIXES: .asm .s .bin .c .ihx .rel

.ihx.bin:
	hex2bin $<

.asm.ihx:
	as31 -Fhex -l -O$@ $<

.s.rel:
	sdas8051 -ols $<

.c.rel:
	sdcc -c $(SDCCFLAGS) $<
	mv $(shell echo $< | sed -e 's@\.c@\.asm@') $(shell echo $< | sed -e 's@\.c@\.casm@')

.rel.ihx:
	sdld -nmuwxMY -i $(shell echo $< | sed -e 's@\.c@\.ihx@') $(shell echo $< | sed -e 's@\.c@\.rel@')

SDCCFLAGS := --all-callee-saves --model-small --opt-code-size --fomit-frame-pointer --lospre-unsafe-read
