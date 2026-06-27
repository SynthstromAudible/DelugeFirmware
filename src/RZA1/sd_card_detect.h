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

#ifdef __cplusplus
extern "C" {
#endif

/// SD card-detect events, latched by the card-detect interrupt and drained by
/// higher layers (the BSP block device, then the app). This is the pull side of
/// the boundary: the ISR *records* an edge here, nothing is pushed upward; the
/// app polls for it. The latch is last-event-wins (card insert/eject is human-
/// speed and the consumer polls several times per second).
enum SdCardDetectEvent {
    SD_CD_NONE     = 0,
    SD_CD_INSERTED = 1,
    SD_CD_EJECTED  = 2,
};

/// Return and clear the latest pending card-detect event for `sd_port`
/// (SD_CD_NONE if none pending). [task]
int sdTakeCardDetectEvent(int sd_port);

#ifdef __cplusplus
}
#endif
