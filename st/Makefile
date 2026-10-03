# Claude ST - builds CLAUDE.PRG with a stock m68k ELF cross compiler
# (Debian/Ubuntu: apt install gcc-m68k-linux-gnu). No MiNTLib needed:
# the program is freestanding and elf2tos.py writes the TOS header.
# An m68k-atari-mint toolchain works too: make CROSS=m68k-atari-mint-

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,link.ld -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

OBJS := crt0.o claude.o gem.o tos.o sting.o sting_s.o bidi.o

all: CLAUDE.PRG

CLAUDE.PRG: claude.elf ../tools/elf2tos.py
	python3 ../tools/elf2tos.py $< $@

claude.elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c tos.h gem.h sting.h bidi.h icon.h
	$(CC) $(CFLAGS) -c -o $@ $<

crt0.o: crt0.S
	$(CC) -m68000 -c -o $@ $<

clean:
	rm -f *.o claude.elf CLAUDE.PRG

.PHONY: all clean

sting_s.o: sting.S
	$(CC) -m68000 -c -o $@ $<

# host-side unit test for the right-to-left layout
test: bidi_test.c bidi.c bidi.h
	cc -O2 -Wall -o /tmp/bidi_test bidi_test.c bidi.c && /tmp/bidi_test
.PHONY: test
