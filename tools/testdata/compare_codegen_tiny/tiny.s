        .syntax unified
        .thumb
        .section .text,"ax",%progbits
        .global f
        .type f,%function
f:      bx lr
        .size f, .-f
        .section .rodata,"a",%progbits
        .global kRo
kRo:    .word 0x11111111, f
        .size kRo, 8
        .section .data,"aw",%progbits
        .global kRw
kRw:    .word 0x22222222
        .size kRw, 4
        .section .init_array,"aw",%init_array
        .word f
        .section .sdram_init,"aw",%progbits
        .incbin "sdram.bin", 0, 48
        .section .sdram_exidx,"aw",%progbits
        .incbin "sdram.bin", 48, 16
        .section .sdram_init_pack,"a",%progbits
        .incbin "pack.bin"
