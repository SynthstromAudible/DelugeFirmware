/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/context_menu.h"
#include "processing/audio_output.h"

namespace deluge::gui::context_menu {

class AudioInputSelector final : public ContextMenu {
	enum class Value;

public:
	AudioInputSelector() = default;
	bool getGreyoutColsAndRows(uint32_t* cols, uint32_t* rows) override;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }
	ActionResult padAction(int32_t x, int32_t y, int32_t on) override;
	AudioOutput* audioOutput;

	/// Title
	char const* getTitle() override;

	/// Options
	std::span<const char*> getOptions() override;
};

extern AudioInputSelector audioInputSelector;
} // namespace deluge::gui::context_menu
