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
#include "hid/display/display.h"
#include "hid/display/oled_canvas/canvas.h"
#include "setting.h"
#include "shift_is_sticky.h"
#include <array>

extern deluge::gui::menu_item::runtime_feature::Setting runtimeFeatureSettingMenuItem;

namespace deluge::gui::menu_item::runtime_feature {

// A SettingToggle that is only relevant (and thus only shown) when an OLED display is
// active — real or emulated. Used for features that have no effect in 7-segment mode.
class OledOnlySettingToggle : public SettingToggle {
public:
	using SettingToggle::SettingToggle;
	bool isRelevant(ModControllableAudio*, int32_t) override { return display->haveOLED(); }
};

// The RoundedCorners toggle additionally pushes its value into the Canvas drawing flag,
// so the low-level primitives don't have to read runtime feature settings themselves.
class RoundedCornersSettingToggle final : public OledOnlySettingToggle {
public:
	using OledOnlySettingToggle::OledOnlySettingToggle;
	void writeCurrentValue() override {
		OledOnlySettingToggle::writeCurrentValue();
		hid::display::oled_canvas::Canvas::roundedCornersEnabled =
		    runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners);
	}
};

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
OledOnlySettingToggle menuHorizontalMenus(RuntimeFeatureSettingType::HorizontalMenus);
SettingToggle menuTrimFromStartOfAudioClip(RuntimeFeatureSettingType::TrimFromStartOfAudioClip);
SettingToggle menuShowBatteryLevel(RuntimeFeatureSettingType::ShowBatteryLevel);
RoundedCornersSettingToggle menuRoundedCorners(RuntimeFeatureSettingType::RoundedCorners);

std::array<MenuItem*, RuntimeFeatureSettingType::MaxElement - kNonTopLevelSettings> subMenuEntries{
    &menuDrumRandomizer,
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
    &menuRoundedCorners,
    &menuTrimFromStartOfAudioClip,
    &menuShowBatteryLevel};

Settings::Settings(l10n::String name, l10n::String title) : menu_item::Submenu(name, title, subMenuEntries) {
}

} // namespace deluge::gui::menu_item::runtime_feature
