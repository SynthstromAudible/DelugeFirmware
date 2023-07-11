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
#include "gui/menu_item/decimal.h"
#include "gui/ui/sound_editor.h"
#include "gui/menu_item/transpose.h"

namespace deluge::gui::menu_item::source {
class Transpose : public menu_item::Transpose {
public:
	Transpose(char const* newName = NULL, int newP = 0) : menu_item::Transpose(newName, newP) {}

	ParamDescriptor getLearningThing() final {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(getP());
		return paramDescriptor;
	}

protected:
	uint8_t getP() final { return p + soundEditor.currentSourceIndex; }
};
} // namespace deluge::gui::menu_item::source
