/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/menu_item.h"

namespace deluge::gui::menu_item::sample {

class LoopPoint : public MenuItem {
public:
	LoopPoint(l10n::String newName, uint8_t sourceId) : MenuItem(newName), sourceId_{sourceId} {}

	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) final;
	bool isRangeDependent() final { return true; }
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) final;
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void getColumnLabel(StringBuf& label) override;

	int32_t xZoom{0};
	int32_t xScroll{0};
	int32_t editPos{0};
	MarkerType markerType{MarkerType::NONE};

private:
	uint8_t sourceId_;
};

} // namespace deluge::gui::menu_item::sample
