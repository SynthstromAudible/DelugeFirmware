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
#include "gui/menu_item/arpeggiator/midi_cv/arp_integer.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "model/song/song.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item::arpeggiator::midi_cv {
class Gate final : public ArpNonSoundInteger {
public:
	using ArpNonSoundInteger::ArpNonSoundInteger;
	void readCurrentValue() override {
		this->setValue(computeCurrentValueForStandardMenuItem(soundEditor.currentArpSettings->gate));
	}
	void writeCurrentValue() override {
		int32_t value = computeFinalValueForStandardMenuItem(this->getValue());
		soundEditor.currentArpSettings->gate = value;
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return LENGTH_SLIDER; }
};
} // namespace deluge::gui::menu_item::arpeggiator::midi_cv
