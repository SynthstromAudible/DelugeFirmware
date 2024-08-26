/*
 * Copyright (c) 2023 Sean Ditny
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
#include "gui/menu_item/selection.h"
#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"
#include "hid/led/indicator_leds.h"
#include "model/model_stack.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::performance_session_view {
class EditingMode final : public Selection {
public:
	using Selection::Selection;

	PerformanceEditingMode currentMode;

	void readCurrentValue() override {
		if (!performanceView.defaultEditingMode) {
			currentMode = PerformanceEditingMode::DISABLED;
		}
		else if (!performanceView.editingParam) {
			currentMode = PerformanceEditingMode::VALUE;
		}
		else {
			currentMode = PerformanceEditingMode::PARAM;
		}
		this->setValue(currentMode);
	}

	void writeCurrentValue() override { currentMode = this->getValue<PerformanceEditingMode>(); }

	MenuItem* selectButtonPress() override {
		if (currentMode == PerformanceEditingMode::DISABLED) {
			return nullptr; // go up a level
		}
		// here we're going to want to step into the value editing or param editing UI
		else if (currentMode == PerformanceEditingMode::VALUE) {
			performanceView.defaultEditingMode = true;
			performanceView.editingParam = false;
		}
		else { // PerformanceEditingMode::PARAM
			performanceView.defaultEditingMode = true;
			performanceView.editingParam = true;
		}

		if (!performanceView.editingParam) {
			// reset performance view when you switch modes
			// but not when in param editing mode cause that would reset param assignments to FX columns
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			performanceView.resetPerformanceView(modelStack);
		}

		display->setNextTransitionDirection(1);
		openUI(&performanceView);
		return NO_NAVIGATION;
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_DISABLED),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_VALUE),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_PARAM),
		};
	}
};

} // namespace deluge::gui::menu_item::performance_session_view
