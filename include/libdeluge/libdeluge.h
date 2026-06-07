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

/// libdeluge.h — umbrella include for the hardware boundary.
///
/// The portable Deluge application includes these headers and nothing from a HAL
/// or a BSP implementation. See include/libdeluge/README.md for the rules and
/// docs/dev/libdeluge_bsp_design.md for the design.
#ifndef LIBDELUGE_H
#define LIBDELUGE_H

#include "audio_io.h"
#include "block_device.h"
#include "board.h"
#include "clock.h"
#include "control_surface.h"
#include "cv_gate.h"
#include "display.h"
#include "memory.h"
#include "midi_io.h"
#include "system.h"
#include "types.h"

/* app.h is the inbound half (implemented by the app, called by the platform);
 * include it explicitly where those entry points are defined. scheduler.h is
 * included where task scheduling is used. */

#endif // LIBDELUGE_H
