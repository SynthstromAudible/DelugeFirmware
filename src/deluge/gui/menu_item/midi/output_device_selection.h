/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include "io/midi/midi_device_manager.h"
#include "model/output.h"
#include "model/song/song.h"
#include "util/d_stringbuf.h"

namespace deluge::gui::menu_item::midi {

/// Menu item for selecting which MIDI output devices to send to
class OutputDeviceSelection final : public Selection {
public:
	using Selection::Selection;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		Output* output = ::getCurrentOutput();
		return (output != nullptr && output->type == OutputType::MIDI_OUT);
	}

	void drawValue() override;

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) override;
	void readCurrentValue() override;
	void writeCurrentValue() override;
	deluge::vector<std::string_view> getOptions(OptType optType) override;
};

} // namespace deluge::gui::menu_item::midi
