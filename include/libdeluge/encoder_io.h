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

/// libdeluge/encoder_io.h — quadrature encoder input.
///
/// The board owns the encoder GPIO, the quadrature decoding and its interrupts:
/// it decodes both pins into a signed quadrature-edge count per encoder and wakes
/// the app loop (DELUGE_WAKE_ENCODERS); the application drains the raw edge
/// counts and applies its own detent / non-detent policy (see hid/encoders.h). No encoder pin number or IRQ id crosses
/// the boundary; the board's pin map is its own.
#ifndef LIBDELUGE_ENCODER_IO_H
#define LIBDELUGE_ENCODER_IO_H

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Number of physical encoders on the board. Indices passed to
/// deluge_encoder_take_quadrature_edges() are the application's encoder ordinals
/// (hid::encoders::EncoderName); the board's pin map uses the same order.
#define DELUGE_NUM_ENCODERS 6

/// Atomically read and clear the accumulated signed quadrature-edge count for
/// `encoder` (0..DELUGE_NUM_ENCODERS-1): four edges per cycle, each reported once
/// the other pin has also changed, so one edge late. A cycle is one detent step
/// on the Deluge's detented encoders; detent reduction (and the non-detent
/// gold-knob mode) is the application's concern. Saturates rather than wraps.
/// [task] [isr]
int8_t deluge_encoder_take_quadrature_edges(uint8_t encoder) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_ENCODER_IO_H
