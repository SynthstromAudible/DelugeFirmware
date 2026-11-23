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

#include "dsp/core/fixed_point.h"
#include "synth/modulation/patch_routing.h" // Globality
#include "synth/modulation/patch_source.h"
#include <array>
#include <concepts>
#include <cstdint>

namespace deluge::synth {

/// @brief The reads a sound's render makes of its params, as one block sees them: what is wired, what an unpatched
/// param holds, and what a patched param's automation can reach. The render's smoothed patched values are not here:
/// they are the sound's own smoothing state, not the params'.
template <class V>
concept param_view = requires(const V& view, int32_t param, PatchSource source, Globality scope) {
	/// An unpatched param's current value.
	{ view.unpatched(param) } -> std::same_as<int32_t>;
	/// An unpatched param's current value as a knob position.
	{ view.knob(param) } -> std::same_as<dsp::Q<31>>;
	/// Whether a cable reaches a patched param.
	{ view.modulated(param) } -> std::same_as<bool>;
	/// Whether a source is wired to anything, at either scope.
	{ view.wired(source) } -> std::same_as<bool>;
	/// A bit per source wired to anything at @p scope.
	{ view.sources_wired(scope) } -> std::same_as<uint32_t>;
	/// Whether a patched param's value or automation rises above its minimum anywhere (a source that can sound).
	{ view.can_sound(param) } -> std::same_as<bool>;
	/// The main and per-finger pitch-bend ranges, in semitones.
	{ view.bend_ranges() } -> std::same_as<std::array<uint8_t, 2>>;
};

} // namespace deluge::synth
