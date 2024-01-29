/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
// Chainload routine
//
// Accepts 5 arguments in registers:
//  - r0, the code start address
//  - r1, the code size
//  - r2, the code execution start address
//  - r3, the buffer containing the new firmware
//  - r4, the location we should copy the second-stage chainloader in memory
//
// Because the ARMv7 ABI is annoying and I prototyped this with more C code
// involved, `deluge_chainload` can't be called as a regular C function and
// must instead be called via inline assembly. See `chainload.cpp` for how this
// is done.
//
// This is a 2-stage chainloader, which first copies its second stage to RAM
// and then copies the new firmware image over the old. The second stage code
// is implicitly position-independent because all jumps (except `bx/blx` to
// registers) in armv7a are relative.
//
// All of this work needs to be done with the MMU and caches disabled because
// the RZ/A1L cache is extremely broken, and doesn't respect most of the cache
// maintenance operations. This includes CP15 MCRs, the usual DMB/IMB
// instructions, and the ARM-recommended L1$ flush logic.
//
// It's possible all of this actually works and it's the L2$ misbehaving -- We
// haven't tried flushing that cache instead yet. But disabling the MMU is
// easy, doesn't cost meaningful performance here, and seems to work (mostly)
// reliably.
  .align 4
  .text
  .arm
  .func
  .global deluge_chainload
deluge_chainload:
  // Switch to system mode so we can poke MCRs without fear of faults, and so
  // the new firmware starts in system mode as it expects.
  cps       #0x1f

  // Disable MMU and caches
  mrc       p15, 0, r5, c1, c0, 0       /* Read CP15 System Control register */
  bic       r5, r5, #(0x1 << 12)        /* Clear I bit 12 to disable I Cache */
  bic       r5, r5, #(0x1 <<  2)        /* Clear C bit  2 to disable D Cache */
  bic       r5, r5, #0x2   /* Clear A bit 1 to disable alignment fault check */
  bic       r5, r5, #0x1 /* Set M bit 0 to enable MMU before scatter loading */
  mcr       p15, 0, r5, c1, c0, 0      /* Write CP15 System Control register */

  // Save arguments
  sub       sp, #0x20
  str       r0, [sp, #0x00]
  str       r1, [sp, #0x04]
  str       r2, [sp, #0x08]
  str       r3, [sp, #0x0c]
  str       r4, [sp, #0x10]

  // Copy the second stage to $r4
  mov       r5, r4
  ldr       r6, =chainload_code_start
  ldr       r7, =chainload_code_end
  mov       r8,r7
chainloader_copy_start:
  cmp       r6, r8
  bgt       chainloader_copy_done
  ldr       r7, [r6] @;; # =0xe320f000
  str       r7, [r5]
  // From DDI0406C (ARMv7AR ARM), section A3.5.4 -- everything required to flush instructions
  mcr       p15, 0, r5, c7, c11, 1      @;; DCCMVAU, Flush D$ to point of unification by MVA
  dmb                                   @;; Make that write visible
  mcr       p15, 0, r5, c7, c5 , 1      @;; ICIMVAU, Flush I$ to point of unification by MVA
  mcr       p15, 0, r5, c7, c5 , 7      @;; BPIMVA , Invalidate branch predictor by MVA
  add       r5, r5, #4
  add       r6, r6, #4
  b         chainloader_copy_start
chainloader_copy_done:
  // Reload arguments and jump to the second-stage chainloader
  ldr       r0, [sp, #0x00]
  ldr       r1, [sp, #0x04]
  ldr       r2, [sp, #0x08]
  ldr       r3, [sp, #0x0c]
  ldr       r4, [sp, #0x10]
  bx        r4

  // Ensure instruction alignment
  .align 2
  // Start of the second-stage chainloder
  // Expects r0, r1, r2, and r3 to be set as described in the arugments
chainload_code_start:
  // Copy the new image to $r0
  // r4 is scratch storage (the data being copied)
  // r5 is the copy end address
  mov       r5, r0
  add       r5, r5, r1
buffer_copy:
  cmp       r0, r5
  bgt       buffer_copy_done
  ldr       r4,[r3]
  str       r4,[r0]
  // From DDI0406C (ARMv7AR ARM), section A3.5.4 -- everything required to flush instructions
  mcr       p15, 0, r0, c7, c11, 1     @;; DCCMVAU, Flush D$ to point of unification by MVA
  dmb                                  @;; Make that write visible
  mcr       p15, 0, r0, c7, c5 , 1     @;; ICIMVAU, Flush I$ to point of unification by MVA
  mcr       p15, 0, r0, c7, c5 , 7     @;; BPIMVA , Invalidate branch predictor by MVA
  add       r3, r3, #4
  add       r0, r0, #4
  b         buffer_copy
buffer_copy_done:
  // flush L1I$
  mov       r0, #0
  mcr       p15, 0, r0, c7, c5, 0      @;; ICIALLU
  // Issue memory barriers
  dsb
  isb
  // Jump to the new firmware image
  bx        r2

  // Align to a full instruction so the first-stage chainloader copies enough bytes
  .align 2
chainload_code_end:

