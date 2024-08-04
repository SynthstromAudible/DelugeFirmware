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
#include "gui/menu_item/menu_item.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "processing/stem_export/stem_export.h"

namespace deluge::gui::menu_item::stem_export {
class Start final : public MenuItem {
public:
	using MenuItem::MenuItem;

	MenuItem* selectButtonPress() override {
		soundEditor.exitCompletely();
		RootUI* rootUI = getRootUI();
		if (rootUI == &arrangerView) {
			stemExport.startStemExportProcess(StemExportType::TRACK);
		}
		else if (rootUI == &sessionView) {
			stemExport.startStemExportProcess(StemExportType::CLIP);
		}
		return NO_NAVIGATION;
	}

	bool shouldEnterSubmenu() override { return false; }
};
} // namespace deluge::gui::menu_item::stem_export
