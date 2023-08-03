/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "settings.h"
#include "setting.h"

#include "gui/ui/sound_editor.h"
#include "hid/display/numeric_driver.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <iterator>

extern deluge::gui::menu_item::runtime_feature::Setting runtimeFeatureSettingMenuItem;

namespace deluge::gui::menu_item::runtime_feature {

// Generic menu item instances
Setting menuDrumRandomizer(RuntimeFeatureSettingType::DrumRandomizer);
Setting menuMasterCompressorFx(RuntimeFeatureSettingType::MasterCompressorFx);
Setting menuFineTempo(RuntimeFeatureSettingType::FineTempoKnob);
Setting menuQuantize(RuntimeFeatureSettingType::Quantize);
Setting menuPatchCableResolution(RuntimeFeatureSettingType::PatchCableResolution);
Setting menuCatchNotes(RuntimeFeatureSettingType::CatchNotes);
Setting menuDeleteUnusedKitRows(RuntimeFeatureSettingType::DeleteUnusedKitRows);
Setting menuAltGoldenKnobDelayParams(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
Setting menuSyncScalingAction(RuntimeFeatureSettingType::SyncScalingAction);

std::array<MenuItem*, RuntimeFeatureSettingType::MaxElement> subMenuEntries{
    &menuDrumRandomizer,       &menuMasterCompressorFx, &menuFineTempo,           &menuQuantize,
    &menuPatchCableResolution, &menuCatchNotes,         &menuDeleteUnusedKitRows, &menuAltGoldenKnobDelayParams,
    &menuSyncScalingAction,
};

Settings::Settings(char const* name, char const* title)
    : menu_item::Submenu<RuntimeFeatureSettingType::MaxElement>(name, title, subMenuEntries) {
}

} // namespace deluge::gui::menu_item::runtime_feature
