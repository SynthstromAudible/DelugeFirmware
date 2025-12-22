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
#include "retrospective_sampler.h"
#include "setting.h"
#include "shift_is_sticky.h"
#include <array>

extern deluge::gui::menu_item::runtime_feature::Setting runtimeFeatureSettingMenuItem;

namespace deluge::gui::menu_item::runtime_feature {

// Generic menu item instances
SettingToggle menuDrumRandomizer(RuntimeFeatureSettingType::DrumRandomizer);
SettingToggle menuFineTempo(RuntimeFeatureSettingType::FineTempoKnob);
SettingToggle menuQuantize(RuntimeFeatureSettingType::Quantize);
SettingToggle menuCatchNotes(RuntimeFeatureSettingType::CatchNotes);
SettingToggle menuDeleteUnusedKitRows(RuntimeFeatureSettingType::DeleteUnusedKitRows);
SettingToggle menuAltGoldenKnobDelayParams(RuntimeFeatureSettingType::AltGoldenKnobDelayParams);
Setting menuSyncScalingAction(RuntimeFeatureSettingType::SyncScalingAction);
DevSysexSetting menuDevSysexAllowed(RuntimeFeatureSettingType::DevSysexAllowed);
SettingToggle menuHighlightIncomingNotes(RuntimeFeatureSettingType::HighlightIncomingNotes);
SettingToggle menuDisplayNornsLayout(RuntimeFeatureSettingType::DisplayNornsLayout);
ShiftIsSticky menuShiftIsSticky{};
SettingToggle menuLightShiftLed(RuntimeFeatureSettingType::LightShiftLed);
SettingToggle menuEnableDX7Engine(RuntimeFeatureSettingType::EnableDX7Engine);
EmulatedDisplay menuEmulatedDisplay{};
SettingToggle menuEnableKeyboardViewSidebarMenuExit(RuntimeFeatureSettingType::EnableKeyboardViewSidebarMenuExit);
SettingToggle menuEnableLaunchEventPlayhead(RuntimeFeatureSettingType::EnableLaunchEventPlayhead);
SettingToggle menuDisplayChordLayout(RuntimeFeatureSettingType::DisplayChordKeyboard);
SettingToggle menuAlternativePlaybackStartBehaviour(RuntimeFeatureSettingType::AlternativePlaybackStartBehaviour);
SettingToggle menuEnableGridViewLoopPads(RuntimeFeatureSettingType::EnableGridViewLoopPads);
SettingToggle menuAlternativeTapTempoBehaviour(RuntimeFeatureSettingType::AlternativeTapTempoBehaviour);
SettingToggle menuHorizontalMenus(RuntimeFeatureSettingType::HorizontalMenus);
SettingToggle menuTrimFromStartOfAudioClip(RuntimeFeatureSettingType::TrimFromStartOfAudioClip);
SettingToggle menuShowBatteryLevel(RuntimeFeatureSettingType::ShowBatteryLevel);

// Number of entries in the subMenuEntries array (total settings minus non-top-level minus retro settings in submenu)
// Note: The 7 retro settings are grouped into menuRetrospectiveSamplerSubmenu, so we subtract 6 (7 settings - 1 submenu
// entry)
constexpr size_t kNumTopLevelEntries = RuntimeFeatureSettingType::MaxElement - kNonTopLevelSettings - 6;

std::array<MenuItem*, kNumTopLevelEntries> subMenuEntries{&menuDrumRandomizer,
                                                          &menuFineTempo,
                                                          &menuQuantize,
                                                          &menuCatchNotes,
                                                          &menuDeleteUnusedKitRows,
                                                          &menuAltGoldenKnobDelayParams,
                                                          &menuDevSysexAllowed,
                                                          &menuSyncScalingAction,
                                                          &menuHighlightIncomingNotes,
                                                          &menuDisplayNornsLayout,
                                                          &menuShiftIsSticky,
                                                          &menuLightShiftLed,
                                                          &menuEnableDX7Engine,
                                                          &menuEmulatedDisplay,
                                                          &menuEnableKeyboardViewSidebarMenuExit,
                                                          &menuEnableLaunchEventPlayhead,
                                                          &menuDisplayChordLayout,
                                                          &menuAlternativePlaybackStartBehaviour,
                                                          &menuEnableGridViewLoopPads,
                                                          &menuAlternativeTapTempoBehaviour,
                                                          &menuHorizontalMenus,
                                                          &menuTrimFromStartOfAudioClip,
                                                          &menuShowBatteryLevel,
                                                          &menuRetrospectiveSamplerSubmenu};

Settings::Settings(l10n::String name, l10n::String title) : menu_item::Submenu(name, title, subMenuEntries) {
}

} // namespace deluge::gui::menu_item::runtime_feature
