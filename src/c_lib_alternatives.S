@ Taken from https://github.com/ARM-software/optimized-routines/blob/master/string/arm/memset.S
/*
 * memset - fill memory with a constant
 *
 * Copyright (c) 2010-2021, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

/*
   Written by Dave Gilbert <david.gilbert@linaro.org>

   This memset routine is optimised on a Cortex-A9 and should work on
   all ARMv7 processors.

 */

	.syntax unified
	.arch armv7-a

@ 2011-08-30 david.gilbert@linaro.org
@    Extracted from local git 2f11b436

@ this lets us check a flag in a 00/ff byte easily in either endianness
#ifdef __ARMEB__
#define CHARTSTMASK(c) 1<<(31-(c*8))
#else
#define CHARTSTMASK(c) 1<<(c*8)
#endif
	.thumb

@ ---------------------------------------------------------------------------
	.thumb_func
	.align 2
	.p2align 4,,15
	.global memset
	.type memset,%function
memset:
	@ r0 = address
	@ r1 = character
	@ r2 = count
	@ returns original address in r0

	mov	r3, r0		@ Leave r0 alone
	cbz	r2, 10f		@ Exit if 0 length

	tst	r0, #7
	beq	2f		@ Already aligned

	@ Ok, so we're misaligned here
1:
	strb	r1, [r3], #1
	subs	r2,r2,#1
	tst	r3, #7
	cbz	r2, 10f		@ Exit if we hit the end
	bne	1b		@ go round again if still misaligned

2:
	@ OK, so we're aligned
	push	{r4,r5,r6,r7}
	bics	r4, r2, #15	@ if less than 16 bytes then need to finish it off
	beq	5f

3:
	@ POSIX says that ch is cast to an unsigned char.  A uxtb is one
	@ byte and takes two cycles, where an AND is four bytes but one
	@ cycle.
	and	r1, #0xFF
	orr	r1, r1, r1, lsl#8	@ Same character into all bytes
	orr	r1, r1, r1, lsl#16
	mov	r5,r1
	mov	r6,r1
	mov	r7,r1

4:
	subs	r4,r4,#16
	stmia	r3!,{r1,r5,r6,r7}
	bne	4b
	and	r2,r2,#15

	@ At this point we're still aligned and we have upto align-1 bytes left to right
	@ we can avoid some of the byte-at-a time now by testing for some big chunks
	tst	r2,#8
	itt	ne
	subne	r2,r2,#8
	stmiane	r3!,{r1,r5}

5:
	pop	{r4,r5,r6,r7}
	cbz	r2, 10f

	@ Got to do any last < alignment bytes
6:
	subs	r2,r2,#1
	strb	r1,[r3],#1
	bne	6b

10:
	bx	lr		@ goodbye
	.size	memset, . - memset


@ ---------------------------------------------------------------------------
@ Taken from https://github.com/ARM-software/optimized-routines/blob/master/string/arm/memcpy.S

#include "c_lib_alternatives.h"

	.syntax unified
	/* This implementation requires ARM state.  */
	.arm

#ifdef __ARM_NEON__

	.fpu	neon
	.arch	armv7-a
# define FRAME_SIZE	4
# define USE_VFP
# define USE_NEON

#elif !defined (__SOFTFP__)

	.arch	armv6
	.fpu	vfpv2
# define FRAME_SIZE	32
# define USE_VFP

#else
	.arch	armv6
# define FRAME_SIZE    32

#endif

/* Old versions of GAS incorrectly implement the NEON align semantics.  */
#ifdef BROKEN_ASM_NEON_ALIGN
#define ALIGN(addr, align) addr,:align
#else
#define ALIGN(addr, align) addr:align
#endif

#define PC_OFFSET	8	/* PC pipeline compensation.  */
#define INSN_SIZE	4

/* Call parameters.  */
#define dstin	r0
#define src	r1
#define count	r2

/* Locals.  */
#define tmp1	r3
#define dst	ip
#define tmp2	r10

#ifndef USE_NEON
/* For bulk copies using GP registers.  */
#define	A_l	r2		/* Call-clobbered.  */
#define	A_h	r3		/* Call-clobbered.  */
#define	B_l	r4
#define	B_h	r5
#define	C_l	r6
#define	C_h	r7
#define	D_l	r8
#define	D_h	r9
#endif

/* Number of lines ahead to pre-fetch data.  If you change this the code
   below will need adjustment to compensate.  */

#define prefetch_lines	5

