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
class Segment : public source::PatchedParam {
public:
	using source::PatchedParam::PatchedParam;

	void getColumnLabel(StringBuf& label) override {
		const auto& l10nString = getStringForEnvelopeParam(menu_item::PatchedParam::getP());
		label.append(deluge::l10n::get(l10nString));
	}

private:
	const deluge::l10n::String getStringForEnvelopeParam(uint8_t param) const {
		using namespace deluge::modulation;
		switch (param) {
		case deluge::modulation::params::LOCAL_ENV_0_ATTACK:
			return deluge::l10n::String::STRING_FOR_ATTACK_SHORT;
		case deluge::modulation::params::LOCAL_ENV_0_DECAY:
			return deluge::l10n::String::STRING_FOR_DECAY_SHORT;
		case deluge::modulation::params::LOCAL_ENV_0_SUSTAIN:
			return deluge::l10n::String::STRING_FOR_SUSTAIN_SHORT;
		case deluge::modulation::params::LOCAL_ENV_0_RELEASE:
			return deluge::l10n::String::STRING_FOR_RELEASE_SHORT;
		}
	}
};
} // namespace deluge::gui::menu_item::envelope
