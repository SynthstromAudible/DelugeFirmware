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
/// @brief The per-device MIDI page's hooks and its three MPE items. See `midi_device.h` for the
///        node table, the legacy mapping and where `whichDirection`/`whichZone` went.

#include "scenes/menu/nodes/midi_device.h"

#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h" // MenuState, kMenuDepth
#include "scenes/profile.h"

#include "shell/scene.h" // UiEvent/UiContext/Result -- CustomItem::handle's signature
#include "ui/oled/frame.h"
#include "ui/oled/list.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include "definitions_cxx.hpp"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "model/song/song.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its
///        own title and name itself as the node a value change belongs to. The same helper
///        `dynamic.cpp`, `settings.cpp` and `patch_cables.cpp` keep.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title(deluge::ui::oled::OledFrame& f, std::string_view text) {
	deluge::ui::oled::title(f, text);
}

void draw_node_title(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	draw_title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch) : std::string_view{});
}

/// @brief Draw a two-option `Selection` and leave the cursor in `MenuState::list[depth]`.
///
/// `Selection::drawPixelsForOled` (`gui/menu_item/selection.cpp:8-30`) as the toolkit spells it.
/// The cursor lives in `list[depth]` rather than in `enum_index` for the reason
/// `dynamic.cpp`'s `render_output_selection` gives: it has to survive the descent into the page the
/// select press opens and the walk back up, which is what legacy's `Selection` value member does.
void render_selection(deluge::ui::oled::OledFrame& f, MenuState& state, std::span<const std::string_view> names) {
	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.render_resp =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), index, names, state.list[d], state.scroll[d]);
}

/// @brief The cursor, clamped into @p count options.
uint8_t chosen(const MenuState& state, int32_t count) {
	const size_t d = depth_of(state);
	return static_cast<uint8_t>(std::clamp<int32_t>(state.list[d].cursor, 0, count - 1));
}

/// @brief `beginSession`'s forward arm for both MPE selectors: reset the cursor to option 0.
///
/// `DirectionSelector::beginSession` (`mpe/direction_selector.cpp:24-29`) and
/// `ZoneSelector::beginSession` (`mpe/zone_selector.cpp:25-30`) assign their member only when
/// `navigatedBackwardFrom == nullptr`; `CustomItem::on_focus` fires only on a descent
/// (`src/scenes/README.md`, "`on_focus` is the forward arm only"), which is that condition one hop
/// earlier. Both legacy zero values are 0 -- `MIDI_DIRECTION_INPUT_TO_DELUGE` and
/// `MPE_ZONE_LOWER_NUMBERED_FROM_0` (`io/midi/midi_device.h:33`, `:36`).
void focus_selection(MenuState& state) {
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.scroll[d] = deluge::ui::ScrollText{};
}

/// @brief `ZoneNumMemberChannels::getPort()` (`mpe/zone_num_member_channels.cpp:29-31`):
///        `soundEditor.currentMIDICable->ports[directionSelectorMenu.whichDirection]`.
/// @return The port, or null when no cable is selected (see `midi_device.h`'s note on the guard).
MIDIPort* mpe_port(const MenuContext& ctx, const MenuState& state) {
	if (ctx.midi_cable == nullptr) {
		return nullptr;
	}
	return &ctx.midi_cable->ports[state.mpe_direction != 0 ? 1 : 0];
}

} // namespace

IntRange mpe_zone_channels_range(const MenuContext& ctx, const MenuState& state) {
	// `ZoneNumMemberChannels::getMaxValue()` (`mpe/zone_num_member_channels.cpp:33-54`), transcribed
	// statement for statement. The minimum is `Number::getMinValue`'s 0.
	const MIDIPort* port = mpe_port(ctx, state);
	if (port == nullptr) {
		return IntRange{0, 0, 1};
	}
	// "This limits it so we don't eat into the other zone" (`:36`).
	int32_t num_channels_available = state.mpe_zone == MPE_ZONE_LOWER_NUMBERED_FROM_0
	                                     ? port->mpeUpperZoneLastMemberChannel
	                                     : 15 - port->mpeLowerZoneLastMemberChannel;

	int32_t num_member_channels_available = num_channels_available - 1;
	if (num_member_channels_available < 0) {
		num_member_channels_available = 0;
	}
	if (num_member_channels_available == 14) {
		num_member_channels_available = 15;
	}
	return IntRange{0, num_member_channels_available, 1};
}

