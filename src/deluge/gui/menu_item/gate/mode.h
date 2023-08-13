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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::gate {

class Mode final : public Selection<3>, public FormattedTitle {

	static_vector<std::string_view, capacity()> options_ = {
	    l10n::getView(l10n::String::STRING_FOR_V_TRIGGER),
	    l10n::getView(l10n::String::STRING_FOR_S_TRIGGER),
	};

public:
	Mode() : Selection(), FormattedTitle(l10n::String::STRING_FOR_GATE_MODE_TITLE) {}
	void readCurrentValue() override { this->setValue(cvEngine.gateChannels[soundEditor.currentSourceIndex].mode); }
	void writeCurrentValue() override {
		cvEngine.setGateType(soundEditor.currentSourceIndex, this->getValue<GateType>());
	}
	static_vector<std::string_view, capacity()> getOptions() override { return options_; }

	void updateOptions(int32_t value) {
		using enum l10n::String;
		switch (value) {
		case WHICH_GATE_OUTPUT_IS_CLOCK:
			options_[2] = l10n::getView(STRING_FOR_CLOCK);
			break;

		case WHICH_GATE_OUTPUT_IS_RUN:
			options_[2] = l10n::getView(STRING_FOR_RUN_SIGNAL);
			break;

		default:
			// Remove the extra entry if it's present
			if (options_.size() > 2) {
				options_.pop_back();
			}
			break;
		}
	}
};

} // namespace deluge::gui::menu_item::gate
