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

#include "ui/scenes/menu/nodes/dynamic.h"

#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/profile.h"

#include "ui/widgets/oled/list.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/values.h"

#include "midi/device_id.h"
#include "project/device_settings.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>

namespace deluge::ui::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its
///        own title. The same helper `MultiRangeItem::render` and `nodes/settings.cpp` keep.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::widgets::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                                    : std::string_view{});
}

/// @brief Draw @p names as `Selection::drawPixelsForOled` would (`origin/main:src/deluge/gui/menu_item/selection.cpp`
///        -> `enum_value`, the toolkit's port of it), and publish the cursor as the chosen output.
///
/// The cursor lives in `MenuState::list[depth]` rather than in `enum_index`, so it survives the descent into the page
/// the select press opens and the walk back up -- which is what legacy's `Selection` value member does, and what its
/// `beginSession(navigatedBackwardFrom != nullptr)` arm reads back (`cv::Selection::beginSession` in
/// `origin/main:src/deluge/gui/menu_item/cv/selection.h`, `gate::Selection::beginSession` in
/// `origin/main:src/deluge/gui/menu_item/gate/selection.h`).
///
/// **Published on the press only, not every frame.** Legacy assigns `soundEditor.currentSourceIndex` in
/// `selectButtonPress` and nowhere else (`cv::Selection::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/cv/selection.h`, `gate::Selection::selectButtonPress` in
/// `origin/main:src/deluge/gui/menu_item/gate/selection.h`); walking the cursor and backing out is a read-only gesture
/// there, so it is one here too. `MultiRangeItem` republishes `range_choice` every frame because *its* legacy twin does
/// (`origin/main:src/deluge/gui/menu_item/multi_range.cpp` `MultiRange::selectEncoderAction` -- the cursor IS the
/// selection); these two do not, and the difference is legacy's.
///
/// `MenuScene::after_render` drains the choice above its own `render_resp.entered` arm, so the
/// press that publishes it and the `select_chain` call that reads it are the same tick -- which is
/// exactly the order legacy's single statement has. `menu_scene.h`'s mailbox note names this the
/// *load-bearing* drain-order case, and `tables_3_spec.cpp`'s "drains the selector's output index
/// BEFORE the navigation arm, not after" pins it: `source_choice` is published on every entered
/// press, unconditionally -- **including** the row past the last real
/// output (`gate::Selection`'s minimum-off-time row), because `gate_selection_chain`
/// (`nodes/settings.cpp`) routes off that same value the same tick.
///
/// @param real_output_limit How many leading @p names rows are real outputs -- `names.size()` for
///        `cv::Selection` (both rows are), `project::kGateOutputCount` for `gate::Selection` (row 4 is the
///        minimum-off-time page). Serves only to keep `state.real_output_choice` -- see
///        `GateSelectionItem::on_focus_backward` for why `source_choice`/`ctx.source_index` cannot
///        serve that hook: routing needs the value this function just published to include the
///        off-time row, but restoring the cursor on the way back must NOT, and legacy keeps the two
///        apart by never writing `currentSourceIndex` on that branch at all
///        (`Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`).
void render_output_selection(deluge::ui::widgets::oled::OledFrame& f, MenuState& state,
                             std::span<const std::string_view> names, size_t real_output_limit) {
	draw_title_for(f, state);

	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.tick.render_resp = deluge::ui::widgets::oled::enum_value(f, deluge::ui::widgets::oled::menu_rect(f), index,
	                                                               names, state.list[d], state.scroll[d]);
	if (state.tick.render_resp.entered && !names.empty()) {
		state.walk.source_choice =
		    static_cast<int8_t>(std::clamp<int32_t>(index, 0, static_cast<int32_t>(names.size()) - 1));
		if (index >= 0 && static_cast<size_t>(index) < real_output_limit) {
			state.real_output_choice = static_cast<int8_t>(index);
		}
	}
}

/// @brief `beginSession`'s forward arm for both selectors: `setValue(0)` (`cv::Selection::beginSession` in
///        `origin/main:src/deluge/gui/menu_item/cv/selection.h`, `gate::Selection::beginSession` in
///        `origin/main:src/deluge/gui/menu_item/gate/selection.h`).
///
/// `CustomItem::on_focus` fires only on a *descent* into the item (`MenuScene::descend_to` ->
/// `enter_leaf`), never on the way back up from the page it opened -- which is precisely legacy's
/// `navigatedBackwardFrom == nullptr` test, one hop earlier. `CustomItem::on_focus_backward` is
/// the port of that arm (`setValue(soundEditor.currentSourceIndex)`); see
/// `GateSelectionItem::on_focus_backward` for the one of these two that needs it.
///
/// It does **not** publish a `source_choice` either: legacy's `beginSession` moves the item's own
/// value and leaves `soundEditor.currentSourceIndex` alone, and so does this.
void focus_output_selection(MenuState& state) {
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::widgets::core::ListState{};
	state.scroll[d] = deluge::ui::widgets::core::ScrollText{};
}