int32_t mpe_zone_channels_value(const MenuContext& ctx, const MenuState& state) {
	// `ZoneNumMemberChannels::readCurrentValue()` (`mpe/zone_num_member_channels.cpp:56-64`).
	const MIDIPort* port = mpe_port(ctx, state);
	if (port == nullptr) {
		return 0;
	}
	return state.mpe_zone == MPE_ZONE_LOWER_NUMBERED_FROM_0 ? port->mpeLowerZoneLastMemberChannel
	                                                        : 15 - port->mpeUpperZoneLastMemberChannel;
}

// ---------------------------------------------------------------------------------------------
// The per-cable binding family. Every hook here reads `MenuContext::midi_cable` and nothing else;
// see `midi_device.h` on why a null one reads 0 and writes nothing.

int32_t get_device_velocity_to_level(const MenuContext& ctx) {
	// `DefaultVelocityToLevel::readCurrentValue` (`midi/default_velocity_to_level.h:29-32`).
	if (ctx.midi_cable == nullptr) {
		return 0;
	}
	return static_cast<int32_t>(
	    ((static_cast<int64_t>(ctx.midi_cable->defaultVelocityToLevel) * kMaxMenuValue + 536870912) >> 30));
}

void set_device_velocity_to_level(const MenuContext& ctx, int32_t value) {
	// `DefaultVelocityToLevel::writeCurrentValue` (`:33-37`). The `currentSong` deref is
	// `ctx.song` here (task-3 ruling 3), and it is guarded: the settings root resolves a song, but
	// nothing in this layer may assume one.
	if (ctx.midi_cable == nullptr) {
		return;
	}
	ctx.midi_cable->defaultVelocityToLevel = value * (2147483648 / (kMaxMenuValue * 2));
	if (ctx.song != nullptr) {
		ctx.song->grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything(*ctx.midi_cable);
	}
	MIDIDeviceManager::anyChangesToSave = true;
}

int32_t get_device_send_clock(const MenuContext& ctx) {
	// `DeviceSendClock::readCurrentValue` (`midi/device_send_clock.h:27`).
	return ctx.midi_cable != nullptr && ctx.midi_cable->sendClock ? 1 : 0;
}

void set_device_send_clock(const MenuContext& ctx, int32_t value) {
	// `DeviceSendClock::writeCurrentValue` (`:28-31`).
	if (ctx.midi_cable == nullptr) {
		return;
	}
	ctx.midi_cable->sendClock = value != 0;
	MIDIDeviceManager::anyChangesToSave = true;
}

int32_t get_device_receive_clock(const MenuContext& ctx) {
	// `DeviceReceiveClock::readCurrentValue` (`midi/device_receive_clock.h:27`).
	return ctx.midi_cable != nullptr && ctx.midi_cable->receiveClock ? 1 : 0;
}

void set_device_receive_clock(const MenuContext& ctx, int32_t value) {
	// `DeviceReceiveClock::writeCurrentValue` (`:28-31`).
	if (ctx.midi_cable == nullptr) {
		return;
	}
	ctx.midi_cable->receiveClock = value != 0;
	MIDIDeviceManager::anyChangesToSave = true;
}

int32_t get_device_is_relative(const MenuContext& ctx) {
	// `DeviceIsRelative::readCurrentValue` (`midi/device_is_relative.h:27`).
	return ctx.midi_cable != nullptr && ctx.midi_cable->is_relative ? 1 : 0;
}

void set_device_is_relative(const MenuContext& ctx, int32_t value) {
	// `DeviceIsRelative::writeCurrentValue` (`:28-31`).
	if (ctx.midi_cable == nullptr) {
		return;
	}
	ctx.midi_cable->is_relative = value != 0;
	MIDIDeviceManager::anyChangesToSave = true;
}

// ---------------------------------------------------------------------------------------------
// `mpe::DirectionSelector` (`gui/menu_item/mpe/direction_selector.{h,cpp}`)

void MpeDirectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_selection(state);
	state.mpe_direction = MIDI_DIRECTION_INPUT_TO_DELUGE; // `direction_selector.cpp:25-27`
}

void MpeDirectionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `DirectionSelector::getTitle()` (`direction_selector.h:40-43`) -- computed from the value,
	// which is why this item cannot be a data node with a static `Node::title`.
	const uint8_t direction = chosen(state, 2);
	draw_title(f, deluge::scenes::text_lookup(
	                  sid(direction != 0 ? String::STRING_FOR_MPE_OUTPUT : String::STRING_FOR_MPE_INPUT)));

	// `DirectionSelector::getOptions` (`direction_selector.h:29-35`).
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_IN)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_OUT)),
	};
	render_selection(f, state, names);

	// `writeCurrentValue` (`direction_selector.h:37`): `whichDirection = getValue()`. A legacy
	// `Value` writes on every encoder move, not only on the press, and `ZoneNumMemberChannels`
	// reads the member -- so this republishes every frame, exactly as `SourceSelectionItem` does
	// for `MenuState::patch_source` (`scenes/menu/nodes/patch_cables.cpp:459-465`).
	state.mpe_direction = chosen(state, 2);
}

