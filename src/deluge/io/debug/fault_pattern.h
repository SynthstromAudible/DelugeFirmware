/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#pragma once

#include "foundation/panic.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Draw a fault's call chain on the pads and flush it to the pad driver, then return.
///
/// The body of deluge_cpu_fault_report() and fault_handler_print_freeze_pointers(), which both own what
/// happens afterwards. Layout, by column pair: the faulting instruction (magenta, when it is a code address),
/// the interrupted code's return address less 2 (blue), up to four return addresses from the stack less 2
/// (green and cyan alternating), blank pairs, then the first four hex digits of the commit in the fault
/// type's colour on the sidebar.
/// @param fault_pc   Address of the faulting instruction; 0 for a freeze.
/// @param fault_type Which fault it was, which picks the commit's colour.
/// @param sys_lr     The interrupted code's link register.
/// @param sys_sp     The interrupted code's stack pointer, where the call chain is walked from.
void deluge_fault_pattern_draw(uint32_t fault_pc, DelugeCpuFault fault_type, uint32_t sys_lr, uint32_t sys_sp);

#ifdef __cplusplus
}
#endif
