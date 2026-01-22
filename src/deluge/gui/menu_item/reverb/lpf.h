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
#include "dsp/reverb/reverb.hpp"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class LPF final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(AudioEngine::reverb.getLPF() * kMaxMenuValue); }
	void writeCurrentValue() override { AudioEngine::reverb.setLPF((float)this->getValue() / kMaxMenuValue); }

	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return RenderingStyle::LPF; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto model = AudioEngine::reverb.getModel();
		return (model == dsp::Reverb::Model::MUTABLE) || (model == dsp::Reverb::Model::DIGITAL)
		       || (model == dsp::Reverb::Model::FEATHERVERB);
	}
};
} // namespace deluge::gui::menu_item::reverb
