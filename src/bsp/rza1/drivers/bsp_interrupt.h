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
/// BSP-internal interrupt registration.
///
/// Registering + enabling a hardware interrupt is a pure interrupt-controller
/// (GIC) operation. OSLikeStuff happened to expose it as setupAndEnableInterrupt,
/// but it is a board/HAL concern, not task scheduling — so the relocated RZ/A1L
/// drivers call the controller directly through this helper instead of reaching
/// up into the task layer.
#include "RZA1/intc/devdrv_intc.h"

static inline void bspRegisterAndEnableInterrupt(void (*handler)(uint32_t), uint16_t interruptID, uint8_t priority) {
	R_INTC_Disable(interruptID);
	R_INTC_RegistIntFunc(interruptID, handler);
	R_INTC_SetPriority(interruptID, priority);
	R_INTC_Enable(interruptID);
}
