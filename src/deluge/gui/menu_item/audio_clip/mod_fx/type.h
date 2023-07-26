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
#include "gui/menu_item/mod_fx/type.h"

namespace menu_item::audio_clip::mod_fx {
class Type final : public menu_item::mod_fx::Type {
public:
	using menu_item::mod_fx::Type::Type;

	// We override this to set min value to 1. We don't inherit any getMinValue() function to override more easily
	void selectEncoderAction(int offset) override {
		soundEditor.currentValue += offset;
		int numOptions = getNumOptions();

		if (soundEditor.currentValue >= numOptions) {
			soundEditor.currentValue -= (numOptions - 1);
		}
		else if (soundEditor.currentValue < 1) {
			soundEditor.currentValue += (numOptions - 1);
		}

		Value::selectEncoderAction(offset);
	}
};
} // namespace menu_item::audio_clip::mod_fx
