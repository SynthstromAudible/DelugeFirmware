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
#include "gui/menu_item/selection.h"
#include "gui/views/performance_session_view.h"
#include "hid/led/indicator_leds.h"
#include "model/model_stack.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::performance_session_view {
class EditingMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		PerformanceEditingMode currentMode;
		if (!performanceSessionView.defaultEditingMode) {
			currentMode = PerformanceEditingMode::DISABLED;
		}
		else if (!performanceSessionView.editingParam) {
			currentMode = PerformanceEditingMode::VALUE;
		}
		else {
			currentMode = PerformanceEditingMode::PARAM;
		}
		this->setValue(currentMode);
	}
	void writeCurrentValue() override {
		PerformanceEditingMode currentMode = this->getValue<PerformanceEditingMode>();
		if (currentMode == PerformanceEditingMode::DISABLED) {
			performanceSessionView.defaultEditingMode = false;
			performanceSessionView.editingParam = false;
		}
		else if (currentMode == PerformanceEditingMode::VALUE) {
			performanceSessionView.defaultEditingMode = true;
			performanceSessionView.editingParam = false;
		}
		else { // PerformanceEditingMode::PARAM
			performanceSessionView.defaultEditingMode = true;
			performanceSessionView.editingParam = true;
		}

		if (performanceSessionView.defaultEditingMode) {
			indicator_leds::blinkLed(IndicatorLED::KEYBOARD);
		}
		else {
			indicator_leds::setLedState(IndicatorLED::KEYBOARD, true);
		}

		if (performanceSessionView.defaultEditingMode && !performanceSessionView.editingParam) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			performanceSessionView.resetPerformanceView(modelStack);
		}
		uiNeedsRendering(&performanceSessionView, 0xFFFFFFFF, 0); // refresh main pads only);
	}
	deluge::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_DISABLED),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_VALUE),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_PARAM),
		};
	}
};

} // namespace deluge::gui::menu_item::performance_session_view
