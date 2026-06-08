
/*
 *  linux/arch/arm/mm/cache-v7.S
 *
 *  Copyright (C) 2001 Deep Blue Solutions Ltd.
 *  Copyright (C) 2005 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This is the "shell" of the ARMv7 processor support.
 */


         .macro  dcache_line_size, reg, tmp
         mrc     p15, 0, \tmp, c0, c0, 1         @ read ctr
         lsr     \tmp, \tmp, #16
         and     \tmp, \tmp, #0xf                @ cache line size encoding
         mov     \reg, #4                        @ bytes per word
         mov     \reg, \reg, lsl \tmp            @ actual cache line size
         .endm


	.text
	.code 32

	.global v7_dma_inv_range

	.func v7_dma_inv_range
	.type v7_dma_inv_range, %function

v7_dma_inv_range:
         dcache_line_size r2, r3
         sub     r3, r2, #1
         tst     r0, r3
         bic     r0, r0, r3
#ifdef CONFIG_ARM_ERRATA_764369
         ALT_SMP(W(dsb))
         ALT_UP(W(nop))
#endif
         mcrne   p15, 0, r0, c7, c14, 1          @ clean & invalidate D / U line

         tst     r1, r3
         bic     r1, r1, r3
         mcrne   p15, 0, r1, c7, c14, 1          @ clean & invalidate D / U line
 1:
         mcr     p15, 0, r0, c7, c6, 1           @ invalidate D / U line
         add     r0, r0, r2
         cmp     r0, r1
         blo     1b
         dsb     st
         bx     lr
	.endfunc

	.global v7_dma_flush_range

	.func v7_dma_flush_range
	.type v7_dma_flush_range, %function

v7_dma_flush_range:
		dcache_line_size r2, r3
		sub     r3, r2, #1
		bic     r0, r0, r3
	#ifdef CONFIG_ARM_ERRATA_764369
		ALT_SMP(W(dsb))
		ALT_UP(W(nop))
	#endif
		1:
		mcr     p15, 0, r0, c7, c14, 1          @ clean & invalidate D / U line
		add     r0, r0, r2
		cmp     r0, r1
		blo     1b
		dsb
		mov     pc, lr
	.endfunc

	.align 4
	.end
