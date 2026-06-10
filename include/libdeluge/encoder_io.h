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
/// The board owns the encoder GPIO and the per-encoder quadrature-edge
/// interrupts. Each ISR reads the A/B pins, accumulates a signed A-pin edge
/// count, and wakes a scheduler task; the application drains the raw edge counts
/// and applies its own detent / non-detent policy (see hid/encoder.h). No encoder
/// pin number or IRQ id crosses the boundary; the board's pin map is its own.
#ifndef LIBDELUGE_ENCODER_IO_H
#define LIBDELUGE_ENCODER_IO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Number of physical encoders on the board. Indices passed to
/// deluge_encoder_take_edges() are the application's encoder ordinals
/// (hid::encoders::EncoderName); the board's pin map uses the same order.
#define DELUGE_NUM_ENCODERS 6

/// Set up the encoder GPIO and quadrature-edge interrupts. The board accumulates
/// signed edges per encoder in an ISR and unblocks `wake_task` (a scheduler task
/// id) whenever an encoder moves, so the application can drain promptly rather
/// than poll. Call once at init, after the encoder task is registered. [task]
void deluge_encoder_io_init(int8_t wake_task);

/// Atomically read and clear the accumulated signed A-pin edge count for
/// `encoder` (0..DELUGE_NUM_ENCODERS-1). Two edges make one detent step on the
/// Deluge encoders; detent reduction (and the non-detent gold-knob mode) is the
/// application's concern. [task] [isr]
int8_t deluge_encoder_take_edges(uint8_t encoder);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_ENCODER_IO_H
