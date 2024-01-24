/*
 * Copyright © 2023 Synthstrom Audible Limited
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

#ifndef COMPILER_ASM_H_
#define COMPILER_ASM_H_

#include <inttypes.h>

//! \section asm-isr ASM stubs defined in isr.S
extern void __enable_irq(void);
extern void __disable_irq(void);
extern void __enable_fiq(void);
extern void __disable_fiq(void);

//! \section asm-invalidate ASM stubs defined in invalidate.S
extern void v7_dma_inv_range(uintptr_t start, uintptr_t end);
extern void v7_dma_flush_range(uintptr_t start, uintptr_t end);

#endif /* COMPILER_ASM_H_ */
