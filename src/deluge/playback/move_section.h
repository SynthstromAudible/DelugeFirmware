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
#pragma once

#include <cstdint>

namespace deluge::playback {

/// Points in the interaction tier's playhead moves where the render may land between two steps that belong together:
/// a host harness runs a render block there to show the window is closed.
enum class InjectAt : uint8_t {
	StartBeforeClock, ///< playback start, after the clips are positioned and before the clock is published
	AfterMove,        ///< a reset, after the clips are positioned and before linear recording reads them
	LaunchBeforeMove, ///< a late launch, after the start position is read and before the clip is moved there
	kCount,
};

#if defined(DELUGE_HOST)
/// @brief Host only: run @p hook (a render block) the next time code reaches @p at, once; nullptr clears it.
void setRenderInjection(InjectAt at, void (*hook)());
/// @brief Host only: run the hook set for @p at, if any, and clear it.
void injectRenderAt(InjectAt at);
#else
inline void injectRenderAt(InjectAt /*at*/) {
}
#endif

} // namespace deluge::playback
