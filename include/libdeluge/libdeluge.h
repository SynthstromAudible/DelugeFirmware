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

#include "abi.h"
#include "alloc.h"
#include "app.h"
#include "audio_format.h"
#include "audio_io.h"
#include "audio_load.h"
#include "block_device.h"
#include "board.h"
#include "boot.h"
#include "card.h"
#include "channel.h"
#include "clock.h"
#include "control_surface.h"
#include "cv_gate.h"
#include "display.h"
#include "encoder_io.h"
#include "fault.h"
#include "file_bytes.h"
#include "file_directory.h"
#include "file_io.h"
#include "file_prefix.h"
#include "file_write.h"
#include "flash.h"
#include "memory.h"
#include "midi_io.h"
#include "sample_reader.h"
#include "sample_source.h"
#include "sample_stream.h"
#include "scheduler.h"
#include "signals.h"
#include "storage_busy.h"
#include "stream_io.h"
#include "streaming_fill.h"
#include "system.h"
#include "tier.h"
#include "types.h"

/* Including the umbrella is always correct. Including one header is a
 * build-time economy, not a boundary. */

#endif // LIBDELUGE_H
