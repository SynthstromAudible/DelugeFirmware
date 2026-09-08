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
#include "scenes/menu/action.h" // set_performance_editing_mode -- the mirror, see selectButtonPress

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

		// The fifth site that keeps the ported menu's mirror of those two bools in step
		// (`scenes/menu/action.h`; the other four are `syncEditingModeMirror`'s callers in
		// `gui/views/performance_view.cpp`). THIS item is the writer that F2 task 5's first pass
		// excluded as "the menu, not behind the menu's back" -- an exclusion that only holds while
		// `RuntimeFeatureSettingType::ShellMenu` is On. With the toggle off this item is the ONLY
		// way into an editing mode from a menu, and without this line the mirror stays where it was
		// while the view moves; flip the toggle on afterwards and `PerformEditorItem::on_focus`
		// seeds its cursor from that stale value (`scenes/menu/nodes/actions.cpp:121`). The toggle
		// is a runtime setting, so "the port is off" is not a state the mirror may rot in.
		//
		// `currentMode` rather than a re-derivation from the two bools: the branch that just ran set
		// them FROM it, so `EditingMode::readCurrentValue` (`:35-46`) maps the pair straight back to
		// this value -- VALUE is `{true, false}` (`:39-41`), PARAM is `{true, true}` (`:42-44`). The
		// spec asserts the derivation rather than trusting the equivalence.
		//
		// Ungated, for the reason `syncEditingModeMirror` gives at length
		// (`gui/views/performance_view.cpp`): it writes one `static` inside `libdeluge_scenes` that
		// nothing reads with the toggle off, reads nothing, and cannot fail. Not reached at all on
		// the DISABLED arm, which returns at `:52` without writing either bool -- and the port's own
		// item leaves the mirror alone on DISABLED too, which is asserted.
		deluge::scenes::menu::set_performance_editing_mode(currentMode);

		if (!performanceView.editingParam) {
			// reset performance view when you switch modes
			// but not when in param editing mode cause that would reset param assignments to FX columns
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStack =
			    currentSong->setupModelStackWithSongAsTimelineCounter(modelStackMemory);
			performanceView.resetPerformanceView(modelStack);
		}

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
