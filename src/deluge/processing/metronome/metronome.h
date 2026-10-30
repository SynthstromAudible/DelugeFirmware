/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/osc/click.h"

#include <cstdint>
#include <span>

/// @brief The metronome: a click on each beat and a higher one on each bar, at the defaults menu's volume scaled by
/// the song's.
class Metronome {
public:
	Metronome() { setVolume(25); }

	/// @brief Starts a click at @p phase_increment per frame; a click under way restarts.
	/// @param phase_increment The square's phase increment per frame: the bar's click is higher than the beat's.
	void trigger(uint32_t phase_increment) { click_.on<&deluge::dsp::fixed::Click::trigger>(phase_increment); }

	/// @brief Whether the next render adds a click.
	[[nodiscard]] bool sounding() { return click_.on<&deluge::dsp::fixed::Click::sounding>(); }

	/// @brief Adds the click to @p buffer while it sounds; does nothing otherwise.
	/// @param buffer The render, added to in place.
	/// @param volume_post_fx The song's post-FX volume, Q31 (ONE_Q31 without a song).
	void render(std::span<deluge::dsp::StereoSample> buffer, int32_t volume_post_fx);

	/// @brief Sets the volume from the defaults menu's value: (e^(v/200) − 1) × 2^27.
	/// @param linear_param The menu's value, 0 to 50.
	void setVolume(int32_t linear_param);

private:
	deluge::dsp::Mono<deluge::dsp::fixed::Click> click_; ///< The square and its gate.
	uint32_t volume_{};                                  ///< (e^(v/200) − 1) × 2^27, from the menu.
};
