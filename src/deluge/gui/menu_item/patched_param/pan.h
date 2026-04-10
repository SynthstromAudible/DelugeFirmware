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
#include "integer.h"

namespace deluge::gui::menu_item::patched_param {
class Pan : public Integer {
public:
	using Integer::Integer;
	void drawValue() override;

protected:
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuRelativeValue; }
	[[nodiscard]] int32_t getMinValue() const override { return kMinMenuRelativeValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return PAN; }
	int32_t getFinalValue() override;
	void readCurrentValue() override;
};
} // namespace deluge::gui::menu_item::patched_param
