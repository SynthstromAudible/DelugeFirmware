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
#include "devSysexSetting.h"
#include "emulated_display.h"
#include "setting.h"
#include "shift_is_sticky.h"
#include <array>

extern deluge::gui::menu_item::runtime_feature::Setting runtimeFeatureSettingMenuItem;

namespace deluge::gui::menu_item::runtime_feature {

// Generic menu item instances
Setting menuDrumRandomizer(RuntimeFeatureSettingType::DrumRandomizer);
Setting menuFineTempo(RuntimeFeatureSettingType::FineTempoKnob);
Setting menuQuantize(RuntimeFeatureSettingType::Quantize);
Setting menuPatchCableResolution(RuntimeFeatureSettingType::PatchCableResolution);
Setting menuCatchNotes(RuntimeFeatureSettingType::CatchNotes);
Setting menuDeleteUnusedKitRows(RuntimeFeatureSettingType::DeleteUnusedKitRows);
Setting menuAltGoldenKnobDelayParams(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
Setting menuQuantizedStutterRate(RuntimeFeatureSettingType::QuantizedStutterRate);
Setting menuSyncScalingAction(RuntimeFeatureSettingType::SyncScalingAction);
DevSysexSetting menuDevSysexAllowed(RuntimeFeatureSettingType::DevSysexAllowed);
Setting menuHighlightIncomingNotes(RuntimeFeatureSettingType::HighlightIncomingNotes);
Setting menuDisplayNornsLayout(RuntimeFeatureSettingType::DisplayNornsLayout);
ShiftIsSticky menuShiftIsSticky{};
Setting menuLightShiftLed(RuntimeFeatureSettingType::LightShiftLed);
Setting menuEnableGrainFX(RuntimeFeatureSettingType::EnableGrainFX);
Setting menuEnableDxShortcuts(RuntimeFeatureSettingType::EnableDxShortcuts);
EmulatedDisplay menuEmulatedDisplay{};

std::array<MenuItem*, RuntimeFeatureSettingType::MaxElement - kNonTopLevelSettings> subMenuEntries{
    &menuDrumRandomizer,
    &menuFineTempo,
    &menuQuantize,
    &menuPatchCableResolution,
    &menuCatchNotes,
    &menuDeleteUnusedKitRows,
    &menuAltGoldenKnobDelayParams,
    &menuQuantizedStutterRate,
    &menuDevSysexAllowed,
    &menuSyncScalingAction,
    &menuHighlightIncomingNotes,
    &menuDisplayNornsLayout,
    &menuShiftIsSticky,
    &menuLightShiftLed,
    &menuEnableGrainFX,
    &menuEnableDxShortcuts,
    &menuEmulatedDisplay};

Settings::Settings(l10n::String name, l10n::String title) : menu_item::Submenu(name, title, subMenuEntries) {
}

} // namespace deluge::gui::menu_item::runtime_feature