#ifdef USE_VFP
	.macro	cpy_line_vfp vreg, base
	vstr	\vreg, [dst, #\base]
	vldr	\vreg, [src, #\base]
	vstr	d0, [dst, #\base + 8]
	vldr	d0, [src, #\base + 8]
	vstr	d1, [dst, #\base + 16]
	vldr	d1, [src, #\base + 16]
	vstr	d2, [dst, #\base + 24]
	vldr	d2, [src, #\base + 24]
	vstr	\vreg, [dst, #\base + 32]
	vldr	\vreg, [src, #\base + prefetch_lines * 64 - 32]
	vstr	d0, [dst, #\base + 40]
	vldr	d0, [src, #\base + 40]
	vstr	d1, [dst, #\base + 48]
	vldr	d1, [src, #\base + 48]
	vstr	d2, [dst, #\base + 56]
	vldr	d2, [src, #\base + 56]
	.endm

	.macro	cpy_tail_vfp vreg, base
	vstr	\vreg, [dst, #\base]
	vldr	\vreg, [src, #\base]
	vstr	d0, [dst, #\base + 8]
	vldr	d0, [src, #\base + 8]
	vstr	d1, [dst, #\base + 16]
	vldr	d1, [src, #\base + 16]
	vstr	d2, [dst, #\base + 24]
	vldr	d2, [src, #\base + 24]
	vstr	\vreg, [dst, #\base + 32]
	vstr	d0, [dst, #\base + 40]
	vldr	d0, [src, #\base + 40]
	vstr	d1, [dst, #\base + 48]
	vldr	d1, [src, #\base + 48]
	vstr	d2, [dst, #\base + 56]
	vldr	d2, [src, #\base + 56]
	.endm
#endif

ENTRY (memcpy)

	mov	dst, dstin	/* Preserve dstin, we need to return it.  */
	cmp	count, #64
	bhs	L(cpy_not_short)
	/* Deal with small copies quickly by dropping straight into the
	   exit block.  */

L(tail63unaligned):
#ifdef USE_NEON
	and	tmp1, count, #0x38
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1
	vld1.8	{d0}, [src]!	/* 14 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 12 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 10 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 8 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 6 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 4 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 2 words to go.  */
	vst1.8	{d0}, [dst]!

	tst	count, #4
	ldrne	tmp1, [src], #4
	strne	tmp1, [dst], #4
#else
	/* Copy up to 15 full words of data.  May not be aligned.  */
	/* Cannot use VFP for unaligned data.  */
	and	tmp1, count, #0x3c
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(60 - PC_OFFSET/2 + INSN_SIZE/2)
	/* Jump directly into the sequence below at the correct offset.  */
	add	pc, pc, tmp1, lsl #1

	ldr	tmp1, [src, #-60]	/* 15 words to go.  */
	str	tmp1, [dst, #-60]

	ldr	tmp1, [src, #-56]	/* 14 words to go.  */
	str	tmp1, [dst, #-56]
	ldr	tmp1, [src, #-52]
	str	tmp1, [dst, #-52]

	ldr	tmp1, [src, #-48]	/* 12 words to go.  */
	str	tmp1, [dst, #-48]
	ldr	tmp1, [src, #-44]
	str	tmp1, [dst, #-44]

	ldr	tmp1, [src, #-40]	/* 10 words to go.  */
	str	tmp1, [dst, #-40]
	ldr	tmp1, [src, #-36]
	str	tmp1, [dst, #-36]

	ldr	tmp1, [src, #-32]	/* 8 words to go.  */
	str	tmp1, [dst, #-32]
	ldr	tmp1, [src, #-28]
	str	tmp1, [dst, #-28]

	ldr	tmp1, [src, #-24]	/* 6 words to go.  */
	str	tmp1, [dst, #-24]
	ldr	tmp1, [src, #-20]
	str	tmp1, [dst, #-20]

	ldr	tmp1, [src, #-16]	/* 4 words to go.  */
	str	tmp1, [dst, #-16]
	ldr	tmp1, [src, #-12]
	str	tmp1, [dst, #-12]

	ldr	tmp1, [src, #-8]	/* 2 words to go.  */
	str	tmp1, [dst, #-8]
	ldr	tmp1, [src, #-4]
	str	tmp1, [dst, #-4]
#endif

	lsls	count, count, #31
	ldrhcs	tmp1, [src], #2
	ldrbne	src, [src]		/* Src is dead, use as a scratch.  */
	strhcs	tmp1, [dst], #2
	strbne	src, [dst]
	bx	lr

L(cpy_not_short):
	/* At least 64 bytes to copy, but don't know the alignment yet.  */
	str	tmp2, [sp, #-FRAME_SIZE]!
	and	tmp2, src, #7
	and	tmp1, dst, #7
	cmp	tmp1, tmp2
	bne	L(cpy_notaligned)

#ifdef USE_VFP
	/* Magic dust alert!  Force VFP on Cortex-A9.  Experiments show
	   that the FP pipeline is much better at streaming loads and
	   stores.  This is outside the critical loop.  */
	vmov.f32	s0, s0
#endif

	/* SRC and DST have the same mutual 64-bit alignment, but we may
	   still need to pre-copy some bytes to get to natural alignment.
	   We bring SRC and DST into full 64-bit alignment.  */
	lsls	tmp2, dst, #29
	beq	1f
	rsbs	tmp2, tmp2, #0
	sub	count, count, tmp2, lsr #29
	ldrmi	tmp1, [src], #4
	strmi	tmp1, [dst], #4
	lsls	tmp2, tmp2, #2
	ldrhcs	tmp1, [src], #2
	ldrbne	tmp2, [src], #1
	strhcs	tmp1, [dst], #2
	strbne	tmp2, [dst], #1

1:
	subs	tmp2, count, #64	/* Use tmp2 for count.  */
	blo	L(tail63aligned)

	cmp	tmp2, #512
	bhs	L(cpy_body_long)

L(cpy_body_medium):			/* Count in tmp2.  */
#ifdef USE_VFP
1:
	vldr	d0, [src, #0]
	subs	tmp2, tmp2, #64
	vldr	d1, [src, #8]
	vstr	d0, [dst, #0]
	vldr	d0, [src, #16]
	vstr	d1, [dst, #8]
	vldr	d1, [src, #24]
	vstr	d0, [dst, #16]
	vldr	d0, [src, #32]
	vstr	d1, [dst, #24]
	vldr	d1, [src, #40]
	vstr	d0, [dst, #32]
	vldr	d0, [src, #48]
	vstr	d1, [dst, #40]
	vldr	d1, [src, #56]
	vstr	d0, [dst, #48]
	add	src, src, #64
	vstr	d1, [dst, #56]
	add	dst, dst, #64
	bhs	1b
	tst	tmp2, #0x3f
	beq	L(done)

L(tail63aligned):			/* Count in tmp2.  */
	and	tmp1, tmp2, #0x38
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1

	vldr	d0, [src, #-56]	/* 14 words to go.  */
	vstr	d0, [dst, #-56]
	vldr	d0, [src, #-48]	/* 12 words to go.  */
	vstr	d0, [dst, #-48]
	vldr	d0, [src, #-40]	/* 10 words to go.  */
	vstr	d0, [dst, #-40]
	vldr	d0, [src, #-32]	/* 8 words to go.  */
	vstr	d0, [dst, #-32]
	vldr	d0, [src, #-24]	/* 6 words to go.  */
	vstr	d0, [dst, #-24]
	vldr	d0, [src, #-16]	/* 4 words to go.  */
	vstr	d0, [dst, #-16]
	vldr	d0, [src, #-8]	/* 2 words to go.  */
	vstr	d0, [dst, #-8]
#else
	sub	src, src, #8
	sub	dst, dst, #8
1:
	ldrd	A_l, A_h, [src, #8]
	strd	A_l, A_h, [dst, #8]
	ldrd	A_l, A_h, [src, #16]
	strd	A_l, A_h, [dst, #16]
	ldrd	A_l, A_h, [src, #24]
	strd	A_l, A_h, [dst, #24]
	ldrd	A_l, A_h, [src, #32]
	strd	A_l, A_h, [dst, #32]
	ldrd	A_l, A_h, [src, #40]
	strd	A_l, A_h, [dst, #40]
	ldrd	A_l, A_h, [src, #48]
	strd	A_l, A_h, [dst, #48]
	ldrd	A_l, A_h, [src, #56]
	strd	A_l, A_h, [dst, #56]
	ldrd	A_l, A_h, [src, #64]!
	strd	A_l, A_h, [dst, #64]!
	subs	tmp2, tmp2, #64
	bhs	1b
	tst	tmp2, #0x3f
	bne	1f
	ldr	tmp2,[sp], #FRAME_SIZE
	bx	lr
1:
	add	src, src, #8
	add	dst, dst, #8

L(tail63aligned):			/* Count in tmp2.  */
	/* Copy up to 7 d-words of data.  Similar to Ltail63unaligned, but
	   we know that the src and dest are 64-bit aligned so we can use
	   LDRD/STRD to improve efficiency.  */
	/* TMP2 is now negative, but we don't care about that.  The bottom
	   six bits still tell us how many bytes are left to copy.  */

	and	tmp1, tmp2, #0x38
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1
	ldrd	A_l, A_h, [src, #-56]	/* 14 words to go.  */
	strd	A_l, A_h, [dst, #-56]
	ldrd	A_l, A_h, [src, #-48]	/* 12 words to go.  */
	strd	A_l, A_h, [dst, #-48]
	ldrd	A_l, A_h, [src, #-40]	/* 10 words to go.  */
	strd	A_l, A_h, [dst, #-40]
	ldrd	A_l, A_h, [src, #-32]	/* 8 words to go.  */
	strd	A_l, A_h, [dst, #-32]
	ldrd	A_l, A_h, [src, #-24]	/* 6 words to go.  */
	strd	A_l, A_h, [dst, #-24]
	ldrd	A_l, A_h, [src, #-16]	/* 4 words to go.  */
	strd	A_l, A_h, [dst, #-16]
	ldrd	A_l, A_h, [src, #-8]	/* 2 words to go.  */
	strd	A_l, A_h, [dst, #-8]

#endif
	tst	tmp2, #4
	ldrne	tmp1, [src], #4
	strne	tmp1, [dst], #4
	lsls	tmp2, tmp2, #31		/* Count (tmp2) now dead. */
	ldrhcs	tmp1, [src], #2
	ldrbne	tmp2, [src]
	strhcs	tmp1, [dst], #2
	strbne	tmp2, [dst]

L(done):
	ldr	tmp2, [sp], #FRAME_SIZE
	bx	lr

L(cpy_body_long):			/* Count in tmp2.  */

	/* Long copy.  We know that there's at least (prefetch_lines * 64)
	   bytes to go.  */
#ifdef USE_VFP
	/* Don't use PLD.  Instead, read some data in advance of the current
	   copy position into a register.  This should act like a PLD
	   operation but we won't have to repeat the transfer.  */

	vldr	d3, [src, #0]
	vldr	d4, [src, #64]
	vldr	d5, [src, #128]
	vldr	d6, [src, #192]
	vldr	d7, [src, #256]

	vldr	d0, [src, #8]
	vldr	d1, [src, #16]
	vldr	d2, [src, #24]
	add	src, src, #32

	subs	tmp2, tmp2, #prefetch_lines * 64 * 2
	blo	2f
1:
	cpy_line_vfp	d3, 0
	cpy_line_vfp	d4, 64
	cpy_line_vfp	d5, 128
	add	dst, dst, #3 * 64
	add	src, src, #3 * 64
	cpy_line_vfp	d6, 0
	cpy_line_vfp	d7, 64
	add	dst, dst, #2 * 64
	add	src, src, #2 * 64
	subs	tmp2, tmp2, #prefetch_lines * 64
	bhs	1b

2:
	cpy_tail_vfp	d3, 0
	cpy_tail_vfp	d4, 64
	cpy_tail_vfp	d5, 128
	add	src, src, #3 * 64
	add	dst, dst, #3 * 64
	cpy_tail_vfp	d6, 0
	vstr	d7, [dst, #64]
	vldr	d7, [src, #64]
	vstr	d0, [dst, #64 + 8]
	vldr	d0, [src, #64 + 8]
	vstr	d1, [dst, #64 + 16]
	vldr	d1, [src, #64 + 16]
	vstr	d2, [dst, #64 + 24]
	vldr	d2, [src, #64 + 24]
	vstr	d7, [dst, #64 + 32]
	add	src, src, #96
	vstr	d0, [dst, #64 + 40]
	vstr	d1, [dst, #64 + 48]
	vstr	d2, [dst, #64 + 56]
	add	dst, dst, #128
	add	tmp2, tmp2, #prefetch_lines * 64
	b	L(cpy_body_medium)
#else
	/* Long copy.  Use an SMS style loop to maximize the I/O
	   bandwidth of the core.  We don't have enough spare registers
	   to synthesise prefetching, so use PLD operations.  */
	/* Pre-bias src and dst.  */
	sub	src, src, #8
	sub	dst, dst, #8
	pld	[src, #8]
	pld	[src, #72]
	subs	tmp2, tmp2, #64
	pld	[src, #136]
	ldrd	A_l, A_h, [src, #8]
	strd	B_l, B_h, [sp, #8]
	ldrd	B_l, B_h, [src, #16]
	strd	C_l, C_h, [sp, #16]
	ldrd	C_l, C_h, [src, #24]
	strd	D_l, D_h, [sp, #24]
	pld	[src, #200]
	ldrd	D_l, D_h, [src, #32]!
	b	1f
	.p2align	6
2:
	pld	[src, #232]
	strd	A_l, A_h, [dst, #40]
	ldrd	A_l, A_h, [src, #40]
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [src, #48]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [src, #56]
	strd	D_l, D_h, [dst, #64]!
	ldrd	D_l, D_h, [src, #64]!
	subs	tmp2, tmp2, #64
1:
	strd	A_l, A_h, [dst, #8]
	ldrd	A_l, A_h, [src, #8]
	strd	B_l, B_h, [dst, #16]
	ldrd	B_l, B_h, [src, #16]
	strd	C_l, C_h, [dst, #24]
	ldrd	C_l, C_h, [src, #24]
	strd	D_l, D_h, [dst, #32]
	ldrd	D_l, D_h, [src, #32]
	bcs	2b
	/* Save the remaining bytes and restore the callee-saved regs.  */
	strd	A_l, A_h, [dst, #40]
	add	src, src, #40
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [sp, #8]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [sp, #16]
	strd	D_l, D_h, [dst, #64]
	ldrd	D_l, D_h, [sp, #24]
	add	dst, dst, #72
	tst	tmp2, #0x3f
	bne	L(tail63aligned)
	ldr	tmp2, [sp], #FRAME_SIZE
	bx	lr
#endif

L(cpy_notaligned):
	pld	[src]
	pld	[src, #64]
	/* There's at least 64 bytes to copy, but there is no mutual
	   alignment.  */
	/* Bring DST to 64-bit alignment.  */
	lsls	tmp2, dst, #29
	pld	[src, #(2 * 64)]
	beq	1f
	rsbs	tmp2, tmp2, #0
	sub	count, count, tmp2, lsr #29
	ldrmi	tmp1, [src], #4
	strmi	tmp1, [dst], #4
	lsls	tmp2, tmp2, #2
	ldrbne	tmp1, [src], #1
	ldrhcs	tmp2, [src], #2
	strbne	tmp1, [dst], #1
	strhcs	tmp2, [dst], #2
1:
	pld	[src, #(3 * 64)]
	subs	count, count, #64
	ldrlo	tmp2, [sp], #FRAME_SIZE
	blo	L(tail63unaligned)
	pld	[src, #(4 * 64)]

#ifdef USE_NEON
	vld1.8	{d0-d3}, [src]!
	vld1.8	{d4-d7}, [src]!
	subs	count, count, #64
	blo	2f
1:
	pld	[src, #(4 * 64)]
	vst1.8	{d0-d3}, [ALIGN (dst, 64)]!
	vld1.8	{d0-d3}, [src]!
	vst1.8	{d4-d7}, [ALIGN (dst, 64)]!
	vld1.8	{d4-d7}, [src]!
	subs	count, count, #64
	bhs	1b
2:
	vst1.8	{d0-d3}, [ALIGN (dst, 64)]!
	vst1.8	{d4-d7}, [ALIGN (dst, 64)]!
	ands	count, count, #0x3f
#else
	/* Use an SMS style loop to maximize the I/O bandwidth.  */
	sub	src, src, #4
	sub	dst, dst, #8
	subs	tmp2, count, #64	/* Use tmp2 for count.  */
	ldr	A_l, [src, #4]
	ldr	A_h, [src, #8]
	strd	B_l, B_h, [sp, #8]
	ldr	B_l, [src, #12]
	ldr	B_h, [src, #16]
	strd	C_l, C_h, [sp, #16]
	ldr	C_l, [src, #20]
	ldr	C_h, [src, #24]
	strd	D_l, D_h, [sp, #24]
	ldr	D_l, [src, #28]
	ldr	D_h, [src, #32]!
	b	1f
	.p2align	6
2:
	pld	[src, #(5 * 64) - (32 - 4)]
	strd	A_l, A_h, [dst, #40]
	ldr	A_l, [src, #36]
	ldr	A_h, [src, #40]
	strd	B_l, B_h, [dst, #48]
	ldr	B_l, [src, #44]
	ldr	B_h, [src, #48]
	strd	C_l, C_h, [dst, #56]
	ldr	C_l, [src, #52]
	ldr	C_h, [src, #56]
	strd	D_l, D_h, [dst, #64]!
	ldr	D_l, [src, #60]
	ldr	D_h, [src, #64]!
	subs	tmp2, tmp2, #64
1:
	strd	A_l, A_h, [dst, #8]
	ldr	A_l, [src, #4]
	ldr	A_h, [src, #8]
	strd	B_l, B_h, [dst, #16]
	ldr	B_l, [src, #12]
	ldr	B_h, [src, #16]
	strd	C_l, C_h, [dst, #24]
	ldr	C_l, [src, #20]
	ldr	C_h, [src, #24]
	strd	D_l, D_h, [dst, #32]
	ldr	D_l, [src, #28]
	ldr	D_h, [src, #32]
	bcs	2b

	/* Save the remaining bytes and restore the callee-saved regs.  */
	strd	A_l, A_h, [dst, #40]
	add	src, src, #36
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [sp, #8]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [sp, #16]
	strd	D_l, D_h, [dst, #64]
	ldrd	D_l, D_h, [sp, #24]
	add	dst, dst, #72
	ands	count, tmp2, #0x3f
#endif
	ldr	tmp2, [sp], #FRAME_SIZE
	bne	L(tail63unaligned)
	bx	lr

END (memcpy)

@ ---------------------------------------------------------------------------
@ Taken from https://github.com/ARM-software/optimized-routines/blob/master/string/arm/strcmp.S

#define STRCMP_NO_PRECHECK	0

	/* This version uses Thumb-2 code.  */
	.thumb
	.syntax unified

#ifdef __ARM_BIG_ENDIAN
#define S2LO lsl
#define S2LOEQ lsleq
#define S2HI lsr
#define MSB 0x000000ff
#define LSB 0xff000000
#define BYTE0_OFFSET 24
#define BYTE1_OFFSET 16
#define BYTE2_OFFSET 8
#define BYTE3_OFFSET 0
#else /* not  __ARM_BIG_ENDIAN */
#define S2LO lsr
#define S2LOEQ lsreq
#define S2HI lsl
#define BYTE0_OFFSET 0
#define BYTE1_OFFSET 8
#define BYTE2_OFFSET 16
#define BYTE3_OFFSET 24
#define MSB 0xff000000
#define LSB 0x000000ff
#endif /* not  __ARM_BIG_ENDIAN */

/* Parameters and result.  */
#define src1		r0
#define src2		r1
#define result		r0	/* Overlaps src1.  */

#undef tmp1
#undef tmp2
/* Internal variables.  */
#define tmp1		r4
#define tmp2		r5
#define const_m1	r12

/* Additional internal variables for 64-bit aligned data.  */
#define data1a		r2
#define data1b		r3
#define data2a		r6
#define data2b		r7
#define syndrome_a	tmp1
#define syndrome_b	tmp2

/* Additional internal variables for 32-bit aligned data.  */
#define data1		r2
#define data2		r3
#define syndrome	tmp2


	/* Macro to compute and return the result value for word-aligned
	   cases.  */
	.macro strcmp_epilogue_aligned synd d1 d2 restore_r6
#ifdef __ARM_BIG_ENDIAN
	/* If data1 contains a zero byte, then syndrome will contain a 1 in
	   bit 7 of that byte.  Otherwise, the highest set bit in the
	   syndrome will highlight the first different bit.  It is therefore
	   sufficient to extract the eight bits starting with the syndrome
	   bit.  */
	clz	tmp1, \synd
	lsl	r1, \d2, tmp1
	.if \restore_r6
	ldrd	r6, r7, [sp, #8]
	.endif
	.cfi_restore 6
	.cfi_restore 7
	lsl	\d1, \d1, tmp1
	.cfi_remember_state
	lsr	result, \d1, #24
	ldrd	r4, r5, [sp], #16
	.cfi_restore 4
	.cfi_restore 5
	sub	result, result, r1, lsr #24
	bx	lr
#else
	/* To use the big-endian trick we'd have to reverse all three words.
	   that's slower than this approach.  */
	rev	\synd, \synd
	clz	tmp1, \synd
	bic	tmp1, tmp1, #7
	lsr	r1, \d2, tmp1
	.cfi_remember_state
	.if \restore_r6
	ldrd	r6, r7, [sp, #8]
	.endif
	.cfi_restore 6
	.cfi_restore 7
	lsr	\d1, \d1, tmp1
	and	result, \d1, #255
	and	r1, r1, #255
	ldrd	r4, r5, [sp], #16
	.cfi_restore 4
	.cfi_restore 5
	sub	result, result, r1

	bx	lr
#endif
	.endm

	.p2align	5
L(strcmp_start_addr):
#if STRCMP_NO_PRECHECK == 0
L(fastpath_exit):
	sub	r0, r2, r3
	bx	lr
	nop
#endif
ENTRY_ALIGN (strcmp, 0)
#if STRCMP_NO_PRECHECK == 0
	ldrb	r2, [src1]
	ldrb	r3, [src2]
	cmp	r2, #1
	it	cs
	cmpcs	r2, r3
	bne	L(fastpath_exit)
#endif
	strd	r4, r5, [sp, #-16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 4, -16
	.cfi_offset 5, -12
	orr	tmp1, src1, src2
	strd	r6, r7, [sp, #8]
	.cfi_offset 6, -8
	.cfi_offset 7, -4
	mvn	const_m1, #0
	lsl	r2, tmp1, #29
	cbz	r2, L(loop_aligned8)

L(not_aligned):
	eor	tmp1, src1, src2
	tst	tmp1, #7
	bne	L(misaligned8)

	/* Deal with mutual misalignment by aligning downwards and then
	   masking off the unwanted loaded data to prevent a difference.  */
	and	tmp1, src1, #7
	bic	src1, src1, #7
	and	tmp2, tmp1, #3
	bic	src2, src2, #7
	lsl	tmp2, tmp2, #3	/* Bytes -> bits.  */
	ldrd	data1a, data1b, [src1], #16
	tst	tmp1, #4
	ldrd	data2a, data2b, [src2], #16
	/* In thumb code we can't use MVN with a register shift, but
	   we do have ORN.  */
	S2HI	tmp1, const_m1, tmp2
	orn	data1a, data1a, tmp1
	orn	data2a, data2a, tmp1
	beq	L(start_realigned8)
	orn	data1b, data1b, tmp1
	mov	data1a, const_m1
	orn	data2b, data2b, tmp1
	mov	data2a, const_m1
	b	L(start_realigned8)

	/* Unwind the inner loop by a factor of 2, giving 16 bytes per
	   pass.  */
	.p2align 5,,12  /* Don't start in the tail bytes of a cache line.  */
	.p2align 2	/* Always word aligned.  */
L(loop_aligned8):
	ldrd	data1a, data1b, [src1], #16
	ldrd	data2a, data2b, [src2], #16
L(start_realigned8):
	uadd8	syndrome_b, data1a, const_m1	/* Only want GE bits,  */
	eor	syndrome_a, data1a, data2a
	sel	syndrome_a, syndrome_a, const_m1
	cbnz	syndrome_a, L(diff_in_a)
	uadd8	syndrome_b, data1b, const_m1	/* Only want GE bits.  */
	eor	syndrome_b, data1b, data2b
	sel	syndrome_b, syndrome_b, const_m1
	cbnz	syndrome_b, L(diff_in_b)

	ldrd	data1a, data1b, [src1, #-8]
	ldrd	data2a, data2b, [src2, #-8]
	uadd8	syndrome_b, data1a, const_m1	/* Only want GE bits,  */
	eor	syndrome_a, data1a, data2a
	sel	syndrome_a, syndrome_a, const_m1
	uadd8	syndrome_b, data1b, const_m1	/* Only want GE bits.  */
	eor	syndrome_b, data1b, data2b
	sel	syndrome_b, syndrome_b, const_m1
	/* Can't use CBZ for backwards branch.  */
	orrs	syndrome_b, syndrome_b, syndrome_a /* Only need if s_a == 0 */
	beq	L(loop_aligned8)

L(diff_found):
	cbnz	syndrome_a, L(diff_in_a)

L(diff_in_b):
	strcmp_epilogue_aligned syndrome_b, data1b, data2b 1

L(diff_in_a):
	.cfi_restore_state
	strcmp_epilogue_aligned syndrome_a, data1a, data2a 1

	.cfi_restore_state
L(misaligned8):
	tst	tmp1, #3
	bne	L(misaligned4)
	ands	tmp1, src1, #3
	bne	L(mutual_align4)

	/* Unrolled by a factor of 2, to reduce the number of post-increment
	   operations.  */
L(loop_aligned4):
	ldr	data1, [src1], #8
	ldr	data2, [src2], #8
L(start_realigned4):
	uadd8	syndrome, data1, const_m1	/* Only need GE bits.  */
	eor	syndrome, data1, data2
	sel	syndrome, syndrome, const_m1
	cbnz	syndrome, L(aligned4_done)
	ldr	data1, [src1, #-4]
	ldr	data2, [src2, #-4]
	uadd8	syndrome, data1, const_m1
	eor	syndrome, data1, data2
	sel	syndrome, syndrome, const_m1
	cmp	syndrome, #0
	beq	L(loop_aligned4)

L(aligned4_done):
	strcmp_epilogue_aligned syndrome, data1, data2, 0

L(mutual_align4):
	.cfi_restore_state
	/* Deal with mutual misalignment by aligning downwards and then
	   masking off the unwanted loaded data to prevent a difference.  */
	lsl	tmp1, tmp1, #3	/* Bytes -> bits.  */
	bic	src1, src1, #3
	ldr	data1, [src1], #8
	bic	src2, src2, #3
	ldr	data2, [src2], #8

	/* In thumb code we can't use MVN with a register shift, but
	   we do have ORN.  */
	S2HI	tmp1, const_m1, tmp1
	orn	data1, data1, tmp1
	orn	data2, data2, tmp1
	b	L(start_realigned4)

L(misaligned4):
	ands	tmp1, src1, #3
	beq	L(src1_aligned)
	sub	src2, src2, tmp1
	bic	src1, src1, #3
	lsls	tmp1, tmp1, #31
	ldr	data1, [src1], #4
	beq	L(aligned_m2)
	bcs	L(aligned_m1)

#if STRCMP_NO_PRECHECK == 1
	ldrb	data2, [src2, #1]
	uxtb	tmp1, data1, ror #BYTE1_OFFSET
	subs	tmp1, tmp1, data2
	bne	L(misaligned_exit)
	cbz	data2, L(misaligned_exit)

L(aligned_m2):
	ldrb	data2, [src2, #2]
	uxtb	tmp1, data1, ror #BYTE2_OFFSET
	subs	tmp1, tmp1, data2
	bne	L(misaligned_exit)
	cbz	data2, L(misaligned_exit)

L(aligned_m1):
	ldrb	data2, [src2, #3]
	uxtb	tmp1, data1, ror #BYTE3_OFFSET
	subs	tmp1, tmp1, data2
	bne	L(misaligned_exit)
	add	src2, src2, #4
	cbnz	data2, L(src1_aligned)
#else  /* STRCMP_NO_PRECHECK */
	/* If we've done the pre-check, then we don't need to check the
	   first byte again here.  */
	ldrb	data2, [src2, #2]
	uxtb	tmp1, data1, ror #BYTE2_OFFSET
	subs	tmp1, tmp1, data2
	bne	L(misaligned_exit)
	cbz	data2, L(misaligned_exit)

L(aligned_m2):
	ldrb	data2, [src2, #3]
	uxtb	tmp1, data1, ror #BYTE3_OFFSET
	subs	tmp1, tmp1, data2
	bne	L(misaligned_exit)
	cbnz	data2, L(aligned_m1)
#endif

L(misaligned_exit):
	.cfi_remember_state
	mov	result, tmp1
	ldr	r4, [sp], #16
	.cfi_restore 4
	bx	lr

#if STRCMP_NO_PRECHECK == 0
L(aligned_m1):
	add	src2, src2, #4
#endif
L(src1_aligned):
	.cfi_restore_state
	/* src1 is word aligned, but src2 has no common alignment
	   with it.  */
	ldr	data1, [src1], #4
	lsls	tmp1, src2, #31		/* C=src2[1], Z=src2[0].  */

	bic	src2, src2, #3
	ldr	data2, [src2], #4
	bhi	L(overlap1)		/* C=1, Z=0 => src2[1:0] = 0b11.  */
	bcs	L(overlap2)		/* C=1, Z=1 => src2[1:0] = 0b10.  */

	/* (overlap3) C=0, Z=0 => src2[1:0] = 0b01.  */
L(overlap3):
	bic	tmp1, data1, #MSB
	uadd8	syndrome, data1, const_m1
	eors	syndrome, tmp1, data2, S2LO #8
	sel	syndrome, syndrome, const_m1
	bne	4f
	cbnz	syndrome, 5f
	ldr	data2, [src2], #4
	eor	tmp1, tmp1, data1
	cmp	tmp1, data2, S2HI #24
	bne	6f
	ldr	data1, [src1], #4
	b	L(overlap3)
4:
	S2LO	data2, data2, #8
	b	L(strcmp_tail)

5:
	bics	syndrome, syndrome, #MSB
	bne	L(strcmp_done_equal)

	/* We can only get here if the MSB of data1 contains 0, so
	   fast-path the exit.  */
	ldrb	result, [src2]
	.cfi_remember_state
	ldrd	r4, r5, [sp], #16
	.cfi_restore 4
	.cfi_restore 5
	/* R6/7 Not used in this sequence.  */
	.cfi_restore 6
	.cfi_restore 7
	neg	result, result
	bx	lr

6:
	.cfi_restore_state
	S2LO	data1, data1, #24
	and	data2, data2, #LSB
	b	L(strcmp_tail)

	.p2align 5,,12	/* Ensure at least 3 instructions in cache line.  */
L(overlap2):
	and	tmp1, data1, const_m1, S2LO #16
	uadd8	syndrome, data1, const_m1
	eors	syndrome, tmp1, data2, S2LO #16
	sel	syndrome, syndrome, const_m1
	bne	4f
	cbnz	syndrome, 5f
	ldr	data2, [src2], #4
	eor	tmp1, tmp1, data1
	cmp	tmp1, data2, S2HI #16
	bne	6f
	ldr	data1, [src1], #4
	b	L(overlap2)
4:
	S2LO	data2, data2, #16
	b	L(strcmp_tail)
5:
	ands	syndrome, syndrome, const_m1, S2LO #16
	bne	L(strcmp_done_equal)

	ldrh	data2, [src2]
	S2LO	data1, data1, #16
#ifdef __ARM_BIG_ENDIAN
	lsl	data2, data2, #16
#endif
	b	L(strcmp_tail)

6:
	S2LO	data1, data1, #16
	and	data2, data2, const_m1, S2LO #16
	b	L(strcmp_tail)

	.p2align 5,,12	/* Ensure at least 3 instructions in cache line.  */
L(overlap1):
	and	tmp1, data1, #LSB
	uadd8	syndrome, data1, const_m1
	eors	syndrome, tmp1, data2, S2LO #24
	sel	syndrome, syndrome, const_m1
	bne	4f
	cbnz	syndrome, 5f
	ldr	data2, [src2], #4
	eor	tmp1, tmp1, data1
	cmp	tmp1, data2, S2HI #8
	bne	6f
	ldr	data1, [src1], #4
	b	L(overlap1)
4:
	S2LO	data2, data2, #24
	b	L(strcmp_tail)
5:
	tst	syndrome, #LSB
	bne	L(strcmp_done_equal)
	ldr	data2, [src2]
6:
	S2LO	data1, data1, #8
	bic	data2, data2, #MSB
	b	L(strcmp_tail)

L(strcmp_done_equal):
	mov	result, #0
	.cfi_remember_state
	ldrd	r4, r5, [sp], #16
	.cfi_restore 4
	.cfi_restore 5
	/* R6/7 not used in this sequence.  */
	.cfi_restore 6
	.cfi_restore 7
	bx	lr

L(strcmp_tail):
	.cfi_restore_state
#ifndef __ARM_BIG_ENDIAN
	rev	data1, data1
	rev	data2, data2
	/* Now everything looks big-endian...  */
#endif
	uadd8	tmp1, data1, const_m1
	eor	tmp1, data1, data2
	sel	syndrome, tmp1, const_m1
	clz	tmp1, syndrome
	lsl	data1, data1, tmp1
	lsl	data2, data2, tmp1
	lsr	result, data1, #24
	ldrd	r4, r5, [sp], #16
	.cfi_restore 4
	.cfi_restore 5
	/* R6/7 not used in this sequence.  */
	.cfi_restore 6
	.cfi_restore 7
	sub	result, result, data2, lsr #24
	bx	lr

END (strcmp)
