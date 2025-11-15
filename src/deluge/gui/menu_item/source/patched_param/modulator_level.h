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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::source::patched_param {
class ModulatorLevel final : public PatchedParam, public FormattedTitle {
public:
	ModulatorLevel(l10n::String name, int32_t newP, uint8_t source_id)
	    : PatchedParam(name, newP, source_id), FormattedTitle(name, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }
	[[nodiscard]] std::string_view getName() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Sound* sound = static_cast<Sound*>(modControllable);
		return sound->getSynthMode() == SynthMode::FM;
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		PatchedParam::configureRenderingOptions(options);
		options.label = getName().substr(2).data();
	}
};

} // namespace deluge::gui::menu_item::source::patched_param
