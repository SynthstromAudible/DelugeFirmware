/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// BSP-internal view of the RZ/A1L render driver (audio_io.cpp), for other
/// drivers on this board that need SSI ring positions (USB audio). Not part of
/// the <libdeluge/...> boundary.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// The SSI TX ring frame index (0 .. SSI_TX_BUFFER_NUM_SAMPLES-1) that frame
/// `offset_frames` of the current deluge_app_render() block will be written to.
uint32_t bsp_audio_tx_frame_at_block_offset(uint32_t offset_frames);

#ifdef __cplusplus
}
#endif
