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

namespace deluge::synth {

/// @brief Which slice of the host's live input a live-input @ref Source plays — the synth-native channel vocabulary.
///
/// Latched once per note (from the configured `OscType`, at the App boundary) so the per-block render context need
/// not carry an `OscType`. @ref Stereo is the *configured* intent; when the host has no stereo input the source
/// still folds down to the left channel at render time (a runtime concern, not this enum).
enum class InputChannel : uint8_t {
	LEFT,   ///< the left input channel (also the internal-mic / mono case)
	RIGHT,  ///< the right input channel
	STEREO, ///< both channels (configured); folds to @ref LEFT when no stereo input is present
};

} // namespace deluge::synth
