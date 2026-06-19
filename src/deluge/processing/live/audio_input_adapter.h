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

#include "synth/source/audio_input_port.h" // synth::AudioInputPort

/// @brief The firmware adapter behind @ref deluge::synth::AudioInputPort: exposes AudioEngine's live-input ring,
/// frame clock, plug-state, and the shared per-channel @ref LiveInputBuffer pool to the (library) live-input DSP.
/// Stateless — all of it lives in AudioEngine; this is the thin seam that keeps the DSP free of that global.
class AudioInputAdapter final : public deluge::synth::AudioInputPort {
public:
	[[nodiscard]] uint32_t sampleTime() const override;
	[[nodiscard]] bool stereoInputAvailable() const override;
	[[nodiscard]] const int32_t* ringReadPos() const override;
	[[nodiscard]] const int32_t* ringEnd() const override;
	[[nodiscard]] deluge::synth::LiveInputBuffer* bufferForChannel(OscType inputType, bool mayCreate) override;
	void reportHops(int32_t hopsEnded) override;
};
