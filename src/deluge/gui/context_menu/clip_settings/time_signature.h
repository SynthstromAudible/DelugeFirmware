/*
 * Copyright © 2025 Synthstrom Audible Limited
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

class Clip;

namespace deluge::gui::context_menu::clip_settings {

/// Submenu listing numerator and denominator options for time signature editing
class TimeSignatureMenu final : public ContextMenu {

public:
	TimeSignatureMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }
	bool acceptCurrentOption() override;

	Clip* clip = nullptr;

	char const* getTitle() override;
	std::span<const char*> getOptions() override;
};

/// Scrolls through numerator values 1-32
class TimeSignatureNumeratorMenu final : public ContextMenu {

public:
	TimeSignatureNumeratorMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	char const* getTitle() override;
	std::span<const char*> getOptions() override;
};

/// Scrolls through denominator values 2, 4, 8, 16
class TimeSignatureDenominatorMenu final : public ContextMenu {

public:
	TimeSignatureDenominatorMenu() = default;
	void selectEncoderAction(int8_t offset) override;
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() override { return true; }

	Clip* clip = nullptr;

	char const* getTitle() override;
	std::span<const char*> getOptions() override;
};

extern TimeSignatureMenu timeSignatureMenu;
extern TimeSignatureNumeratorMenu timeSignatureNumerator;
extern TimeSignatureDenominatorMenu timeSignatureDenominator;

} // namespace deluge::gui::context_menu::clip_settings