deluge::ui::shell::Result MpeDirectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                   const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                   const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `mpe::ZoneSelector` (`gui/menu_item/mpe/zone_selector.{h,cpp}`)

void MpeZoneItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_selection(state);
	state.mpe_zone = MPE_ZONE_LOWER_NUMBERED_FROM_0; // `zone_selector.cpp:25-27`
}

void MpeZoneItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// No `getTitle()` override in legacy and no name either (`zone_selector.cpp:23` default-
	// constructs it), so this draws the node's own title -- see `midi_device.h` on the invention.
	draw_node_title(f, state);

	// `ZoneSelector::getOptions` (`zone_selector.h:32-38`).
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_LOWER_ZONE)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_UPPER_ZONE)),
	};
	render_selection(f, state, names);

	state.mpe_zone = chosen(state, 2); // `writeCurrentValue` (`zone_selector.h:30`)
}

deluge::ui::shell::Result MpeZoneItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                              const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                              const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `mpe::ZoneNumMemberChannels` (`gui/menu_item/mpe/zone_num_member_channels.{h,cpp}`)

void MpeZoneChannelsItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Number::beginSession` -> `readCurrentValue()`. A `Kind::Custom` node's `enter_leaf` does not
	// seed `MenuState::edit_int` (there is no `Binding` to read), so the item does it here -- the
	// one hook that stands for `beginSession`.
	state.edit_int = mpe_zone_channels_value(ctx, state);
}

void MpeZoneChannelsItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_node_title(f, state);

	// `IntegerWithOff::drawPixelsForOled` (`gui/menu_item/integer.cpp:41-50`): the huge centred
	// number, or "OFF" at 0. `f.bounds()`, not `menu_rect(f)`, for the reason
	// `MenuScene::render_leaf_int` spells out -- `int_value` places its digits at a rect-relative
	// `kValueY` calibrated for the full panel.
	const deluge::ui::Response r =
	    deluge::ui::oled::int_value(f, f.bounds(), state.edit_int, mpe_zone_channels_range(ctx, state),
	                                deluge::ui::oled::ValueStyle{.off_at_zero = true}, /*focused=*/true);
	state.render_resp = r;
	if (r.changed) {
		state.value_changed = true;
		state.value_node = focused_of(state);
	}
	// `int_value` owns the gold knobs only, so the select press that leaves the item
	// (`MenuItem::selectButtonPress` returning nullptr -> `goUpOneLevel`) is this scene's to claim,
	// and the select *encoder* is claimed for the same reason `render_leaf_int` claims it.
	if (f.select_pressed()) {
		state.render_resp.entered = true;
	}
	f.claim_turn();
}

void MpeZoneChannelsItem::commit(const MenuContext& ctx, MenuState& state) const {
	// `ZoneNumMemberChannels::writeCurrentValue` (`mpe/zone_num_member_channels.cpp:66-85`).
	MIDIPort* port = mpe_port(ctx, state);
	if (port == nullptr) {
		return;
	}
	if (state.mpe_zone == MPE_ZONE_LOWER_NUMBERED_FROM_0) {
		port->mpeLowerZoneLastMemberChannel = static_cast<uint8_t>(state.edit_int);
	}
	else {
		port->mpeUpperZoneLastMemberChannel = static_cast<uint8_t>(15 - state.edit_int);
	}

	MIDIDeviceManager::recountSmallestMPEZones();
	MIDIDeviceManager::anyChangesToSave = true;

	// "If this was for an output, we transmit an MCM message to tell the device about our MPE zone
	// for the MIDI they'll be receiving from us." (`:78-84`)
	if (state.mpe_direction == MIDI_DIRECTION_OUTPUT_FROM_DELUGE) {
		const int32_t master_channel = state.mpe_zone == MPE_ZONE_LOWER_NUMBERED_FROM_0 ? 0 : 15;
		ctx.midi_cable->sendRPN(master_channel, 0, 6, state.edit_int);
	}
}

deluge::ui::shell::Result MpeZoneChannelsItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                      const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                      const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

} // namespace deluge::scenes::menu::nodes
