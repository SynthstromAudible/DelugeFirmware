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

/// @file
/// @brief The CV/gate output selectors and the MIDI device list. See `dynamic.h`.

#include "scenes/menu/nodes/dynamic.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its
///        own title. The same helper `MultiRangeItem::render` and `settings.cpp` keep.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief Draw @p names as `Selection::drawPixelsForOled` would (`gui/menu_item/selection.cpp:8-30`
///        -> `enum_value`, the toolkit's port of it), and publish the cursor as the chosen output.
///
/// The cursor lives in `MenuState::list[depth]` rather than in `enum_index`, so it survives the
/// descent into the page the select press opens and the walk back up -- which is what legacy's
/// `Selection` value member does, and what its `beginSession(navigatedBackwardFrom != nullptr)` arm
/// reads back (`cv/selection.h:31-39`, `gate/selection.h:33-41`).
///
/// **Published on the press only, not every frame** (fix round 1, minor 3). Legacy assigns
/// `soundEditor.currentSourceIndex` in `selectButtonPress` and nowhere else
/// (`cv/selection.h:42`, `gate/selection.h:47`); walking the cursor and backing out is a read-only
/// gesture there, so it is one here too. `MultiRangeItem` republishes `range_choice` every frame
/// because *its* legacy twin does (`multi_range.cpp:230-247` -- the cursor IS the selection); these
/// two do not, and the difference is legacy's.
///
/// `MenuScene::after_render` drains the choice above its own `render_resp.entered` arm, so the
/// press that publishes it and the `select_chain` call that reads it are the same tick -- which is
/// exactly the order legacy's single statement has.
void render_output_selection(deluge::ui::oled::OledFrame& f, MenuState& state,
                             std::span<const std::string_view> names) {
	draw_title_for(f, state);

	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.render_resp =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), index, names, state.list[d], state.scroll[d]);
	if (state.render_resp.entered && !names.empty()) {
		state.source_choice =
		    static_cast<int8_t>(std::clamp<int32_t>(index, 0, static_cast<int32_t>(names.size()) - 1));
	}
}

/// @brief `beginSession`'s forward arm for both selectors: `setValue(0)`
///        (`cv/selection.h:32-34`, `gate/selection.h:34-36`).
///
/// `CustomItem::on_focus` fires only on a *descent* into the item (`MenuScene::descend_to` ->
/// `enter_leaf`), never on the way back up from the page it opened -- which is precisely legacy's
/// `navigatedBackwardFrom == nullptr` test, one hop earlier. So there is no port of the backward arm
/// (`setValue(soundEditor.currentSourceIndex)`): the cursor this leaves is still in
/// `state.list[depth]` when the user returns, holding the same value that assignment would restore.
///
/// It does **not** publish a `source_choice` either: legacy's `beginSession` moves the item's own
/// value and leaves `soundEditor.currentSourceIndex` alone, and so does this (fix round 1).
void focus_output_selection(MenuState& state) {
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.scroll[d] = deluge::ui::ScrollText{};
}

} // namespace

MIDICable* cable_at(int32_t index) {
	// `Devices::getCable` (`devices.cpp:132-152`). Legacy also `D_PRINTLN`s the out-of-range case;
	// `io/debug/log.h` is not on this layer's include list and the null return says the same thing.
	if (index < kLowestDeviceNum || index >= static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size())) {
		return nullptr;
	}
	switch (index) {
	case -3:
		return &MIDIDeviceManager::dinMIDIPorts;
	case -2:
		return &MIDIDeviceManager::upstreamUSBMIDICable1;
	case -1:
		return &MIDIDeviceManager::upstreamUSBMIDICable2;
	default:
		return MIDIDeviceManager::hostedMIDIDevices[index];
	}
}

size_t connected_cables(std::span<MIDICable*> out) {
	// `Devices::drawPixelsForOled`'s own walk (`devices.cpp:163-176`), without the viewport limit:
	// every connected cable, in index order, DIN first. Legacy states the reason for the
	// `connectionFlags` test on its twin in `Devices::computeScrollForSelected` -- "Disconnected
	// devices aren't drawn, so they don't take up a row" (`devices.cpp:70`).
	size_t n = 0;
	const auto end = static_cast<int32_t>(MIDIDeviceManager::hostedMIDIDevices.size());
	for (int32_t index = kLowestDeviceNum; index < end && n < out.size(); index++) {
		MIDICable* cable = cable_at(index);
		if (cable != nullptr && cable->connectionFlags != 0u) {
			out[n++] = cable;
		}
	}
	return n;
}

// ---------------------------------------------------------------------------------------------
// `cv::Selection` (`gui/menu_item/cv/selection.h`)

void CvSelectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_output_selection(state); // `cv/selection.h:31-39 beginSession`
}

void CvSelectionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `cv::Selection::getOptions` (`cv/selection.h:47-54`): one row per physical CV output.
	static_assert(NUM_PHYSICAL_CV_CHANNELS == 2, "cv::Selection::getOptions names exactly two outputs");
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_CV_OUTPUT_1)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_CV_OUTPUT_2)),
	};
	render_output_selection(f, state, names);
}

deluge::ui::shell::Result CvSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `gate::Selection` (`gui/menu_item/gate/selection.h`)

void GateSelectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_output_selection(state); // `gate/selection.h:33-41 beginSession`
}

void GateSelectionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `gate::Selection::getOptions` (`gate/selection.h:56-71`): the four gate outputs, then the
	// minimum-off-time page -- the one-past-the-last option `gate_selection_chain` tests for.
	static_assert(NUM_GATE_CHANNELS == 4, "gate::Selection::getOptions names exactly four outputs");
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_GATE_OUTPUT_1)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_GATE_OUTPUT_2)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_GATE_OUTPUT_3)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_GATE_OUTPUT_4)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_MINIMUM_OFF_TIME)),
	};
	render_output_selection(f, state, names);
}

deluge::ui::shell::Result GateSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                    const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                    const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `midi::Devices` (`gui/menu_item/midi/devices.{h,cpp}`)

void MidiDevicesItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Devices::beginSession` (`devices.cpp:38-57`): land on the cable the menu is already pointed
	// at if it is still in the list, otherwise on the first one -- which is DIN, because
	// `lowestDeviceNum` is -3 and the walk starts there ("Start on DIN. That's the only one that'll
	// always be there.", `:52`).
	MIDICable* cables[kMaxDeviceRows];
	const size_t n = connected_cables(cables);

	size_t cursor = 0;
	if (ctx.midi_cable != nullptr) {
		for (size_t i = 0; i < n; i++) {
			if (cables[i] == ctx.midi_cable) {
				cursor = i;
				break;
			}
		}
	}

	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.list[d].cursor = static_cast<int16_t>(cursor);
	state.scroll[d] = deluge::ui::ScrollText{};

	// `:55` -- `soundEditor.currentMIDICable = getCable(this->getValue())`. A `CustomItem` gets a
	// `const MenuContext&`, so the choice is recorded and `MenuScene::after_render` applies it (see
	// `MenuState::midi_cable_choice`), exactly as `MultiRangeItem` does for the selected range.
	//
	// **Unconditional, like legacy's own statement** (fix round 1, minor 2). With nothing connected
	// at all -- DIN included -- there is no row to sit on, but legacy still lands on
	// `getCable(lowestDeviceNum)`, which is the DIN cable whatever its `connectionFlags` say
	// (`devices.cpp:138-141`; the guard at `:133-136` only rejects indices, not disconnection). So
	// the empty list resolves to DIN rather than leaving the previous visit's cable in place, which
	// `nullptr` -- this field's "no choice" sentinel -- would have done.
	state.midi_cable_choice = cursor < n ? cables[cursor] : cable_at(kLowestDeviceNum);
}

void MidiDevicesItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	draw_title_for(f, state);

	// `Devices::drawPixelsForOled` (`devices.cpp:158-179`) builds `kOLEDMenuNumOptionsVisible`
	// labels around its own `current_scroll_`; `ui::oled::list` owns the window, so every connected
	// cable becomes a row and the widget picks the slice.
	MIDICable* cables[kMaxDeviceRows];
	const size_t n = connected_cables(cables);

	deluge::ui::oled::ListRow rows[kMaxDeviceRows];
	for (size_t i = 0; i < n; i++) {
		// `getDisplayName()` returns a `char const*` with static or device-owned storage
		// (`io/midi/midi_device.h:100`), so the view outlives this frame.
		rows[i].label = std::string_view{cables[i]->getDisplayName()};
		rows[i].selectable = true;
	}

	const size_t d = depth_of(state);
	state.render_resp = deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state.list[d],
	                                           std::span<const deluge::ui::oled::ListRow>{rows, n}, state.scroll[d]);

	// `selectEncoderAction`'s own tail (`devices.cpp:102`): the cursor IS the selection, and
	// `soundEditor.currentMIDICable` follows it. Legacy's skip-the-disconnected loop (`:86-104`) has
	// no port because the row list is already the connected cables only.
	const int32_t cursor = state.list[d].cursor;
	if (n > 0 && cursor >= 0 && static_cast<size_t>(cursor) < n) {
		state.midi_cable_choice = cables[cursor];
	}
}

deluge::ui::shell::Result MidiDevicesItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

} // namespace deluge::scenes::menu::nodes
