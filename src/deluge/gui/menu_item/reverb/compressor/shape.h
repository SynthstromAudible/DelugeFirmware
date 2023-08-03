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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::reverb::compressor {

class Shape final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override {
		this->value_ = (((int64_t)AudioEngine::reverbCompressorShape + 2147483648) * 50 + 2147483648) >> 32;
	}
	void writeCurrentValue() override {
		AudioEngine::reverbCompressorShape = (uint32_t)this->value_ * 85899345 - 2147483648;
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }
	bool isRelevant(Sound* sound, int32_t whichThing) override { return (AudioEngine::reverbCompressorVolume >= 0); }
};
} // namespace deluge::gui::menu_item::reverb::compressor
