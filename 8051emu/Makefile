CFLAGS := -g

all: emu51

emu51: main.c core.c core_gen.c Makefile
	$(CC) $(CFLAGS) -o $@ main.c core.c -llua5.3 -lpthread

core_gen.c: gen allops.txt
	./gen > $@

genops.txt: genops.py
	./genops.py > $@

allops.txt: ops.txt genops.txt
	cat $+ > $@

%.s : %.c
	$(CC) $(CFLAGS) -S -fverbose-asm -o $@ $<
