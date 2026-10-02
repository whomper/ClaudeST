/* Flat TOS image: text (+rodata) | data | bss, linked at address 0.
   elf2tos.py turns the --emit-relocs output into a relocatable PRG. */
ENTRY(_start)
SECTIONS
{
  . = 0;
  .text : {
    *fstart.o(.text)
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
