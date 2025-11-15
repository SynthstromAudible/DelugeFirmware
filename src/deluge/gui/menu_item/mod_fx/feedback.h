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
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "util/comparison.h"

namespace deluge::gui::menu_item::mod_fx {
class Feedback final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
		return (util::one_of(modControllable->getModFXType(),
		                     {ModFXType::FLANGER, ModFXType::PHASER, ModFXType::GRAIN, ModFXType::WARBLE}));
	}
	[[nodiscard]] std::string_view getName() const override {
		return modfx::getParamName(soundEditor.currentModControllable->getModFXType(), ModFXParam::FEEDBACK);
	}
	[[nodiscard]] virtual std::string_view getTitle() const { return getName(); }

	void configureRenderingOptions(const HorizontalMenuRenderingOptions &options) override {
		UnpatchedParam::configureRenderingOptions(options);
		options.label = modfx::getParamName(soundEditor.currentModControllable->getModFXType(), ModFXParam::FEEDBACK, true);
	}
};
} // namespace deluge::gui::menu_item::mod_fx
