/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "effects/reverb.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class HPF final : public Integer {
public:
	using Integer::Integer;
	// project.reverb is authoritative; the render takes up the song's knobs, published here, at its next block.
	void readCurrentValue() override { this->setValue(std::round(currentSong->project.reverb.hpf * kMaxMenuValue)); }
	void writeCurrentValue() override {
		float value = (float)this->getValue() / kMaxMenuValue;
		currentSong->project.reverb.hpf = value;
		AudioEngine::publishReverbSettings(currentSong->project.reverb);
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return RenderingStyle::HPF; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		auto model = AudioEngine::reverb.model();
		return (model == deluge::effects::ReverbModel::MUTABLE) || (model == deluge::effects::ReverbModel::DIGITAL);
	}
};
} // namespace deluge::gui::menu_item::reverb
