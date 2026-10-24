/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 AERA | Erez Yaary */
/* TLS.OVL: text (+rodata) | data | bss, linked at 0 like a program
   (atari/link.ld), with head.c's TLS_OVL table first: the program finds
   it at the start of the text. elf2tos.py writes the fixups. */
ENTRY(tlsovl)
SECTIONS
{
  . = 0;
  .text : {
    KEEP(*(.tlsovl))
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  }
  .data : {
    *(.data .data.*)
    . = ALIGN(4);
  }
  .bss : {
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gnu*) }
}
