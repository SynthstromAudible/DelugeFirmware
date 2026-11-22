/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief Driving the shell guest from an integration scenario: its tick, its input, what it has on top, and the
///        browser's selection.
///
/// The guest takes input through `shellHostUI` as legacy's UIs take it, but a press only reaches a scene on the
/// guest's next tick, which is where the host also performs what a scene parked (claims, imports, previews). The app
/// loop's "ui routine" job ticks the guest between scenario steps, but the headless sample clock stands still, so
/// nothing timed comes due there: not a long press, and not the render a scene files a load's end from (a browser
/// closing on its claim). A step that presses and reads the result, or waits on a scene, ticks it here, moving the
/// clock as it goes.

#include "gui/ui/shell_clock.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include "model/favourite/favourite_manager.h"
#include "model/favourite/favourites_policy.h"
#include "processing/engines/audio_engine.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "storage/file_catalogue.h"
#include "ui/shell/shell.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace deluge::harness {

/// One guest tick @p ms on: legacy's UI clock moved as the render would move it, then the host's tick, which performs
/// what the browser parked (claims, imports, previews).
inline void guestTick(uint32_t ms = 10) {
	AudioEngine::audioSampleTimer += ms * deluge::gui::ui::SampleClockMs::kSamplesPerMs;
	if (isUIOpen(&shellHostUI)) {
		(void)shellHostUI.timerCallback();
	}
}

/// Whether the guest is open with @p kind taking its input.
inline bool guestTop(deluge::ui::shell::SceneKind kind) {
	return isUIOpen(&shellHostUI) && shellHostUI.shell().stack().input_top().kind() == kind;
}

inline bool browserOnTop() {
	return guestTop(deluge::scenes::browser::kBrowserSceneKind);
}

/// The guest closed as its long-press BACK closes it, if it is open.
inline void guestClose() {
	if (isUIOpen(&shellHostUI)) {
		(void)shellHostUI.exitUI();
	}
}

/// A short press of @p button on the guest, and the tick that delivers it.
inline void guestPress(deluge::hid::Button button) {
	(void)shellHostUI.buttonAction(button, true);
	(void)shellHostUI.buttonAction(button, false);
	guestTick();
}

/// The Select encoder's short press, as a user's press delivers it.
inline void guestSelect() {
	guestPress(deluge::hid::button::SELECT_ENC);
}

inline void guestBack() {
	guestPress(deluge::hid::button::BACK);
}

/// Select held past the browser's long press, then let go: the context menu for what the browser chooses for.
inline void guestLongPressSelect() {
	using namespace deluge::hid::button;
	(void)shellHostUI.buttonAction(SELECT_ENC, true);
	for (uint32_t t = 0; t <= deluge::scenes::browser::kLongPressMs + 20; t += 10) {
		guestTick();
	}
	(void)shellHostUI.buttonAction(SELECT_ENC, false);
	guestTick();
}

/// Turn the context menu onto @p option (its index in the menu's option table, as legacy's `currentOption`) and
/// accept it.
/// @return Whether the menu reached the option; nothing is accepted when it did not.
inline bool guestPickOption(uint8_t option) {
	auto& menu = deluge::scenes::context_menu::context_menu_scene();
	for (int32_t i = 0; i < 8 && menu.selected_option() != option; i++) {
		shellHostUI.selectEncoderAction(1);
		guestTick();
	}
	if (menu.selected_option() != option) {
		return false;
	}
	guestSelect();
	return true;
}

/// The browser's selected item's name, its extension included, as legacy's `getCurrentFileItem()->filename`: the
/// browser selects by index into the shared listing. Empty with no browser on top or nothing selected.
inline std::string guestSelectedName() {
	const auto& items = deluge::storage::FileCatalogue::fileItems;
	const int32_t index = deluge::scenes::browser::browser_scene().file_index();
	if (!browserOnTop() || index < 0 || index >= static_cast<int32_t>(items.size())) {
		return {};
	}
	return items[index].filename;
}

/// Turn the browser's select encoder onto @p name in the folder it lists: one fast turn, so that only the item it
/// lands on is previewed, as a user's spin of the encoder previews only where it stops.
/// @return Whether the browser's selection is @p name.
inline bool guestTurnTo(std::string_view name) {
	const auto& items = deluge::storage::FileCatalogue::fileItems;
	for (int32_t attempt = 0; attempt < 4 && guestSelectedName() != name; attempt++) {
		const auto found = std::find_if(items.begin(), items.end(), [name](const FileItem& item) {
			return std::string_view{item.filename} == name;
		});
		if (found == items.end()) {
			return false;
		}
		const int32_t offset =
		    static_cast<int32_t>(found - items.begin()) - deluge::scenes::browser::browser_scene().file_index();
		shellHostUI.selectEncoderAction(static_cast<int8_t>(std::clamp<int32_t>(offset, -63, 63)));
		guestTick();
	}
	return guestSelectedName() == name;
}

/// The browser's current folder joined with its selected item's name: the path a Select would claim.
inline std::string guestSelectedPath() {
	const std::string name = guestSelectedName();
	return name.empty() ? std::string{}
	                    : std::string{deluge::scenes::browser::browser_scene().current_dir()} + "/" + name;
}

/// Land the browser on @p path from a favourite, as a user lands on a file they saved: once the bank has loaded, the
/// file is put in slot 0, KEYBOARD keeps the keyboard and its favourites row up, and the slot's pad is pressed. The
/// recall lists the file's folder by its path and selects the file in it, as legacy's `setFileByFullPath` does; the
/// caller waits that listing out, then lets the keyboard go (`guestHideKeyboard`).
/// @return False while the bank is still loading, with nothing pressed.
inline bool guestRecallFavourite(std::string_view path) {
	if (favouritesManager.bankState() != deluge::favourites::BankState::Loaded) {
		return false;
	}
	favouritesManager.setFavorite(0, FavouritesManager::favouriteDefaultColor, std::string{path});
	auto& browser = deluge::scenes::browser::browser_scene();
	if (!browser.keyboard_always_visible()) {
		guestPress(deluge::hid::button::KEYBOARD);
	}
	const int32_t row = browser.favourite_row();
	(void)shellHostUI.padAction(0, row, 64);
	guestTick();
	(void)shellHostUI.padAction(0, row, 0);
	guestTick();
	return true;
}

/// KEYBOARD again, once a recall's listing is in: the keyboard goes back to showing only while typing.
inline void guestHideKeyboard() {
	if (browserOnTop() && deluge::scenes::browser::browser_scene().keyboard_always_visible()) {
		guestPress(deluge::hid::button::KEYBOARD);
	}
}

} // namespace deluge::harness
