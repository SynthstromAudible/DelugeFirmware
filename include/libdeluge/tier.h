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

/// libdeluge/tier.h -- which execution tier is running the caller, and the count of synchronous card calls made
/// after boot on the interaction tier. The BSP owns both.

#ifndef LIBDELUGE_TIER_H
#define LIBDELUGE_TIER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Values of @ref deluge_tier_current.
enum {
	DELUGE_TIER_INTERACTION = 0, ///< thread mode: the UI, the sequencer, the app's tasks
	DELUGE_TIER_STORAGE = 1,     ///< the storage tasks
	DELUGE_TIER_AUDIO = 2,       ///< the audio render
};

/// @brief The tier running the caller. A C++-only build answers DELUGE_TIER_INTERACTION.
uint8_t deluge_tier_current(void);

/// @brief Synchronous card calls made on the audio tier after boot, which must be none. Zero in a C++-only build.
uint32_t deluge_tier_sync_audio_total(void);

/// @brief How many entry points the synchronous-call meter has. Zero in a C++-only build.
uint8_t deluge_tier_sync_entries(void);

/// @brief Synchronous card calls made through @p entry after boot on the interaction tier.
uint32_t deluge_tier_sync_count(uint8_t entry);

/// @brief @p entry's short name (`file_open`, `block_read`, ...), or "" out of range. Static storage.
const char* deluge_tier_sync_name(uint8_t entry);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_TIER_H
