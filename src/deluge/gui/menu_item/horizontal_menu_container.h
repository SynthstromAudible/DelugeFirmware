/*
 * Copyright (c) 2025 Leonid Burygin
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

namespace deluge::gui::menu_item {

// A base class for rendering multiple menu items as a single "container" within a horizontal menu
class HorizontalMenuContainer {
public:
	virtual ~HorizontalMenuContainer() = default;
	HorizontalMenuContainer(std::initializer_list<MenuItem*> items) : items_{items} {}

	[[nodiscard]] int32_t getOccupiedSlotsCount() const { return items_.size(); }
	std::span<MenuItem* const> getItems() const { return items_; }

	virtual void render(const SlotPosition& slots, const MenuItem* selected_item, HorizontalMenu* parent) {}

protected:
	deluge::vector<MenuItem*> items_;
};

} // namespace deluge::gui::menu_item
