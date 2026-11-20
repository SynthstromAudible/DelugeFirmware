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
/// @brief The per-device MIDI page's hooks and its three MPE items. See `nodes/midi_device.h` for the
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
#include "scenes/bind/ports/midi_devices.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace deluge::scenes::menu::nodes {

namespace {

using bind::MidiDeviceSettings;
using bind::MidiDirection;
using bind::MpeZones;
using deluge::l10n::String;

/// @brief The zone `MenuWalkState::mpe_zone` holds: legacy's `MPE_ZONE_LOWER_NUMBERED_FROM_0` and
///        `MPE_ZONE_UPPER_NUMBERED_FROM_0` (`origin/main:src/deluge/io/midi/midi_device.h`), the zone selector's
///        option order.
enum class MpeZone : uint8_t {
	LOWER = 0, ///< the lower zone, numbered from channel 0
	UPPER = 1, ///< the upper zone, its master on channel 15
};

/// @brief Whether the walk's zone is the lower one.
bool lower_zone(const MenuState& state) {
	return state.walk.mpe_zone == std::to_underlying(MpeZone::LOWER);
}

/// @brief The settings of the device the context names.
/// @return None when it names none, or one the host does not know, or there is no MIDI devices port.
std::optional<MidiDeviceSettings> device_settings(const MenuContext& ctx) {
	if (ctx.ports.midi == nullptr || !ctx.midi_device) {
		return std::nullopt;
	}
	return ctx.ports.midi->settings(ctx.midi_device);
}

/// @brief Read the named device's settings, apply @p change to them, and write them back.
/// @return Whether the host took the write; false, and nothing written, when there is no device to write.
// TODO(c++26): take std::function_ref<void(MidiDeviceSettings&)> once the device's libstdc++ provides it
// (arm-none-eabi GCC 16; 15.2.1 does not).
template <typename Change>
bool edit_device(const MenuContext& ctx, Change change) {
	std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	if (!settings) {
		return false;
	}
	change(*settings);
	return ctx.ports.midi->write(ctx.midi_device, *settings);
}

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so an item -- which is handed no `Node` of its own -- can draw its
///        own title and name itself as the node a value change belongs to. The same helper
///        `dynamic.cpp`, `nodes/settings.cpp` and `nodes/patch_cables.cpp` keep.
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
/// `Selection::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/selection.cpp`) as the toolkit spells it.
/// The cursor lives in `list[depth]` rather than in `enum_index` for the reason
/// `dynamic.cpp`'s `render_output_selection` gives: it has to survive the descent into the page the
/// select press opens and the walk back up, which is what legacy's `Selection` value member does.
void render_selection(deluge::ui::oled::OledFrame& f, MenuState& state, std::span<const std::string_view> names) {
	const size_t d = depth_of(state);
	int32_t index = state.list[d].cursor;
	state.tick.render_resp =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), index, names, state.list[d], state.scroll[d]);
}

/// @brief The cursor, clamped into @p count options.
uint8_t chosen(const MenuState& state, int32_t count) {
	const size_t d = depth_of(state);
	return static_cast<uint8_t>(std::clamp<int32_t>(state.list[d].cursor, 0, count - 1));
}

/// @brief `beginSession`'s forward arm for both MPE selectors: reset the cursor to option 0.
///
/// `DirectionSelector::beginSession` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`) and
/// `ZoneSelector::beginSession` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`) assign their member only
/// when `navigatedBackwardFrom == nullptr`, and `MenuScene::ascend()` never runs `on_focus` at all
/// (`src/scenes/README.md`, "`on_focus` is the forward arm only"). That is the same condition one hop earlier for the
/// path that matters here -- the walk back up from the page this item chained to keeps the cursor rather than resetting
/// it.
///
/// **Not "only on a descent", though:** `enter_leaf` also runs `on_focus`
/// for `FocusCause::NoRowPress` -- `open()`, a `SceneId` deep link, the shortcut-pad jump,
/// `restore_state` rebuilding a saved path -- for any item whose `on_focus_is_press()` is false, as
/// these two are. Only the *ascend* path is excluded. That is right here and needs no
/// `on_focus_is_press`: legacy runs `beginSession` on all of those too
/// (`SoundEditor::focusRegained` -> `beginScreen`), and this hook writes no model state, only a
/// cursor and a `MenuState` selection.
///
/// Both legacy zero values are 0 -- `MIDI_DIRECTION_INPUT_TO_DELUGE` (`io/midi/midi_device.h`)
/// and `MPE_ZONE_LOWER_NUMBERED_FROM_0`.
void focus_selection(MenuState& state) {
	const size_t d = depth_of(state);
	state.list[d] = deluge::ui::ListState{};
	state.scroll[d] = deluge::ui::ScrollText{};
}

