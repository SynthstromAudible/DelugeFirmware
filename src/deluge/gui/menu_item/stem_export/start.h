/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "model/settings/runtime_feature_settings.h"
#include "processing/stem_export/stem_export.h"

namespace deluge::gui::menu_item::stem_export {
class Start final : public MenuItem {
public:
	using MenuItem::MenuItem;

	MenuItem* selectButtonPress() override {
		soundEditor.exitCompletely();
		RootUI* rootUI = getRootUI();
		if (rootUI == &arrangerView) {
			if (stemExport.exportMixdown) {
				stemExport.startStemExportProcess(StemExportType::MIXDOWN);
			}
			else {
				stemExport.startStemExportProcess(StemExportType::TRACK);
			}
		}
		else if (rootUI == &sessionView) {
			stemExport.startStemExportProcess(StemExportType::CLIP);
		}
		else if (rootUI == &instrumentClipView && getCurrentOutputType() == OutputType::KIT) {
			// re-check the gating (not just whether the toggle happens to be set) at the point of
			// export, the same way the CHOKE GROUP voice menu item itself re-checks it in
			// PolyphonyType::selectButtonPress() rather than only hiding the toggle that leads here
			if (stemExport.exportChokeGroups && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ChokeGroups)
			    && stemExport.currentKitHasBundlingChokeGroup()) {
				stemExport.startStemExportProcess(StemExportType::CHOKE_GROUP);
			}
			else {
				stemExport.startStemExportProcess(StemExportType::DRUM);
			}
		}
		return NO_NAVIGATION;
	}

	bool shouldEnterSubmenu() override { return false; }
};

/// Toggle in the Kit's Configure Export submenu. Off, a kit exports one file per pad. On, pads that
/// share a choke group are rendered together into one file instead, and every other pad is still
/// exported on its own - see Start::selectButtonPress() above for where this is actually consulted.
class ExportChokeGroups final : public ToggleBool {
public:
	using ToggleBool::ToggleBool;

	// Only offer the choice when the community feature is on AND turning it on would change the
	// output - that is, when some choke group holds more than one exportable drum. If no group
	// does, every drum is rendered on its own either way and there is nothing to choose.
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ChokeGroups)
		       && stemExport.currentKitHasBundlingChokeGroup();
	}
};
} // namespace deluge::gui::menu_item::stem_export
