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
#include "deluge/modulation/params/param.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/param.h"
#include "gui/menu_item/source/patched_param.h"

namespace deluge::gui::menu_item::envelope {
class Segment : public source::PatchedParam, FormattedTitle {
public:
	Segment(l10n::String newName, l10n::String title, int32_t newP, uint8_t source_id)
	    : PatchedParam(newName, title, newP, source_id), FormattedTitle(title, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		PatchedParam::configureRenderingOptions(options);
		options.label = l10n::get(getShortEnvelopeParamName(menu_item::PatchedParam::getP()));
	}

private:
	static l10n::String getShortEnvelopeParamName(uint8_t param) {
		using namespace deluge::modulation;
		switch (param) {
		case params::LOCAL_ENV_0_ATTACK:
			return l10n::String::STRING_FOR_ATTACK_SHORT;
		case params::LOCAL_ENV_0_DECAY:
			return l10n::String::STRING_FOR_DECAY_SHORT;
		case params::LOCAL_ENV_0_SUSTAIN:
			return l10n::String::STRING_FOR_SUSTAIN_SHORT;
		case params::LOCAL_ENV_0_RELEASE:
			return l10n::String::STRING_FOR_RELEASE_SHORT;
		}
		return l10n::String::STRING_FOR_NONE;
	}
};
} // namespace deluge::gui::menu_item::envelope