/// @brief `ZoneNumMemberChannels::getPort()` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`):
///        the current cable's `ports[directionSelectorMenu.whichDirection]`, as that port's zones.
/// @return The direction's index into `MidiDeviceSettings::zones`.
size_t direction_of(const MenuState& state) {
	return std::to_underlying(state.walk.mpe_direction != 0 ? MidiDirection::OUT : MidiDirection::IN);
}

/// @brief The zones of the port the two MPE selectors name.
/// @return None when no device is selected (see `nodes/midi_device.h`'s note on the guard).
std::optional<MpeZones> mpe_zones(const MenuContext& ctx, const MenuState& state) {
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	if (!settings) {
		return std::nullopt;
	}
	return settings->zones[direction_of(state)];
}

} // namespace

IntRange mpe_zone_channels_range(const MenuContext& ctx, const MenuState& state) {
	// `ZoneNumMemberChannels::getMaxValue()` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`),
	// transcribed statement for statement. The minimum is `Number::getMinValue`'s 0.
	const std::optional<MpeZones> port = mpe_zones(ctx, state);
	if (!port) {
		return IntRange{0, 0, 1};
	}
	// "This limits it so we don't eat into the other zone" (`ZoneNumMemberChannels::getMaxValue`
	// (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`)).
	int32_t num_channels_available = lower_zone(state) ? port->upper_last_member : 15 - port->lower_last_member;

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
	// `ZoneNumMemberChannels::readCurrentValue()`
	// (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`).
	const std::optional<MpeZones> port = mpe_zones(ctx, state);
	if (!port) {
		return 0;
	}
	return lower_zone(state) ? port->lower_last_member : 15 - port->upper_last_member;
}

// ---------------------------------------------------------------------------------------------
// The per-device binding family. Every hook here reads and writes the device `MenuContext::midi_device` names, through
// the MIDI devices port, and nothing else; see `nodes/midi_device.h` on why none reads 0 and writes nothing.

int32_t get_device_velocity_to_level(const MenuContext& ctx) {
	// `DefaultVelocityToLevel::readCurrentValue`
	// (`origin/main:src/deluge/gui/menu_item/midi/default_velocity_to_level.h`).
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	if (!settings) {
		return 0;
	}
	return static_cast<int32_t>(
	    ((static_cast<int64_t>(settings->velocity_to_level) * kMaxMenuValue + 536870912) >> 30));
}

void set_device_velocity_to_level(const MenuContext& ctx, int32_t value) {
	// `DefaultVelocityToLevel::writeCurrentValue`: the device's default, then every input on it takes it, by the
	// app's Edit through the sound port (none for a song the app does not play).
	const bool written = edit_device(ctx, [value](MidiDeviceSettings& s) {
		s.velocity_to_level = static_cast<int32_t>(value * (2147483648 / (kMaxMenuValue * 2)));
	});
	if (written && ctx.ports.sound_writes != nullptr) {
		(void)ctx.ports.sound_writes->velocity_to_level_everywhere(ctx.midi_device);
	}
}

int32_t get_device_send_clock(const MenuContext& ctx) {
	// `DeviceSendClock::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_send_clock.h`).
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	return settings && settings->send_clock ? 1 : 0;
}

void set_device_send_clock(const MenuContext& ctx, int32_t value) {
	// `DeviceSendClock::writeCurrentValue`.
	(void)edit_device(ctx, [value](MidiDeviceSettings& s) { s.send_clock = value != 0; });
}

int32_t get_device_receive_clock(const MenuContext& ctx) {
	// `DeviceReceiveClock::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_receive_clock.h`).
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	return settings && settings->receive_clock ? 1 : 0;
}

void set_device_receive_clock(const MenuContext& ctx, int32_t value) {
	// `DeviceReceiveClock::writeCurrentValue`.
	(void)edit_device(ctx, [value](MidiDeviceSettings& s) { s.receive_clock = value != 0; });
}

int32_t get_device_is_relative(const MenuContext& ctx) {
	// `DeviceIsRelative::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/device_is_relative.h`).
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	return settings && settings->relative ? 1 : 0;
}

void set_device_is_relative(const MenuContext& ctx, int32_t value) {
	// `DeviceIsRelative::writeCurrentValue`.
	(void)edit_device(ctx, [value](MidiDeviceSettings& s) { s.relative = value != 0; });
}

int32_t get_device_send_thru(const MenuContext& ctx) {
	const std::optional<MidiDeviceSettings> settings = device_settings(ctx);
	return settings && settings->thru ? 1 : 0;
}

void set_device_send_thru(const MenuContext& ctx, int32_t value) {
	(void)edit_device(ctx, [value](MidiDeviceSettings& s) { s.thru = value != 0; });
}

// ---------------------------------------------------------------------------------------------
// `mpe::DirectionSelector` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`)

void MpeDirectionItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_selection(state);
	// `DirectionSelector::beginSession` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.cpp`).
	state.walk.mpe_direction = std::to_underlying(MidiDirection::IN);
}

void MpeDirectionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `DirectionSelector::getTitle()` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`) -- computed
	// from the value, which is why this item cannot be a data node with a static `Node::title`.
	const uint8_t direction = chosen(state, 2);
	draw_title(f, deluge::scenes::text_lookup(
	                  sid(direction != 0 ? String::STRING_FOR_MPE_OUTPUT : String::STRING_FOR_MPE_INPUT)));

	// `DirectionSelector::getOptions` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`).
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_IN)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_OUT)),
	};
	render_selection(f, state, names);

	// `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/mpe/direction_selector.h`):
	// `whichDirection = getValue()`. A legacy `Value` writes on every encoder move, not only on the press, and
	// `ZoneNumMemberChannels` reads the member -- so this republishes every frame, exactly as `SourceSelectionItem`
	// does for `MenuWalkState::patch_source` (`scenes/menu/nodes/patch_cables.cpp`).
	state.walk.mpe_direction = chosen(state, 2);
}

deluge::ui::shell::Result MpeDirectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                   const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                   const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `mpe::ZoneSelector` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`)

void MpeZoneItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	focus_selection(state);
	// `ZoneSelector::beginSession` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.cpp`).
	state.walk.mpe_zone = std::to_underlying(MpeZone::LOWER);
}

void MpeZoneItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& /*ctx*/, MenuState& state) const {
	// `ZoneSelector` has no name and no `getTitle()`, so legacy's title is empty: the separator alone.
	draw_title(f, {});

	// `ZoneSelector::getOptions` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.h`).
	const std::string_view names[] = {
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_LOWER_ZONE)),
	    deluge::scenes::text_lookup(sid(String::STRING_FOR_UPPER_ZONE)),
	};
	render_selection(f, state, names);

	state.walk.mpe_zone =
	    chosen(state, 2); // `writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/mpe/zone_selector.h`)
}

deluge::ui::shell::Result MpeZoneItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                              const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                              const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// `mpe::ZoneNumMemberChannels` (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`)

void MpeZoneChannelsItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `Number::beginSession` -> `readCurrentValue()`. A `Kind::Custom` node's `enter_leaf` does not
	// seed `MenuState::edit_int` (there is no `Binding` to read), so the item does it here -- the
	// one hook that stands for `beginSession`.
	state.edit_int = mpe_zone_channels_value(ctx, state);
}

void MpeZoneChannelsItem::on_reread(const MenuContext& ctx, MenuState& state) const {
	// `ZoneNumMemberChannels::readCurrentValue`.
	state.edit_int = mpe_zone_channels_value(ctx, state);
}

void MpeZoneChannelsItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_node_title(f, state);

	// `IntegerWithOff::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/integer.cpp`): the huge centred
	// number, or "OFF" at 0. `f.bounds()`, not `menu_rect(f)`, for the reason
	// `MenuView::render_leaf_int` spells out -- `int_value` places its digits at a rect-relative
	// `kValueY` calibrated for the full panel.
	const deluge::ui::Response r =
	    deluge::ui::oled::int_value(f, f.bounds(), state.edit_int, mpe_zone_channels_range(ctx, state),
	                                deluge::ui::oled::ValueStyle{.off_at_zero = true}, /*focused=*/true);
	state.tick.render_resp = r;
	if (r.changed) {
		state.tick.value_changed = true;
		state.tick.value_node = focused_of(state);
	}
	// `int_value` owns the gold knobs only, so the select press that leaves the item
	// (`MenuItem::selectButtonPress` returning nullptr -> `goUpOneLevel`) is this scene's to claim,
	// and the select *encoder* is claimed for the same reason `render_leaf_int` claims it.
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
	f.claim_turn();
}

void MpeZoneChannelsItem::commit(const MenuContext& ctx, MenuState& state) const {
	// `ZoneNumMemberChannels::writeCurrentValue`
	// (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`).
	// The zone, then -- the host's half of the same write -- the recount, the save mark and, for an output, the MCM
	// message that tells the device about its zone: "If this was for an output, we transmit an MCM message to tell the
	// device about our MPE zone for the MIDI they'll be receiving from us."
	const size_t direction = direction_of(state);
	(void)edit_device(ctx, [&](MidiDeviceSettings& s) {
		MpeZones& port = s.zones[direction];
		if (lower_zone(state)) {
			port.lower_last_member = static_cast<uint8_t>(state.edit_int);
		}
		else {
			port.upper_last_member = static_cast<uint8_t>(15 - state.edit_int);
		}
	});
}

deluge::ui::shell::Result MpeZoneChannelsItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                      const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                      const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

ValueText midi_device_title(const MenuContext& ctx, int32_t, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Title || ctx.ports.midi == nullptr || !ctx.midi_device) {
		return kValueTextFallback;
	}
	const std::string_view name = ctx.ports.midi->name(ctx.midi_device);
	const size_t n = std::min(name.size(), out.size());
	std::copy_n(name.begin(), n, out.begin());
	return ValueText{.length = n};
}

} // namespace deluge::scenes::menu::nodes
