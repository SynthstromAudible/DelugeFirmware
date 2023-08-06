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
#include "gui/menu_item/patched_param/integer_non_fm.h"
#include "gui/ui/sound_editor.h"
#include "modulation/patch/patch_cable_set.h"

namespace deluge::gui::menu_item::filter {

class HPFFreq final : public patched_param::IntegerNonFM {
public:
	using patched_param::IntegerNonFM::IntegerNonFM;
#if !HAVE_OLED
	void drawValue() override {
		if (this->getValue() == 0
		    && !soundEditor.currentParamManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(
		        ::Param::Local::HPF_FREQ)) {
			numericDriver.setText("OFF");
		}
		else {
			patched_param::IntegerNonFM::drawValue();
		}
	}
#endif
};
} // namespace deluge::gui::menu_item::filter
