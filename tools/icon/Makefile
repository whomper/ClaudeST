# RSCTEST.PRG: loads ../../icons/CLAUDE.RSC and draws it (a check, not shipped)
CROSS  ?= m68k-linux-gnu-
CC     := $(CROSS)gcc
CFLAGS := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-tree-loop-distribute-patterns -fno-store-merging -fno-asynchronous-unwind-tables -Wall
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,../../st/link.ld \
          -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

RSCTEST.PRG: rsctest.elf
	python3 ../elf2tos.py $< $@
rsctest.elf: crt0.o rsctest.o gem.o tos.o
	$(CC) $(LDFLAGS) -o $@ $^
crt0.o: ../../st/crt0.S
	$(CC) -m68000 -c -o $@ $<
gem.o: ../../st/gem.c
	$(CC) $(CFLAGS) -c -o $@ $<
tos.o: ../../st/tos.c
	$(CC) $(CFLAGS) -c -o $@ $<
rsctest.o: rsctest.c
	$(CC) $(CFLAGS) -c -o $@ $<
clean:
	rm -f *.o *.elf RSCTEST.PRG
