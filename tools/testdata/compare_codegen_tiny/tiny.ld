SECTIONS {
  .text 0x20001000 : { *(.text) }
  .rodata 0x20002000 : { *(.rodata) }
  .data 0x20003000 : { *(.data) }
  .init_array 0x20004000 : { *(.init_array) }
  .sdram_init 0x0c000000 : AT(0x20010000) { *(.sdram_init) }
  .sdram_exidx 0x0c000030 : AT(0x20010030) { *(.sdram_exidx) }
  .sdram_init_pack 0x20005000 : { *(.sdram_init_pack) }
}
