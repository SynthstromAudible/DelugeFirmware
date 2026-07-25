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
			    && stemExport.currentKitSpansMultipleChokeGroups()) {
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

/// Toggle in the Kit's Configure Export submenu that switches the kit-context export granularity
/// between DRUM (one file per pad, the default) and CHOKE_GROUP (one file per numbered choke group)
/// - see Start::selectButtonPress() above for where this is actually consulted.
class ExportChokeGroups final : public ToggleBool {
public:
	using ToggleBool::ToggleBool;

	// Only offer the choice when the community feature is on AND it would actually produce more
	// than one file - if every eligible drum shares a single choke group, CHOKE_GROUP export
	// degenerates to the same single-file output as leaving this off, so there's nothing to choose.
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ChokeGroups)
		       && stemExport.currentKitSpansMultipleChokeGroups();
	}
};
} // namespace deluge::gui::menu_item::stem_export
