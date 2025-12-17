/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "gui/menu_item/menu_item.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item {

class FileSelector final : public MenuItem {
public:
	FileSelector(l10n::String newName, uint8_t sourceId) : MenuItem(newName), sourceId_{sourceId} {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override;
	MenuItem* selectButtonPress() override;
	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             MultiRange** currentRange) override;

	[[nodiscard]] bool allowToBeginSessionFromHorizontalMenu() override { return true; }
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void getColumnLabel(StringBuf& label) override;

private:
	uint8_t sourceId_;
	static std::string getLastFolderFromPath(String& path);
};

extern FileSelector file0SelectorMenu;
extern FileSelector file1SelectorMenu;
} // namespace deluge::gui::menu_item
