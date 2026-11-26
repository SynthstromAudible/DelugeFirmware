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
/// @brief The settings root's hooks and its exported root span. The `Node` literals are in `nodes/settings.h`.
///
/// A DEVICE row's node has no `get`/`set` hook: the bindings read and write it through the device port. What is here
/// is the rest: the MIDI follow inputs' channels and learns (the MIDI devices and learn ports), the turns and formats a
/// row's widget needs, the horizontal-menus after-write, the gate selector's chain, the battery and firmware readouts
/// (the UI facts port) and the voice-usage readouts. None of them reads the song: a setting is device-wide, which is
/// exactly why the settings root's `MenuContext` has a null `params` and a null `sound`.

#include "ui/scenes/menu/nodes/settings.h"

#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/device_settings.h"
#include "ui/scenes/bind/ports/learn_sink.h"
#include "ui/scenes/bind/ports/midi_devices.h"
#include "ui/scenes/l10n_bridge.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h" // MenuState, kMenuDepth -- for the CustomItems below
#include "ui/scenes/profile.h"         // deluge_profile() -- ditto
#include "ui/scenes/registry.h"        // menu_scene()

#include "music/note_names.h"
#include "project/device_settings.h"
#include "project/midi_channels.h"
#include "project/midi_command.h"
#include "ui/gfx/oled/glyphs.h"

#include "ui/shell/scene.h"                       // UiEvent/UiContext/Result -- CustomItem::handle's signature
#include "ui/widgets/core/metrics.h"              // kReadoutValueY
#include "ui/widgets/oled/detail/int_to_string.h" // format_range_integer -- the port of intToString
#include "ui/widgets/oled/frame.h"
#include "ui/widgets/oled/scroll_text.h"
#include "ui/widgets/oled/text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>

namespace deluge::ui::scenes::menu::nodes {

// ---------------------------------------------------------------------------------------------
// MIDI follow

namespace {

/// @brief What @p input is learned to, through the MIDI devices port; none with no port.
std::optional<bind::LearnedBinding> follow_binding(const MenuContext& ctx, project::FollowInput input) {
	return ctx.ports.midi != nullptr ? ctx.ports.midi->follow_input(input) : std::nullopt;
}

} // namespace

int32_t follow_channel_get(const MenuContext& ctx, project::FollowInput input) {
	// `FollowChannel::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`): an
	// input with no channel learned is unassigned, whatever its device.
	const std::optional<bind::LearnedBinding> binding = follow_binding(ctx, input);
	return binding && binding->channel_or_zone ? *binding->channel_or_zone : kFollowChannelUnassigned;
}

void follow_channel_set(const MenuContext& ctx, project::FollowInput input, int32_t value) {
	if (ctx.ports.learn == nullptr) {
		return;
	}
	// `FollowChannel::selectEncoderAction`'s unassigned arm forgets the learned input, device and all; any other value
	// is `writeCurrentValue`, which keeps the device.
	if (value == kFollowChannelUnassigned) {
		ctx.ports.learn->learn_follow(input, std::nullopt);
		return;
	}
	bind::LearnedBinding binding = follow_binding(ctx, input).value_or(bind::LearnedBinding{});
	binding.channel_or_zone = static_cast<uint8_t>(value);
	ctx.ports.learn->learn_follow(input, binding);
}

bool follow_channel_learn(const MenuContext& ctx, project::FollowInput input, const deluge::ui::shell::UiEvent& event) {
	using Kind = deluge::ui::shell::UiEvent::Kind;
	if ((event.kind != Kind::MidiNoteOn && event.kind != Kind::MidiCc) || ctx.ports.learn == nullptr
	    || ctx.ports.midi == nullptr || !ctx.ports.midi->settings(event.device)) {
		return false;
	}
	// The device and the channel; the number the input names none of stays as it was.
	bind::LearnedBinding binding = follow_binding(ctx, input).value_or(bind::LearnedBinding{});
	binding.device = event.device;
	binding.channel_or_zone = event.channel;
	ctx.ports.learn->learn_follow(input, binding);
	return true;
}

bool follow_channel_unlearn(const MenuContext& ctx, project::FollowInput input) {
	if (ctx.ports.learn == nullptr) {
		return false;
	}
	ctx.ports.learn->learn_follow(input, std::nullopt);
	return true;
}

std::optional<int32_t> turn_follow_channel(const MenuContext&, int32_t value, int32_t offset) {
	if (value == kFollowChannelUnassigned) {
		if (offset > 0) {
			return 0;
		}
		if (offset < 0) {
			return project::kMidiChannelUpperZone;
		}
		return value;
	}
	const int32_t next = value + offset;
	return next >= project::kMidiChannelCount || next < 0 ? kFollowChannelUnassigned : next;
}

void follow_channel_draw(project::FollowInput input, const MenuContext& ctx, deluge::ui::widgets::oled::OledFrame& f,
                         int32_t value, deluge::ui::widgets::core::ScrollText& scroll) {
	using deluge::l10n::String;
	// Legacy's canvas rows 20 / 29 / 38 at the Apple face's height 7, laid out as `MidiCommandItem` lays the same rows
	// (`ui/widgets/core/metrics.h`).
	const bool tall = f.style.menu_base_y == deluge::ui::widgets::core::kMenuBaseY64;
	const int32_t row0_y =
	    (tall ? deluge::ui::widgets::core::kMidiCommandRowY64 : deluge::ui::widgets::core::kMidiCommandRowY48)
	    + deluge::ui::widgets::core::kApple7Height7YAdjust;
	const int32_t row_h = f.style.row_height;
	const auto row = [&](int32_t i, int32_t x) {
		return deluge::ui::gfx::oled::Rect{x, row0_y + i * row_h, f.bounds().w - x, row_h};
	};
	const deluge::ui::widgets::oled::TextStyle ts{.font = f.style.row_font};

	deluge::ui::widgets::oled::text(
	    f, row(0, 0),
	    deluge::ui::scenes::text_lookup(
	        sid(bind::read_device_flag(ctx.ports.device, project::device_path::kDifferentiateInputs)
	                ? String::STRING_FOR_INPUT_DIFFERENTIATION_ON
	                : String::STRING_FOR_INPUT_DIFFERENTIATION_OFF)),
	    ts);

	const std::optional<bind::LearnedBinding> binding = follow_binding(ctx, input);
	std::string_view device = deluge::ui::scenes::text_lookup(sid(String::STRING_FOR_FOLLOW_DEVICE_UNASSIGNED));
	if (binding && binding->device) {
		device = ctx.ports.midi->name(*binding->device);
	}
	deluge::ui::widgets::oled::scroll_text(f, row(1, 0), device, f.style.row_font, scroll);

	std::string_view channel_text;
	if (value == project::kMidiChannelLowerZone) {
		channel_text = deluge::ui::scenes::text_lookup(sid(String::STRING_FOR_MPE_LOWER_ZONE));
	}
	else if (value == project::kMidiChannelUpperZone) {
		channel_text = deluge::ui::scenes::text_lookup(sid(String::STRING_FOR_MPE_UPPER_ZONE));
	}
	else if (value == kFollowChannelUnassigned) {
		channel_text = deluge::ui::scenes::text_lookup(sid(String::STRING_FOR_FOLLOW_CHANNEL_UNASSIGNED));
	}
	else {
		channel_text = deluge::ui::scenes::text_lookup(sid(String::STRING_FOR_CHANNEL));
		char buf[12];
		const int32_t shown = value + 1 - (value >= project::kFirstCcChannel ? project::kFirstCcChannel : 0);
		const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), shown);
		deluge::ui::widgets::oled::text(f, row(2, deluge::ui::widgets::oled::spacing_x_for(f.style.row_font) * 8),
		                                std::string_view{buf, static_cast<size_t>(r.ptr - buf)}, ts);
	}
	deluge::ui::widgets::oled::text(f, row(2, 0), channel_text, ts);
}

bool learn_follow_kit_root_note(const MenuContext& ctx, const deluge::ui::shell::UiEvent& event) {
	if (event.kind != deluge::ui::shell::UiEvent::Kind::MidiNoteOn || ctx.ports.device == nullptr) {
		return false;
	}
	return ctx.ports.device->write(project::device_path::kFollowKitRootNote, event.data1) != bind::DeviceWrite::REFUSED;
}

