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
#include "setting.h"
#include "shift_is_sticky.h"

#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
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
Setting menuQuantizedStutterRate(RuntimeFeatureSettingType::QuantizedStutterRate);
Setting menuAutomationInterpolate(RuntimeFeatureSettingType::AutomationInterpolate);
Setting menuAutomationClearClip(RuntimeFeatureSettingType::AutomationClearClip);
Setting menuAutomationNudgeNote(RuntimeFeatureSettingType::AutomationNudgeNote);
Setting menuAutomationShiftClip(RuntimeFeatureSettingType::AutomationShiftClip);
Setting menuSyncScalingAction(RuntimeFeatureSettingType::SyncScalingAction);
DevSysexSetting menuDevSysexAllowed(RuntimeFeatureSettingType::DevSysexAllowed);
Setting menuHighlightIncomingNotes(RuntimeFeatureSettingType::HighlightIncomingNotes);
Setting menuDisplayNornsLayout(RuntimeFeatureSettingType::DisplayNornsLayout);
ShiftIsSticky menuShiftIsSticky{};
Setting menuLightShiftLed(RuntimeFeatureSettingType::LightShiftLed);

Submenu subMenuAutomation{
    l10n::String::STRING_FOR_COMMUNITY_FEATURE_AUTOMATION,
    {
        &menuAutomationInterpolate,
        &menuAutomationClearClip,
        &menuAutomationNudgeNote,
        &menuAutomationShiftClip,
    },
};

std::array<MenuItem*, RuntimeFeatureSettingType::MaxElement - kNonTopLevelSettings> subMenuEntries{
    &menuDrumRandomizer,         &menuMasterCompressorFx, &menuFineTempo,           &menuQuantize,
    &menuPatchCableResolution,   &menuCatchNotes,         &menuDeleteUnusedKitRows, &menuAltGoldenKnobDelayParams,
    &menuQuantizedStutterRate,   &subMenuAutomation,      &menuDevSysexAllowed,     &menuSyncScalingAction,
    &menuHighlightIncomingNotes, &menuDisplayNornsLayout, &menuShiftIsSticky,       &menuLightShiftLed,
};

Settings::Settings(l10n::String name, l10n::String title) : menu_item::Submenu(name, title, subMenuEntries) {
}

} // namespace deluge::gui::menu_item::runtime_feature