/// @brief The devices `Devices::drawPixelsForOled` would draw, in its own order: the host's connected devices
///        (`bind::MidiDevices::connected`), copied out so no caller holds the host's list across another call.
///
/// Legacy walks `lowestDeviceNum .. hostedMIDIDevices.size()` and draws a row only for a cable with `connectionFlags`
/// set (`Devices::drawPixelsForOled`, `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`) -- "Disconnected
/// devices aren't drawn, so they don't take up a row" (`Devices::computeScrollForSelected`). So the visible list *is*
/// the connected devices in that order, which is also exactly the set `Devices::selectEncoderAction` skips its way
/// along.
/// @return How many were written; none without a MIDI devices port.
size_t listed_devices(const MenuContext& ctx, std::span<deluge::midi::DeviceId> out) {
	if (ctx.ports.midi == nullptr) {
		return 0;
	}
	const std::span<const deluge::midi::DeviceId> connected = ctx.ports.midi->connected();
	const size_t n = std::min(connected.size(), out.size());
	std::copy_n(connected.begin(), n, out.begin());
	return n;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// `cv::Selection` (`origin/main:src/deluge/gui/menu_item/cv/selection.h`)

void CvSelectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_output_selection(state); // `origin/main:src/deluge/gui/menu_item/cv/selection.h` `beginSession`
}

void CvSelectionItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& /*ctx*/,
                             MenuState& state) const {
	static_assert(std::size(kCvOutputNames) == project::kCvOutputCount,
	              "cv::Selection::getOptions names one row per physical CV output");
	const std::string_view names[] = {
	    deluge::ui::scenes::text_lookup(kCvOutputNames[0]),
	    deluge::ui::scenes::text_lookup(kCvOutputNames[1]),
	};
	render_output_selection(f, state, names, std::size(names)); // both rows are real outputs
}

deluge::ui::shell::Result CvSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `gate::Selection` (`origin/main:src/deluge/gui/menu_item/gate/selection.h`)

void GateSelectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_output_selection(state); // `origin/main:src/deluge/gui/menu_item/gate/selection.h` `beginSession`
	// `setValue(0)`'s forward arm lands the cursor on a real output (row 0), so the backward arm's
	// own fallback does too -- consistent should the very first descent this visit be straight into
	// the off-time row, before `render` below has ever recorded a real one.
	state.real_output_choice = 0;
}

/// @brief `beginSession`'s backward arm: `setValue(soundEditor.currentSourceIndex)`
///        (`Selection::beginSession` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`).
///
/// The one of these two selectors that needs the hook. `focus_output_selection`'s "leave the cursor where it is" answer
/// is right for a real output row -- nothing else has touched it -- but wrong for the minimum-off-time row: legacy's
/// cursor SNAPS to the last real output chosen, discarding the off-time row, because
/// `gate::Selection::selectButtonPress` never writes `currentSourceIndex` on that branch
/// (`Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`) and `beginSession`'s
/// backward arm unconditionally restores from it anyway.
///
/// Restored from `MenuState::real_output_choice`, not from `ctx.source_index` /
/// `MenuContext::source_index` -- see that field's own doc (`menu_scene.h`) for why the two cannot
/// be merged: `gate_selection_chain` (`nodes/settings.cpp`) legitimately reads `ctx.source_index` as
/// `project::kGateOutputCount` while routing to the off-time page this same tick (`menu_scene.h`'s mailbox
/// note calls that drain order load-bearing, and `tables_3_spec.cpp` pins it), so by the time this
/// hook runs on the way back out, `ctx.source_index` may still be sitting on that sentinel.
/// `real_output_choice` never does -- `render` below updates it on every real-output press and
/// nothing else.
void GateSelectionItem::on_focus_backward(const MenuContext& /*ctx*/, MenuState& state) const {
	const size_t d = depth_of(state);
	state.list[d].cursor = static_cast<int16_t>(
	    std::clamp<int32_t>(state.real_output_choice, 0, static_cast<int32_t>(project::kGateOutputCount) - 1));
}

void GateSelectionItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& /*ctx*/,
                               MenuState& state) const {
	static_assert(std::size(kGateOutputNames) == project::kGateOutputCount + 1,
	              "gate::Selection::getOptions names every gate output, then the off-time page");
	const std::string_view names[] = {
	    deluge::ui::scenes::text_lookup(kGateOutputNames[0]), deluge::ui::scenes::text_lookup(kGateOutputNames[1]),
	    deluge::ui::scenes::text_lookup(kGateOutputNames[2]), deluge::ui::scenes::text_lookup(kGateOutputNames[3]),
	    deluge::ui::scenes::text_lookup(kGateOutputNames[4]),
	};
	// `project::kGateOutputCount` (4), not `names.size()` (5): row 4 is the minimum-off-time page, not a
	// real output, so a press on it must not become `real_output_choice` -- see
	// `on_focus_backward` and `render_output_selection`'s own note. `source_choice` still publishes
	// unconditionally either way; only the second field this call adds is limited.
	render_output_selection(f, state, names, project::kGateOutputCount);
}

deluge::ui::shell::Result GateSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                    const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                    const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `midi::Devices` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`)

void MidiDevicesItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Devices::beginSession` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`): land on the device the menu is
	// already pointed at if it is still in the list, otherwise on the first one -- which is DIN, because
	// `lowestDeviceNum` is -3 and the walk starts there ("Start on "DIN". That's the only one that'll always be
	// there.", `Devices::beginSession`).
	std::array<deluge::midi::DeviceId, kMaxDeviceRows> devices{};
	const size_t n = listed_devices(ctx, devices);

	size_t cursor = 0;
	if (ctx.midi_device) {
		for (size_t i = 0; i < n; i++) {
			if (devices[i] == ctx.midi_device) {
				cursor = i;
				break;
			}
		}
	}

	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::widgets::core::ListState{};
	state.list[d].cursor = static_cast<int16_t>(cursor);
	state.scroll[d] = deluge::ui::widgets::core::ScrollText{};

	// `Devices::beginSession` sets `soundEditor`'s current MIDI cable to `getCable(this->getValue())`. A `CustomItem`
	// gets a `const MenuContext&`, so the choice is recorded and `MenuScene::after_render` applies it (see
	// `MenuWalkState::midi_device_choice`), exactly as `MultiRangeItem` does for the selected range.
	//
	// **Unconditional, like legacy's own statement.** With nothing connected at all -- DIN included -- there is no row
	// to sit on, but legacy still lands on `getCable(lowestDeviceNum)`, which is the DIN cable whatever its
	// `connectionFlags` say (`Devices::getCable` in `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`: the `-3`
	// case; the range guard before it only rejects indices, not disconnection). So the empty list resolves to DIN
	// rather than leaving the previous visit's device in place, which a default handle -- this field's "no choice"
	// value -- would have done.
	state.walk.midi_device_choice = cursor < n ? devices[cursor] : deluge::midi::kDin;
}

void MidiDevicesItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// `Devices::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`) builds
	// `kOLEDMenuNumOptionsVisible` labels around its own `current_scroll_`; `ui::widgets::oled::list` owns the window,
	// so every connected device becomes a row and the widget picks the slice.
	std::array<deluge::midi::DeviceId, kMaxDeviceRows> devices{};
	const size_t n = listed_devices(ctx, devices);

	deluge::ui::widgets::oled::ListRow rows[kMaxDeviceRows];
	for (size_t i = 0; i < n; i++) {
		// `Devices::drawPixelsForOled` labels a row with the cable's `getDisplayName()`; the host's names outlive this
		// frame.
		rows[i].label = ctx.ports.midi->name(devices[i]);
		rows[i].selectable = true;
	}

	const size_t d = depth_of(state);
	state.tick.render_resp =
	    deluge::ui::widgets::oled::list(f, deluge::ui::widgets::oled::menu_rect(f), state.list[d],
	                                    std::span<const deluge::ui::widgets::oled::ListRow>{rows, n}, state.scroll[d]);

	// `selectEncoderAction`'s own tail (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`): the cursor IS the
	// selection, and `soundEditor`'s current MIDI cable follows it. Legacy's skip-the-disconnected loop
	// (`Devices::selectEncoderAction`) has no port because the row list is already the connected devices only.
	const int32_t cursor = state.list[d].cursor;
	if (n > 0 && cursor >= 0 && static_cast<size_t>(cursor) < n) {
		state.walk.midi_device_choice = devices[cursor];
	}
}

deluge::ui::shell::Result MidiDevicesItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

} // namespace deluge::ui::scenes::menu::nodes