// ---------------------------------------------------------------------------------------------
// Defaults: the rest

int32_t default_resolution_magnitude(const MenuContext& ctx) {
	return bind::read_device_value(ctx.ports.device, project::device_path::kMagnitude);
}
int32_t relative_to_song_magnitude(const MenuContext&) {
	return 1;
}

std::optional<int32_t> pad_brightness_turn(const MenuContext&, int32_t value, int32_t offset) {
	using project::kBrightestPadLevel;
	using project::kDimmestPadLevel;
	using project::kPadLevelShown;
	return std::clamp(value + (offset * kPadLevelShown), kDimmestPadLevel * kPadLevelShown,
	                  kBrightestPadLevel * kPadLevelShown);
}

// ---------------------------------------------------------------------------------------------
// Community features

void apply_horizontal_menus(const MenuContext&, int32_t value) {
	menu_scene().set_horizontal_menus(value != 0);
}

// ---------------------------------------------------------------------------------------------
// 2-3b. The gate selector's chain

const Node* gate_selection_chain(const MenuContext& ctx, const MenuState&) {
	// `gate::Selection::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/gate/selection.h`): the option one
	// past the last physical output is the minimum-off-time page; every other option is that output's mode page.
	// Unclamped on purpose: the equality is against the exact sentinel legacy tests. The mode page's row is the chosen
	// output's, which an index past the outputs does not have.
	//
	// Reads `ctx.source_index`, not `state`'s own cursor: `menu_scene.h`'s mailbox note names this
	// the *load-bearing* drain-order case, and `tables_3_spec.cpp`'s "drains the selector's output
	// index BEFORE the navigation arm, not after" pins exactly this read. `GateSelectionItem`'s
	// backward arm (`nodes/dynamic.cpp`) needs a DIFFERENT value on the way back -- "the last real
	// output", which `source_index` is not once this branch has been taken -- and gets it from its
	// own `MenuState::real_output_choice` rather than by disturbing this read.
	return static_cast<std::size_t>(ctx.source_index) == project::kGateOutputCount ? &gate_off_time : &gate_mode;
}

// ---------------------------------------------------------------------------------------------
// 12-13. Battery and firmware (`CustomItem`s -- see `nodes/settings.h`'s note on
// why these are not a shared `Kind`)

namespace {
/// @brief The node currently on screen, so a `CustomItem` -- which is handed no `Node` of its own,
///        only `MenuState` -- can still draw its own title. Mirrors `PendingCustom::render`
///        (`nodes/sound_osc.cpp`).
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::widgets::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                                    : std::string_view{});
}

/// @brief Draw @p text centred at the shared battery/firmware baseline (`kReadoutValueY`), in the
///        huge font -- `firmware::Version`/`battery::Level::drawPixelsForOled`'s shared
///        `drawStringCentredShrinkIfNecessary(str, 22, 18, 20)` (`ui/widgets/core/metrics.h`'s
///        `kReadoutValueY` doc comment has the full derivation).
void draw_readout(deluge::ui::widgets::oled::OledFrame& f, std::string_view text) {
	const deluge::ui::gfx::oled::Rect band{f.bounds().x, f.bounds().y + deluge::ui::widgets::core::kReadoutValueY,
	                                       f.bounds().w, deluge::ui::widgets::oled::text_height_for(f.style.big_font)};
	deluge::ui::widgets::oled::text(
	    f, band, text, {.font = f.style.big_font, .align = deluge::ui::widgets::oled::Align::Centre, .shrink = true});
}
} // namespace

void FirmwareVersionItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx,
                                 MenuState& state) const {
	draw_title_for(f, state);
	// `firmware::Version::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/firmware/version.h`): the build's
	// own version string, a build-time constant with nothing to format.
	// A context with no UI facts has no version to show: the readout is empty.
	draw_readout(f, ctx.ports.ui != nullptr ? ctx.ports.ui->firmware_version() : std::string_view{});
	// The select press leaves the item, same as every other leaf's arm (`menu_scene.cpp`'s
	// `render_leaf_sync_level`, for one) -- `Kind::Custom`'s own dispatch draws nothing else, so
	// this item claims it itself.
	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result FirmwareVersionItem::handle(const deluge::ui::shell::UiEvent&,
                                                      const deluge::ui::shell::UiContext&, const MenuContext&,
                                                      MenuState&) const {
	return {}; // `firmware::Version` has no `buttonAction` override; nothing to consume here either
}

void BatteryLevelItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// A context with no UI facts has no reading: 0 mV, which draws as an empty battery.
	const int32_t battery_mv = ctx.ports.ui != nullptr ? ctx.ports.ui->battery_millivolts() : 0;

	// Charging heuristic, ported from `battery::Level::timerCallback`
	// (`origin/main:src/deluge/gui/menu_item/battery/level.h`): every 4 timer ticks at 500 ms (= 2000 ms), check
	// whether the voltage rose by more than 5 mV since the last check. This port has no separate
	// timer callback to count ticks on -- only a clock reading each time `render()` runs -- so the
	// same 2000 ms window is measured directly off `now_ms()` instead.
	constexpr uint32_t kChargeCheckMs = 2000;
	const uint32_t now = f.now_ms();
	if (!checked_once_) {
		last_check_mv_ = static_cast<uint16_t>(battery_mv);
		last_check_ms_ = now;
		checked_once_ = true;
	}
	else if (now - last_check_ms_ >= kChargeCheckMs) {
		const int32_t diff = battery_mv - static_cast<int32_t>(last_check_mv_);
		// `Level::timerCallback` (`origin/main:src/deluge/gui/menu_item/battery/level.h`)'s own threshold
		charging_ = diff > 5;
		last_check_mv_ = static_cast<uint16_t>(battery_mv);
		last_check_ms_ = now;
	}

	// `battery::Level::getBatteryString` (`origin/main:src/deluge/gui/menu_item/battery/level.h`). 2600 mV / 4200 mV
	// are its own empty/full endpoints.
	constexpr int32_t kMinMv = 2600;
	constexpr int32_t kMaxMv = 4200;
	int32_t percentage = 0;
	if (battery_mv > kMinMv) {
		percentage = (battery_mv - kMinMv) * 100 / (kMaxMv - kMinMv);
		percentage = std::clamp(percentage, 0, 100);
	}

	char buf[48];
	char* out = buf;
	char* const end = buf + sizeof(buf);
	const auto append = [&](std::string_view s) {
		const size_t n = std::min(s.size(), static_cast<size_t>(end - out));
		std::copy_n(s.data(), n, out);
		out += n;
	};
	const auto append_int = [&](int32_t v) {
		const std::to_chars_result r = std::to_chars(out, end, v);
		out = r.ptr;
	};

	append_int(percentage);
	append("% (");
	append_int(battery_mv);
	append("mV)");
	// `Level::getBatteryString` (`origin/main:src/deluge/gui/menu_item/battery/level.h`): FULL takes precedence over
	// CHG -- a battery reading 99%+ is reported full even if the charging heuristic above still thinks it is rising.
	if (percentage >= 99) {
		append(" FULL");
	}
	else if (charging_) {
		append(" CHG");
	}

	draw_readout(f, std::string_view{buf, static_cast<size_t>(out - buf)});

	// `battery::Level::beginSession`/`timerCallback` re-render every 500 ms for as long as the item is on screen
	// (`origin/main:src/deluge/gui/menu_item/battery/level.h`); this port's equivalent asks the frame's own repaint
	// gate to wake it up on the same cadence -- the same shape every other animated widget here uses
	// (`menu_scene.cpp`'s `render_grid`, on the shortcut blink).
	f.gate().request_repaint_after(500, now);

	if (f.select_pressed()) {
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result BatteryLevelItem::handle(const deluge::ui::shell::UiEvent&,
                                                   const deluge::ui::shell::UiContext&, const MenuContext&,
                                                   MenuState&) const {
	return {}; // `battery::Level` has no `buttonAction` override either
}

void BatteryLevelItem::on_focus(const MenuContext&, MenuState&) const {
	// `battery::Level::beginSession` (`origin/main:src/deluge/gui/menu_item/battery/level.h`): reset the baseline
	// every time the item becomes focused, not just once for the whole process -- otherwise
	// leaving the screen and coming back takes the `now - last_check_ms_ >= kChargeCheckMs` branch
	// in `render()` immediately against a stale baseline from the previous visit, computing a
	// delta over an arbitrary real-world gap (a spurious `CHG`, or a missed real one).
	//
	// `checked_once_ = false` is the whole reset: `on_focus` has no `OledFrame` and so no `now_ms()`
	// to stamp a fresh baseline with; the next `render()` call (the first of this new visit) takes
	// the `!checked_once_` branch and captures `last_check_mv_`/`last_check_ms_` off *that* call's
	// clock instead, deferring the next real check a full `kChargeCheckMs` from the moment the item
	// actually became visible again -- exactly what legacy's `voltageCheckCounter = 0` guarantees.
	//
	// `charging_` is reset too, unlike legacy's `isCharging` (never touched in `beginSession`,
	// because legacy's `timerCallback` recomputes it before the ~2 s window is up regardless of
	// visits). Showing a stale charging indicator left over from a previous, unrelated visit would
	// be actively misleading on the very first render of this one; nothing here is charging until
	// the reset baseline says so.
	checked_once_ = false;
	charging_ = false;
}

// ---------------------------------------------------------------------------------------------
// Defaults: the range ends' spellings

std::string_view format_range_integer(int32_t value, const void* /*hook_ctx*/, std::span<char> scratch) {
	// `IntegerRange::getText` (`origin/main:src/deluge/gui/menu_item/integer_range.cpp`) spells each end with
	// `intToString(value, buffer)`, i.e. `intToString(value, buffer, 1)` -- at least one digit, and a
	// leading '-' for a negative one. `int_to_string` is the toolkit's port of exactly that call
	// (`ui/widgets/oled/detail/int_to_string.h`), which `int_value` uses for the same job.
	return deluge::ui::widgets::oled::detail::int_to_string(value, 1, scratch);
}

std::string_view format_range_key(int32_t value, const void* hook_ctx, std::span<char> scratch) {
	// `KeyRange::getText` (`origin/main:src/deluge/gui/menu_item/key_range.cpp`), one end at a time: the note
	// letter, then an accidental on a black key. The device's `defaults/useSharps` row picks both the letter and the
	// sign.
	const auto* ctx = static_cast<const MenuContext*>(hook_ctx);
	const bool sharps =
	    bind::read_device_flag(ctx != nullptr ? ctx->ports.device : nullptr, project::device_path::kUseSharps);
	const deluge::music::NoteName name = deluge::music::note_name(std::clamp<int32_t>(value, 0, 11), sharps);
	size_t len = 0;
	if (len < scratch.size()) {
		scratch[len++] = name.letter;
	}
	if (name.accidental != deluge::music::Accidental::NONE && len < scratch.size()) {
		scratch[len++] = name.accidental == deluge::music::Accidental::SHARP ? '#' : deluge::ui::gfx::oled::kFlatGlyph;
	}
	return std::string_view{scratch.data(), len};
}

// ---------------------------------------------------------------------------------------------

namespace {
constexpr const Node* kRoots[] = {&root_settings};
} // namespace

const std::span<const Node* const> kSettingsRoots{kRoots};

// --------------------------------------------------------------------------------------------- Voice usage
// (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`): the scene keeps the counters, sampled while the page is
// up.

int32_t get_voice_usage_live(const MenuContext&) {
	return menu_scene().voice_usage().live();
}
int32_t get_voice_usage_average(const MenuContext&) {
	return menu_scene().voice_usage().average();
}
int32_t get_voice_usage_maximum(const MenuContext&) {
	return menu_scene().voice_usage().maximum();
}
void set_voice_usage_count(const MenuContext&, int32_t) {
	// `VoiceCount::selectEncoderAction` does nothing (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`).
}
bool voice_usage_permitted(const MenuContext&) {
	// `VoiceCount::selectEncoderActionIsPermitted` (`origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`).
	return false;
}

} // namespace deluge::ui::scenes::menu::nodes
