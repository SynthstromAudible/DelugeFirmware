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
/// @brief The patch-cable items' members, the four page nodes, and the five chain hooks. See
///        `nodes/patch_cables.h` for the map from legacy globals and for what did not come across.

#include "ui/scenes/menu/nodes/patch_cables.h"
#include "ui/scenes/menu/nodes/patching.h"
#include "ui/scenes/services/source_names.h"

#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/profile.h"

#include "base/notify/notice_queue.h"
#include "ui/widgets/core/led_frame.h"
#include "ui/widgets/oled/list.h"
#include "ui/widgets/oled/routing.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/values.h"

#include "gui/l10n/param_names.h"
#include "instruments/classic/cable_acceptance.h"
#include "instruments/classic/cable_codec.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"
#include "instruments/classic/state_access.h"
#include "plugin/types/param_mapping.h"
#include "project/device_settings.h"
#include "synth/modulation/cable.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace deluge::ui::scenes::menu::nodes {

using deluge::l10n::String;

namespace {
/// @brief The per-voice volume, one of the three volume params a patch cable reaches as one destination.
constexpr uint8_t kLocalVolume =
    deluge::instruments::classic::cable_target_of(deluge::instruments::classic::param::kVolume).param_index();
/// @brief The post-FX volume, the destination that stands for the three volumes.
constexpr uint8_t kPostFxVolume =
    deluge::instruments::classic::cable_target_of(deluge::instruments::classic::param::kVolumePostFX).param_index();
/// @brief The post-FX volume's reverb send, the third of the three volume params.
constexpr uint8_t kPostReverbSendVolume =
    deluge::instruments::classic::cable_target_of(deluge::instruments::classic::param::kVolumePostReverbSend)
        .param_index();
/// @brief The first of the sound's global patched params; the per-voice ones come before it.
constexpr uint8_t kFirstGlobal =
    deluge::instruments::classic::cable_target_of(deluge::instruments::classic::kFirstGlobalParam).param_index();

/// @brief The polarity a cable made without one takes where its source fixes none: the device's setting.
/// @param ctx The menu's context, whose device port holds the setting.
/// @return The device's default cable polarity; bipolar, the row's default, where the context binds no device port.
instruments::classic::Polarity default_patch_cable_polarity(const MenuContext& ctx) {
	return bind::read_device<instruments::classic::Polarity>(ctx.ports.device,
	                                                         project::device_path::kPatchCablePolarity);
}

/// @brief The polarity a new cable from @p source takes.
/// @param ctx    The menu's context, whose device port holds the default polarity.
/// @param source The cable's source.
/// @return The source's fixed polarity, or the device's default where it fixes none.
instruments::classic::Polarity default_polarity(const MenuContext& ctx, PatchSource source) {
	return instruments::classic::fixed_polarity(source).value_or(default_patch_cable_polarity(ctx));
}

// ---------------------------------------------------------------------------------------------
// Text helpers. The two `util/functions.cpp` source-name tables are `services/source_names.h`'s.

using services::source_short_name;

/// @brief `getSourceDisplayName`'s string for @p source, as the id the profile resolves.
uint16_t source_display_string(PatchSource source) {
	return sid(services::source_display_string(source));
}

/// @brief Append @p s to @p out, up to @p cap.
void append(char* out, size_t& len, size_t cap, std::string_view s) {
	for (const char c : s) {
		if (len + 1 < cap) {
			out[len++] = c;
		}
	}
}

/// @brief A menu-unit cable value (-5000..5000) as "-12.34" -- the `floatToString(level / 100, ..., 2, 2)` in
///        `PatchCables::renderOptions` (`origin/main:src/deluge/gui/menu_item/patch_cables.cpp`), without the float.
void append_cable_value(char* out, size_t& len, size_t cap, int32_t value) {
	if (value < 0) {
		append(out, len, cap, "-");
		value = -value;
	}
	char digits[8];
	size_t d = 0;
	int32_t whole = value / 100;
	do {
		digits[d++] = static_cast<char>('0' + whole % 10);
		whole /= 10;
	} while (whole != 0 && d < sizeof(digits));
	while (d > 0) {
		if (len + 1 < cap) {
			out[len++] = digits[--d];
		}
		else {
			--d;
		}
	}
	const int32_t frac = value % 100;
	if (len + 1 < cap) {
		out[len++] = '.';
	}
	if (len + 1 < cap) {
		out[len++] = static_cast<char>('0' + frac / 10);
	}
	if (len + 1 < cap) {
		out[len++] = static_cast<char>('0' + frac % 10);
	}
}

// ---------------------------------------------------------------------------------------------
// Model access.

/// @brief The address of the cable from @p from to @p destination on the sound @p ctx edits, or nullopt where it edits
///        none.
std::optional<deluge::project::ParamAddress> cable_address(const MenuContext& ctx, PatchSource from,
                                                           deluge::synth::CableTarget destination) {
	const deluge::ui::scenes::bind::Target cable{.kind = deluge::ui::scenes::bind::Target::Kind::CABLE,
	                                             .param =
	                                                 deluge::instruments::classic::cable_depth_of(from, destination)};
	return deluge::ui::scenes::bind::param_address(cable, ctx.addresses);
}

/// @brief The address of the sound @p ctx edits (its slot's owner, with the clip), or nullopt where it edits none.
std::optional<deluge::project::ParamAddress> sound_address(const MenuContext& ctx) {
	if (!ctx.addresses.slot || !ctx.addresses.clip) {
		return std::nullopt;
	}
	const deluge::project::SlotAddress& slot = *ctx.addresses.slot;
	return deluge::project::ParamAddress{.owner = slot.kind == deluge::project::SlotAddress::Kind::DRUM
	                                                  ? deluge::project::OwnerId::drum(slot.id)
	                                                  : deluge::project::OwnerId::instrument(slot.id),
	                                     .clip_id = ctx.addresses.clip->clip_id};
}

/// @brief The cables of the sound @p ctx edits, in the engine's order; empty where it edits none.
deluge::ui::scenes::bind::CableList cables_of(const MenuContext& ctx) {
	const std::optional<deluge::project::ParamAddress> owner = sound_address(ctx);
	return owner && ctx.ports.sound != nullptr ? ctx.ports.sound->cables(*owner)
	                                           : deluge::ui::scenes::bind::CableList{};
}

/// @brief A cable's two ends, as legacy's `PatchCable` holds them: its source, and its destination descriptor (the
///        param, with the source of the cable whose depth it modulates when it modulates one).
struct CableEnds {
	PatchSource from{PatchSource::NOT_AVAILABLE};
	deluge::synth::CableTarget destination{};
};

CableEnds ends_of(deluge::plugin::ParamId depth) {
	using deluge::instruments::classic::cable_route;
	const auto route = cable_route(depth);
	if (!route) {
		return {};
	}
	CableEnds ends{.from = static_cast<PatchSource>(route->source)};
	if (const auto inner = cable_route(route->destination)) {
		ends.destination = deluge::synth::CableTarget::depth_of(static_cast<uint8_t>(inner->destination.value),
		                                                        static_cast<PatchSource>(inner->source));
	}
	else {
		ends.destination = deluge::synth::CableTarget::param(static_cast<int32_t>(route->destination.value));
	}
	return ends;
}

/// @brief The engine's cable from @p from to @p destination on the sound @p ctx edits, or nullopt.
std::optional<deluge::ui::scenes::bind::CableFact> find_cable(const MenuContext& ctx, PatchSource from,
                                                              deluge::synth::CableTarget destination) {
	const deluge::plugin::ParamId depth = deluge::instruments::classic::cable_depth_of(from, destination);
	for (const deluge::ui::scenes::bind::CableFact& cable : cables_of(ctx)) {
		if (cable.depth == depth) {
			return cable;
		}
	}
	return std::nullopt;
}

/// @brief The value the cable plays now, as the engine stores it; nullopt where the sound has no such cable.
std::optional<int32_t> cable_value(const MenuContext& ctx, PatchSource from, deluge::synth::CableTarget destination) {
	const std::optional<deluge::project::ParamAddress> address = cable_address(ctx, from, destination);
	return address && ctx.ports.controls != nullptr ? ctx.ports.controls->param_value(*address) : std::nullopt;
}

/// @brief The typed state of the sound @p ctx edits, or null.
const deluge::instruments::classic::SoundState* sound_state_of(const MenuContext& ctx) {
	return ctx.ports.sound != nullptr && ctx.addresses.slot ? ctx.ports.sound->sound_state(*ctx.addresses.slot)
	                                                        : nullptr;
}

/// @brief The destination's short name (`l10n::patched_param_short_name`), the morph params named after what the
///        sound's filter modes make them.
/// @param ctx The menu context, whose sound's filter modes name the morph params.
/// @param p   The destination's index in the patched set: the low byte of a cable's destination.
/// @return The name; empty for an index no patched param has.
std::string_view destination_short_name(const MenuContext& ctx, uint8_t p) {
	const deluge::plugin::ParamId id = deluge::instruments::classic::flat_param_id(p);
	const deluge::instruments::classic::SoundState* state = sound_state_of(ctx);
	if (state != nullptr) {
		deluge::l10n::EnvelopeModes modes{};
		for (size_t e = 0; e < modes.size(); ++e) {
			modes[e] = deluge::instruments::classic::envelope(*state, e).mode;
		}
		if (const auto name = deluge::l10n::envelope_param_name(id, modes, /*brief=*/true)) {
			return *name;
		}
	}
	const deluge::effects::FilterState* filter =
	    state != nullptr ? &deluge::instruments::classic::sound_effects(*state).filter : nullptr;
	return filter != nullptr ? deluge::l10n::patched_param_short_name(id, filter->lpf_mode, filter->hpf_mode)
	                         : deluge::l10n::patched_param_short_name(id);
}

/// @brief `((int64_t)paramValue * kMaxMenuPatchCableValue + (1 << 29)) >>
///        30` -- the stored-to-menu rescale `PatchCableStrength::readCurrentValue` and `PatchCables::renderOptions`
///        share (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`,
///        `origin/main:src/deluge/gui/menu_item/patch_cables.cpp` `PatchCables::renderOptions`).
int32_t cable_value_to_menu(int32_t param_value) {
	return static_cast<int32_t>((static_cast<int64_t>(param_value) * plugin::kCableMenuMax + (1 << 29)) >> 30);
}

/// @brief How many sources a cable can name: every `PatchSource` before `NONE`.
constexpr size_t kNumPatchSources = std::to_underlying(PatchSource::NONE);

/// @brief `SourceSelection`'s own option order (`origin/main:src/deluge/gui/menu_item/source_selection.cpp`) -- NOT the
///        `PatchSource` enumerator order, which is why it is a table rather than a loop over the enum.
constexpr std::array<PatchSource, kNumPatchSources> kSourceMenuContents{
    PatchSource::ENVELOPE_0,
    PatchSource::ENVELOPE_1,
    PatchSource::ENVELOPE_2,
    PatchSource::ENVELOPE_3,
    PatchSource::LFO_GLOBAL_1,
    PatchSource::LFO_LOCAL_1,
    PatchSource::LFO_GLOBAL_2,
    PatchSource::LFO_LOCAL_2,
    PatchSource::VELOCITY,
    PatchSource::NOTE,
    PatchSource::SIDECHAIN,
    PatchSource::RANDOM,
    PatchSource::X,
    PatchSource::Y,
    PatchSource::AFTERTOUCH,
};

/// @brief The destination descriptor a source picker or strength page addresses.
///
/// `Regular` addresses the bare param (`origin/main:src/deluge/gui/menu_item/source_selection/regular.cpp`,
/// `patch_cable_strength::Regular::getDestinationDescriptor` in
/// `origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`); `Range` addresses that param *plus* the
/// regular source, i.e. the cable whose depth is being modulated (`source_selection::Range::getDestinationDescriptor`
/// in `origin/main:src/deluge/gui/menu_item/source_selection/range.cpp`, the `patch_cable_strength` twin in
/// `origin/main:src/deluge/gui/menu_item/patch_cable_strength/range.cpp`).
deluge::synth::CableTarget destination_for(const MenuState& state, bool range) {
	deluge::synth::CableTarget desc{};
	if (range) {
		desc = deluge::synth::CableTarget::depth_of(state.walk.patching_param, state.walk.patch_source);
	}
	else {
		desc = deluge::synth::CableTarget::param(state.walk.patching_param);
	}
	return desc;
}

/// @brief The frame-local rect the two list pages draw their rows into, and the node whose title
///        they draw -- the same three lines `nodes/midi_command.cpp`'s `draw_title_for` owns.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::widgets::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::widgets::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                                    : std::string_view{});
}

/// @brief The per-depth widget state a list page borrows (`MenuState::list` / `::scroll`).
size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

} // namespace

namespace {

/// @brief A cable strength's value text for @p use: the value over 100 to two places, cut to three characters on a
///        column at or below -10.00 (`origin/main:src/deluge/gui/menu_item/decimal.cpp`, "-10.50" keeps "-10").
ValueText spell_cable_value(int32_t value, ValueTextUse use, std::span<char> out) {
	char buf[16];
	size_t len = 0;
	append_cable_value(buf, len, sizeof(buf), value);
	if (use == ValueTextUse::Column && value <= -1000) {
		len = std::min<size_t>(len, 3);
	}
	const size_t n = std::min(len, out.size());
	std::memcpy(out.data(), buf, n);
	return ValueText{.length = n};
}

/// @brief `PatchCableStrength::readCurrentValue` for a `Fixed` item's own cable.
int32_t fixed_strength(const MenuContext& ctx, const PatchCableStrengthItem& item) {
	return cable_value_to_menu(
	    cable_value(ctx, item.fixed_source(), deluge::synth::CableTarget::param(item.fixed_param())).value_or(0));
}

} // namespace

ValueText cable_strength_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Notification && use != ValueTextUse::Column) {
		return kValueTextFallback;
	}
	return spell_cable_value(value, use, out);
}

ValueText ducking_strength_text(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Notification) {
		return kValueTextFallback;
	}
	return spell_cable_value(value, use, out);
}

int32_t vibrato_strength(const MenuContext& ctx) {
	return fixed_strength(ctx, kVibratoItem);
}

int32_t ducking_strength(const MenuContext& ctx) {
	return fixed_strength(ctx, kSidechainVolumeItem);
}

// ---------------------------------------------------------------------------------------------
// PatchCablesItem -- `menu_item::PatchCables`.

void PatchCablesItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	// `PatchCables::beginSession` (`origin/main:src/deluge/gui/menu_item/patch_cables.cpp`): the cursor starts at 0 on
	// a forward entry and is restored from `savedVal` on the way back. The port needs no `savedVal`: coming *back* from
	// a chained page runs `MenuScene::ascend`, which does not call `on_focus` at all, so `list[depth]` still holds the
	// cursor the user left. This hook is therefore the forward-entry arm alone, and `on_focus_backward` (the port of
	// the `savedVal` restore) is left on its do-nothing default -- deliberately, not by omission: `list[depth]` already
	// holding the value `savedVal` would restore is exactly what makes the default correct here, unlike
	// `GateSelectionItem`'s (`nodes/dynamic.cpp`), which needs the hook because its own cursor is
	// not what legacy restores.
	state.list[depth_of(state)] = deluge::ui::widgets::core::ListState{};
	state.scroll[depth_of(state)] = deluge::ui::widgets::core::ScrollText{};
}

void PatchCablesItem::on_reread(const MenuContext& ctx, MenuState& state) const {
	// `PatchCables::readValueAgain`: a cable deleted under the cursor leaves it on the last one.
	deluge::ui::widgets::core::ListState& list = state.list[depth_of(state)];
	const auto count = static_cast<int32_t>(cables_of(ctx).size());
	if (list.cursor >= count) {
		list.cursor = static_cast<int16_t>(std::max(0, count - 1));
	}
}

void PatchCablesItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// `PatchCables::renderOptions` (`origin/main:src/deluge/gui/menu_item/patch_cables.cpp`). Legacy keeps its row
	// buffers in SDRAM BSS because its `options` vector holds views into them across frames; this port rebuilds them
	// every frame in the render frame's own stack, which is what an immediate-mode widget wants and costs 32 x 32
	// bytes.
	constexpr size_t kRowChars = 32;
	static_assert(instruments::classic::kMaxCables <= 64, "the row buffer below is sized for legacy's 32 cables");
	char bufs[instruments::classic::kMaxCables][kRowChars];
	deluge::ui::widgets::oled::ListRow rows[instruments::classic::kMaxCables];
	size_t n = 0;

	for (const deluge::ui::scenes::bind::CableFact& cable : cables_of(ctx)) {
		if (n >= instruments::classic::kMaxCables) {
			break;
		}
		const CableEnds ends = ends_of(cable.depth);
		const deluge::synth::CableTarget desc = ends.destination;
		size_t len = 0;
		char* buf = bufs[n];
		append(buf, len, kRowChars, source_short_name(ends.from));
		append(buf, len, kRowChars, " ");
		if (!desc.is_param()) { // a cable modulating another cable's depth
			append(buf, len, kRowChars, source_short_name(desc.top_source()));
			append(buf, len, kRowChars, " ");
		}
		append_cable_value(buf, len, kRowChars, cable_value_to_menu(cable_value(ctx, ends.from, desc).value_or(0)));
		append(buf, len, kRowChars, " ");
		// The destination's short name, from `l10n::patched_param_short_name_at`.
		append(buf, len, kRowChars, destination_short_name(ctx, static_cast<uint8_t>(desc.param_index())));
		rows[n].label = std::string_view{buf, len};
		rows[n].selectable = true;
		n++;
	}

	// `PatchCables::drawPixelsForOled` is `drawItemsForOled(options, ...)`, i.e. exactly
	// the vertical list `ui::widgets::oled::list` is the port of -- including `selectEncoderAction`'s clamp
	// and scroll (`PatchCables::selectEncoderAction`), which `list` already owns.
	state.tick.render_resp = deluge::ui::widgets::oled::list(
	    f, deluge::ui::widgets::oled::menu_rect(f), state.list[depth_of(state)],
	    std::span<const deluge::ui::widgets::oled::ListRow>{rows, n}, state.scroll[depth_of(state)],
	    /*allow_motion=*/true, /*accelerate=*/true);
	if (n == 0 && f.select_pressed()) {
		// `PatchCables::selectButtonPress`'s "There were no items" arm: with nothing to select, the
		// press goes up a level -- `MenuItem::selectButtonPress`'s own `return nullptr`. `list` never
		// reports `entered` on an empty span, so this is the only way that press is seen.
		state.tick.render_resp.entered = true;
	}
}

deluge::ui::shell::Result PatchCablesItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// SourceSelectionItem -- `menu_item::SourceSelection` and its two subclasses.

namespace {

/// @brief The sources this picker may offer, in `kSourceMenuContents` order.
/// @return How many were written to @p out.
size_t allowed_sources(const MenuContext& ctx, deluge::synth::CableTarget destination, std::span<PatchSource> out) {
	size_t n = 0;
	for (const PatchSource s : kSourceMenuContents) {
		if (n >= out.size()) {
			break;
		}
		if (patching::source_is_allowed(ctx, s, destination)) {
			out[n++] = s;
		}
	}
	return n;
}

} // namespace

void SourceSelectionItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// The half of `PatchedParam::selectButtonPress` a `SelectChainFn` cannot do
	// (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`: `soundEditor.patchingParamSelected = this->getP()`).
	// The node the chain came from is `path[depth - 1]` -- `enter_child` has just pushed this page
	// under it -- and its `bind.param`'s id is `getP()`.
	//
	// Guarded on a patched param so the *other* two ways into this page leave the selection alone:
	// `patch_cables_chain` (which sets `patching_param` from the chosen cable) and a re-entry from
	// the range picker below it.
	if (which_ == Which::Regular && state.depth > 0) {
		const Node* from = state.path[state.depth - 1];
		if (from != nullptr && from->bind.patch_destination.has_value()) {
			state.walk.patching_param = *from->bind.patch_destination;
		}
	}

	const deluge::synth::CableTarget destination = destination_for(state, which_ == Which::Range);
	PatchSource sources[kNumPatchSources];
	const size_t n = allowed_sources(ctx, destination, sources);

	// `SourceSelection::beginSession`'s forward-entry arm
	// (`origin/main:src/deluge/gui/menu_item/source_selection.cpp`): land on the first source that is already patched
	// to this destination, else on the first allowed one. (The `navigatedBackwardFrom` arm -- "put the cursor back on
	// `s`" -- is what `MenuScene::ascend` gives for free by never calling this hook on the way back, exactly as for
	// `PatchCablesItem`.)
	int32_t cursor = 0;
	for (size_t i = 0; i < n; i++) {
		if (patching::source_is_patched(ctx, sources[i], destination)) {
			cursor = static_cast<int32_t>(i);
			break;
		}
	}
	state.list[depth_of(state)] = deluge::ui::widgets::core::ListState{};
	state.list[depth_of(state)].cursor = static_cast<int16_t>(cursor);
	state.scroll[depth_of(state)] = deluge::ui::widgets::core::ScrollText{};
	if (n != 0) {
		PatchSource& chosen = which_ == Which::Range ? state.walk.patch_range_source : state.walk.patch_source;
		chosen = sources[cursor];
	}
}

void SourceSelectionItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx,
                                 MenuState& state) const {
	draw_title_for(f, state);

	const deluge::synth::CableTarget destination = destination_for(state, which_ == Which::Range);
	PatchSource sources[kNumPatchSources];
	const size_t n = allowed_sources(ctx, destination, sources);

	deluge::ui::widgets::oled::ListRow rows[kNumPatchSources];
	deluge::ui::shell::TextScratch scratch[kNumPatchSources];
	for (size_t i = 0; i < n; i++) {
		// The names alone: `shouldDrawDotOnValue` (`origin/main:src/deluge/gui/menu_item/source_selection.cpp`) is the
		// 7-segment display's dot, and `drawPixelsForOled` marks nothing.
		rows[i].label = deluge_profile().text_or_digits(source_display_string(sources[i]), scratch[i]);
		rows[i].selectable = true;
	}

	// `SourceSelection::drawPixelsForOled` hand-builds a three-row window over the *unfiltered* source table, skipping
	// disallowed entries as it walks. This port filters first and hands `list` the allowed sources only, which shows
	// the same rows in the same order and gets the windowing, the clamp and `selectEncoderAction`'s allowed-only walk
	// (`SourceSelection::selectEncoderAction`) from the widget instead of re-deriving them.
	state.tick.render_resp = deluge::ui::widgets::oled::list(
	    f, deluge::ui::widgets::oled::menu_rect(f), state.list[depth_of(state)],
	    std::span<const deluge::ui::widgets::oled::ListRow>{rows, n}, state.scroll[depth_of(state)],
	    /*allow_motion=*/true, /*accelerate=*/true);

	// `selectEncoderAction`'s `s = sourceMenuContents[newValue]`: the cursor IS the value,
	// so it is republished every frame rather than only when it moves.
	const int32_t cursor = state.list[depth_of(state)].cursor;
	if (n != 0 && cursor >= 0 && static_cast<size_t>(cursor) < n) {
		PatchSource& chosen = which_ == Which::Range ? state.walk.patch_range_source : state.walk.patch_source;
		chosen = sources[cursor];
	}
	if (n == 0 && f.select_pressed()) {
		state.tick.render_resp.entered = true; // nothing to pick: the press goes up, as an empty list must
	}
}

deluge::ui::shell::Result SourceSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                      const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                      const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// PatchCableStrengthItem -- `menu_item::PatchCableStrength` and its three subclasses.

namespace {

/// @brief `PatchCableStrength::getS()` -- `Regular`/`Fixed` edit the regular source's cable, `Range` the range source's
///        (`Regular::getS` in `origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`, `Range::getS` in
///        `origin/main:src/deluge/gui/menu_item/patch_cable_strength/range.cpp`).
PatchSource strength_source(const MenuState& state, PatchCableStrengthItem::Which which) {
	return which == PatchCableStrengthItem::Which::Range ? state.walk.patch_range_source : state.walk.patch_source;
}

/// @brief `PatchCableStrength::getDestinationDescriptor()`
/// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.h`).
deluge::synth::CableTarget strength_destination(const MenuState& state, PatchCableStrengthItem::Which which) {
	return destination_for(state, which == PatchCableStrengthItem::Which::Range);
}

/// @brief `PatchCableStrength::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`).
int32_t strength_read(const MenuContext& ctx, const MenuState& state, PatchCableStrengthItem::Which which) {
	// No cable yet reads as zero depth, as in main's `PatchCableStrength::readCurrentValue`.
	return cable_value_to_menu(
	    cable_value(ctx, strength_source(state, which), strength_destination(state, which)).value_or(0));
}

} // namespace

deluge::synth::CableTarget PatchCableStrengthItem::learn_descriptor(const MenuContext& /*ctx*/,
                                                                    const MenuState& state) const {
	if (which_ == Which::Range) {
		// `Range::getLearningThing` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/range.cpp`).
		return deluge::synth::CableTarget::depth_of_depth(state.walk.patching_param, state.walk.patch_source,
		                                                  state.walk.patch_range_source);
	}
	if (which_ == Which::Fixed) {
		// `Fixed` inherits `Regular::getLearningThing`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`) over the destination and source it
		// installs itself (`on_focus` below), so its cable is known before its session begins.
		return deluge::synth::CableTarget::depth_of(param_, source_);
	}
	// `Regular::getLearningThing` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`).
	return deluge::synth::CableTarget::depth_of(state.walk.patching_param, state.walk.patch_source);
}

void PatchCableStrengthItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `PatchCables::selectButtonPress`'s own assignments (`origin/main:src/deluge/gui/menu_item/patch_cables.cpp`):
	// `soundEditor.patchingParamSelected = dest`, `source_selection::regularMenu.s = cable->from`,
	// and for a cable whose destination is itself a cable, `rangeMenu.s = cable->from` with
	// `regularMenu.s` taking the top-level source instead. A `SelectChainFn` is handed a
	// **`const MenuState&`** and cannot write any of it, so -- exactly as for
	// `SourceSelectionItem::on_focus` -- the target does it, recognising where it came from by the
	// node `enter_child` has just pushed it under.
	// Recognised by the parent's *item*, not by its node address: `nodes::patch_cables` lives in
	// `sound_root.h`, which includes this header, and the identity is the item either way.
	const Node* from = state.depth > 0 ? state.path[state.depth - 1] : nullptr;
	if (from != nullptr && from->custom == &kPatchCablesItem) {
		const deluge::ui::scenes::bind::CableList cables = cables_of(ctx);
		const int32_t cursor = state.list[state.depth - 1].cursor;
		if (cursor >= 0 && static_cast<size_t>(cursor) < cables.size()) {
			const CableEnds cable = ends_of(cables[cursor].depth);
			const deluge::synth::CableTarget desc = cable.destination;
			state.walk.patching_param = static_cast<uint8_t>(desc.param_index());
			if (desc.is_param()) {
				state.walk.patch_source = cable.from;
			}
			else {
				state.walk.patch_source = desc.top_source();
				state.walk.patch_range_source = cable.from;
			}
		}
	}

	if (which_ == Which::Fixed) {
		// `Fixed::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/fixed.cpp`): the whole point of the class is that
		// it installs its own destination and source before behaving as a `Regular`.
		state.walk.patching_param = param_;
		state.walk.patch_source = source_;
	}
	else if (which_ == Which::Regular && state.walk.patching_param == kPostFxVolume) {
		// `Regular::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength/regular.cpp`): the three volume params are one
		// destination to the user, so a source that cannot reach POST_FX is retried against POST_REVERB_SEND and then
		// LOCAL_VOLUME before the page gives up.
		const auto disallowed = [&](int32_t p) { return !patching::source_may_patch(ctx, state.walk.patch_source, p); };
		if (disallowed(kPostFxVolume)) {
			state.walk.patching_param = kPostReverbSendVolume;
			if (disallowed(kPostReverbSendVolume)) {
				state.walk.patching_param = kLocalVolume;
			}
		}
	}

	// `PatchCableStrength::beginSession` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`): the
	// polarity the page shows is the cable's own, or -- when there is no cable yet -- the source's default. Legacy also
	// calls `setPatchCablePolarity(polarity_in_the_ui_)` here, which is a no-op in the no-cable case and a write-back
	// of what it just read in the other; the port skips the round trip and keeps the read.
	const PatchSource source = strength_source(state, which_);
	const std::optional<deluge::ui::scenes::bind::CableFact> existing =
	    find_cable(ctx, source, strength_destination(state, which_));
	state.walk.patch_polarity = existing.has_value() ? existing->polarity : default_polarity(ctx, source);

	// `Decimal::beginSession` plus `getDefaultEditPos()`
	// (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.h`): the cursor starts on the hundredths' place.
	state.edit_int = strength_read(ctx, state, which_);
	state.decimal_pos = 2;
	state.cursor_blink = deluge::ui::widgets::core::Blink{};
}

void PatchCableStrengthItem::on_reread(const MenuContext& ctx, MenuState& state) const {
	// `PatchCableStrength::readCurrentValue`: the strength alone; the cursor and polarity stay.
	state.edit_int = strength_read(ctx, state, which_);
}

/// @name This page's layout, which is legacy's own.
///
/// **The whole page is frame-local**: every row below is measured from the top of the visible panel, which is what
/// `f.origin.y` carries. On the reference geometry (`OLED_MAIN_TOPMOST_PIXEL` is 5) each lands on the canvas row
/// `PatchCableStrength::renderOLED` names, and on the taller panel the page hugs the visible top as one piece:
///
///   * the diagram's first row, `extraY + OLED_MAIN_TOPMOST_PIXEL` -- canvas 6; here the rect's own y of 0,
///     plus `routing_chain`'s internal `+1`;
///   * the number's top, `textY = extraY + OLED_MAIN_TOPMOST_PIXEL + 4` -- canvas 10; here `kValueTextY` = 5,
///     its cursor and tick on frame rows 19-21;
///   * the switch's slot band, `startY = 34` -- canvas 34; here `kPolaritySwitchY` = 29.
///
/// @{

/// @brief The number band's first column. Legacy's number is right-aligned at the panel's edge, and its widest string,
///        `-5000` with the cursor on the hundredths, starts at column 70. Clearing the band from there leaves every
///        column a diagram label can reach on the band's rows, and the diagram itself draws across the whole panel,
///        unclipped as legacy's is (`SIDE LEVEL` runs to column 60).
constexpr int32_t kValueBandX = 70;

/// @brief The number's top row, frame-local (`textY`, `patch_cable_strength.cpp renderOLED`), and its band's
///        height: the 13-row digits, a clear row, and the cursor's and tick's three.
/// @{
constexpr int32_t kValueTextY = 5;
constexpr int32_t kValueBandHeight = deluge::ui::gfx::oled::kTextBigSizeY + 4;
/// @}

/// @brief The polarity switch's slot band, frame-local -- legacy's `startY = 34`
///        (`patch_cable_strength.cpp renderOLED`).
///
/// `segmented_switch` inks two rows *above* this one for its bracket (`ui/widgets/oled/routing.h`), so the page's real
/// ink starts at 27; the number's band ends at 21. Five clear rows between them, which is legacy's own gap.
constexpr int32_t kPolaritySwitchY = 29;
/// @}

/// @brief The polarity switch's two slot widths -- `biSlotWidth` / `uniSlotWidth`
///        (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`).
/// @{
constexpr std::array<int32_t, 2> kPolarityWidths{{25, 28}};
constexpr std::array<int32_t, 1> kNoPolarityWidth{{53}};
/// @}

void PatchCableStrengthItem::render(deluge::ui::widgets::oled::OledFrame& f, const MenuContext& ctx,
                                    MenuState& state) const {
	const PatchSource source = strength_source(state, which_);
	const deluge::synth::CableTarget destination = strength_destination(state, which_);
	const deluge::ui::gfx::oled::Rect bounds = f.bounds();

	// `PatchCableStrength::renderOLED`'s first block (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`),
	// through the toolkit widget that ports it (`ui/widgets/oled/routing.h`). The stage order is legacy's: the cable's
	// own source first, then -- for a cable modulating another cable's depth -- that outer cable's source, then the
	// destination param.
	//
	// **No title.** Legacy draws none on this page: `renderOLED` replaces `drawPixelsForOled`
	// wholesale, and the diagram is the page's identity.
	std::array<std::string_view, deluge::ui::widgets::oled::kMaxRoutingStages> stages{};
	size_t n = 0;
	stages[n++] = source_short_name(source);
	if (!destination.is_param()) {
		stages[n++] = source_short_name(destination.top_source());
	}
	stages[n++] = destination_short_name(ctx, static_cast<uint8_t>(destination.param_index()));
	deluge::ui::widgets::oled::routing_chain(f, deluge::ui::gfx::oled::Rect{0, 0, bounds.w, bounds.h},
	                                         std::span<const std::string_view>{stages.data(), n});

	// `PatchCableStrength::renderOLED`'s polarity switcher, through `segmented_switch`. The band's left
	// edge is `bounds.w - 55`, which is legacy's own `startX` of 73 on a 128-column panel, and its
	// row is `kPolaritySwitchY` -- legacy's `startY` of 34, expressed frame-local (see the layout
	// block above). Below the number, which is where legacy puts it.
	const deluge::ui::gfx::oled::Rect switch_area{bounds.w - 55, kPolaritySwitchY, 53,
	                                              deluge::ui::gfx::oled::kTextSpacingY};
	if (instruments::classic::has_polarity(source)) {
		static constexpr std::array<std::string_view, 2> kPolarityLabels{{"BI", "UNI"}};
		deluge::ui::widgets::oled::segmented_switch(
		    f, switch_area, kPolarityLabels, kPolarityWidths,
		    state.walk.patch_polarity == instruments::classic::Polarity::UNIPOLAR ? 1 : 0);
	}
	else {
		// A param with no polarity shows only the default one, full width, uninverted.
		const std::string_view only = instruments::classic::to_string(default_polarity(ctx, source));
		deluge::ui::widgets::oled::segmented_switch(f, switch_area, std::span<const std::string_view>{&only, 1},
		                                            kNoPolarityWidth, -1);
	}

	// `getMinValue`/`getMaxValue` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.h`).
	constexpr IntRange kRange{-plugin::kCableMenuMax, plugin::kCableMenuMax, 1};

	// `PatchCableStrength::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`): a
	// turn while Select is held switches a polarised source's polarity -- up unipolar, down bipolar --, edits nothing,
	// and uses the press up so its release opens nothing. This page is never a horizontal column here (a column draws
	// through `present_column`), which is legacy's `!isInHorizontalMenu()`.
	const int8_t turn = f.turn();
	const bool polarity_turn = turn != 0 && f.select_held() && instruments::classic::has_polarity(source);
	if (polarity_turn) {
		set_polarity(ctx, state,
		             turn > 0 ? instruments::classic::Polarity::UNIPOLAR : instruments::classic::Polarity::BIPOLAR);
		state.tick.use_up_select = true;
	}

	// `renderOLED`'s number, right of the diagram.
	const deluge::ui::gfx::oled::Rect value_rect{kValueBandX, kValueTextY, bounds.w - kValueBandX, kValueBandHeight};
	const deluge::ui::widgets::core::Response r = deluge::ui::widgets::oled::cable_strength_value(
	    f, value_rect, state.edit_int, kRange, state.decimal_pos, state.cursor_blink, /*take_turn=*/!polarity_turn);
	state.tick.render_resp = r;
	if (r.changed) {
		state.tick.value_changed = true;
		state.tick.value_node = focused_of(state);
	}

	if (f.select_pressed()) {
		state.tick.render_resp.entered = true; // -> `patch_cable_strength_chain`
	}
}

void PatchCableStrengthItem::set_polarity(const MenuContext& ctx, MenuState& state,
                                          instruments::classic::Polarity polarity) const {
	// `polarity_in_the_ui_` first, then `setPatchCablePolarity` (main's `PatchCableStrength::buttonAction`), which is a
	// no-op while no cable exists -- `commit` makes a new cable with it.
	state.walk.patch_polarity = polarity;
	// `updatePolarityUI`'s `renderUIsForOled` restarts the cursor's blink dark (`OLED::setupBlink`).
	state.cursor_blink = deluge::ui::widgets::core::Blink{};
	const PatchSource source = strength_source(state, which_);
	const deluge::synth::CableTarget destination = strength_destination(state, which_);
	if (ctx.ports.writes != nullptr && find_cable(ctx, source, destination).has_value()) {
		const std::optional<deluge::project::ParamAddress> address = cable_address(ctx, source, destination);
		if (address) {
			(void)ctx.ports.writes->write(
			    {.target = {.kind = deluge::ui::scenes::bind::Target::Kind::CABLE, .param = address->id},
			     .where = ctx.addresses,
			     .polarity = polarity,
			     .polarity_only = true});
		}
	}
	deluge::base::notify::dirty().raise(deluge::base::notify::Dirty::Oled);
}

std::optional<deluge::project::ParamAddress> PatchCableStrengthItem::automation_address(const MenuContext& ctx,
                                                                                        const MenuState& state) const {
	const deluge::synth::CableTarget learn = learn_descriptor(ctx, state);
	return cable_address(ctx, learn.bottom_source(), learn.modulated());
}

void PatchCableStrengthItem::commit(const MenuContext& ctx, MenuState& state) const {
	// `PatchCableStrength::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`) as a
	// cable request: the Edit makes a missing cable with the polarity the page shows (legacy's `polarity_in_the_ui_`,
	// `PatchCableStrength::getModelStack`), so a polarity chosen while no cable existed survives the cable's making. On
	// a horizontal page's column the new cable takes its source's default, as legacy applies the shown polarity only
	// outside a horizontal menu (`PatchCableStrength::getModelStack`).
	const Node* focused = focused_of(state);
	const bool in_horizontal = focused != nullptr && renders_horizontal(*focused, ctx);
	const instruments::classic::Polarity polarity =
	    in_horizontal ? default_polarity(ctx, strength_source(state, which_)) : state.walk.patch_polarity;
	const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
	    cable_request(ctx, learn_descriptor(ctx, state), state.edit_int, polarity);
	if (request && ctx.ports.writes != nullptr) {
		(void)ctx.ports.writes->write(*request);
	}
}

deluge::ui::shell::Result PatchCableStrengthItem::handle(const deluge::ui::shell::UiEvent& event,
                                                         const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                         const MenuContext& ctx, MenuState& state) const {
	// `PatchCableStrength::buttonAction` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`): on the
	// press, MIDI makes a polarised source's cable bipolar and CV unipolar. Everything else is the scene's.
	using deluge::ui::shell::ButtonId;
	if (event.kind != deluge::ui::shell::UiEvent::Kind::Button || !event.on
	    || (event.button != ButtonId::Midi && event.button != ButtonId::Cv)
	    || !instruments::classic::has_polarity(strength_source(state, which_))) {
		return {};
	}
	set_polarity(ctx, state,
	             event.button == ButtonId::Midi ? instruments::classic::Polarity::BIPOLAR
	                                            : instruments::classic::Polarity::UNIPOLAR);
	return deluge::ui::shell::Result{.handled = true};
}

void PatchCableStrengthItem::leds(deluge::ui::widgets::core::LedFrame& f, const MenuContext& /*ctx*/,
                                  const MenuState& state) const {
	// `updatePolarityUI` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`): MIDI for a bipolar cable,
	// CV for a unipolar one, neither for a source with no polarity.
	constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};
	const bool polarised = instruments::classic::has_polarity(strength_source(state, which_));
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(deluge::ui::shell::ButtonId::Midi),
	            polarised && state.walk.patch_polarity == instruments::classic::Polarity::BIPOLAR ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(deluge::ui::shell::ButtonId::Cv),
	            polarised && state.walk.patch_polarity == instruments::classic::Polarity::UNIPOLAR ? kOn : kOff);
}

// ---------------------------------------------------------------------------------------------
// The chain hooks.

const Node* patched_param_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &source_selection_regular; // `PatchedParam::selectButtonPress`
	                                  // (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`)
}

const Node* source_selection_regular_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &patch_cable_strength_regular; // `Regular::selectButtonPress`
	                                      // (`origin/main:src/deluge/gui/menu_item/source_selection/regular.cpp`)
}

const Node* source_selection_range_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &patch_cable_strength_range; // `source_selection::Range::selectButtonPress`
}

const Node* patch_cable_strength_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &source_selection_range; // `PatchCableStrength::selectButtonPress`
}

const Node* patch_cables_chain(const MenuContext& ctx, const MenuState& state) {
	// `PatchCables::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/patch_cables.cpp`). The cable the cursor
	// is on decides: a cable whose destination is a bare param opens the regular strength page, one whose destination
	// is itself a cable opens the range strength page.
	const deluge::ui::scenes::bind::CableList cables = cables_of(ctx);
	const int32_t cursor = state.list[state.depth < kMenuDepth ? state.depth : 0].cursor;
	if (cursor < 0 || static_cast<size_t>(cursor) >= cables.size()) {
		return nullptr; // no items, so the press ascends
	}
	return ends_of(cables[cursor].depth).destination.is_param() ? &patch_cable_strength_regular
	                                                            : &patch_cable_strength_range;
}

std::optional<deluge::ui::scenes::bind::WriteRequest> cable_request(const MenuContext& ctx,
                                                                    deluge::synth::CableTarget learn,
                                                                    int32_t menu_value,
                                                                    instruments::classic::Polarity polarity) {
	const std::optional<deluge::project::ParamAddress> address =
	    cable_address(ctx, learn.bottom_source(), learn.modulated());
	if (!address) {
		return std::nullopt;
	}
	// `PatchCableStrength::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`):
	// "rescale from 5000 to 2^30. The magic constant is ((2^30)/5000), shifted 32 bits for precision
	// ((1<<(30+32))/5000)".
	const int64_t magic_constant = (INT64_C(922337203685477) * 5000) / plugin::kCableMenuMax;
	return deluge::ui::scenes::bind::WriteRequest{
	    .target = {.kind = deluge::ui::scenes::bind::Target::Kind::CABLE, .param = address->id},
	    .where = ctx.addresses,
	    .menu_value = menu_value,
	    .param_value = static_cast<int32_t>((magic_constant * menu_value) >> 32),
	    .polarity = polarity};
}

// ---------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------
// The shortcut blinks.

namespace {

/// The volume triple is one param to the user: its two other members blink POST_FX's pad.
uint8_t folded_volume(uint8_t param) {
	if (param == kPostReverbSendVolume || param == kLocalVolume) {
		return kPostFxVolume;
	}
	return param;
}

/// The cable under the patch cables page's cursor, or nullopt with none there.
std::optional<CableEnds> cable_under_cursor(const MenuContext& ctx, const MenuState& state) {
	const deluge::ui::scenes::bind::CableList cables = cables_of(ctx);
	const int32_t cursor = state.list[state.depth].cursor;
	if (cursor < 0 || static_cast<size_t>(cursor) >= cables.size()) {
		return std::nullopt;
	}
	return ends_of(cables[cursor].depth);
}

} // namespace

uint8_t PatchCablesItem::patched_param_to_blink(const MenuContext& ctx, const MenuState& state) const {
	const std::optional<CableEnds> cable = cable_under_cursor(ctx, state);
	return cable ? folded_volume(cable->destination.param_index()) : 255;
}

uint8_t PatchCablesItem::source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
                                      deluge::ui::widgets::grid::FlashColour& colour) const {
	const std::optional<CableEnds> cable = cable_under_cursor(ctx, state);
	if (!cable) {
		return 255;
	}
	if (source == cable->from) {
		colour = deluge::ui::widgets::grid::FlashColour::Palette6;
		return 0;
	}
	if (!cable->destination.is_param() && source == cable->destination.top_source()) {
		return 3;
	}
	return 255;
}

uint8_t SourceSelectionItem::patched_param_to_blink(const MenuContext& /*ctx*/, const MenuState& state) const {
	return state.walk.patching_param;
}

uint8_t SourceSelectionItem::source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
                                          deluge::ui::widgets::grid::FlashColour& /*colour*/) const {
	return patching::source_is_patched(ctx, source, destination_for(state, which_ == Which::Range)) ? 3 : 255;
}

uint8_t PatchCableStrengthItem::patched_param_to_blink(const MenuContext& /*ctx*/, const MenuState& state) const {
	return folded_volume(state.walk.patching_param);
}

uint8_t PatchCableStrengthItem::source_blink(const MenuContext& ctx, const MenuState& state, PatchSource source,
                                             deluge::ui::widgets::grid::FlashColour& colour) const {
	switch (which_) {
	case Which::Regular:
		// The source being edited, then a source on that cable's depth.
		if (source == state.walk.patch_source) {
			return 0;
		}
		if (find_cable(ctx, source, learn_descriptor(ctx, state)).has_value()) {
			colour = deluge::ui::widgets::grid::FlashColour::Palette6;
			return 3;
		}
		return 255;
	case Which::Range:
		// The depth's own source, then the cable's source whose depth it controls.
		if (source == state.walk.patch_range_source) {
			colour = deluge::ui::widgets::grid::FlashColour::Palette6;
			return 0;
		}
		return source == state.walk.patch_source ? 3 : 255;
	case Which::Fixed:
		return find_cable(ctx, source, learn_descriptor(ctx, state)).has_value() ? 3 : 255;
	}
	return 255;
}

SourcePress SourceSelectionItem::source_press(const MenuContext& ctx, MenuState& state, PatchSource source,
                                              bool /*previous_held*/, const Node*& open) const {
	if (which_ == Which::Range) {
		return SourcePress::GoUp;
	}
	// The volume triple is one destination: a press from either of its other two members patches POST_FX.
	const uint8_t previous = state.walk.patching_param;
	state.walk.patching_param = folded_volume(previous);
	if (!patching::source_is_allowed(ctx, source, destination_for(state, false))) {
		state.walk.patching_param = previous;
		deluge::base::notify::notice_string(static_cast<uint16_t>(String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
		return SourcePress::Unhandled;
	}
	state.walk.patch_source = source;
	open = &patch_cable_strength_regular;
	return SourcePress::Open;
}

SourcePress PatchCableStrengthItem::source_press(const MenuContext& /*ctx*/, MenuState& state, PatchSource source,
                                                 bool previous_held, const Node*& open) const {
	if (which_ == Which::Range || (which_ == Which::Regular && !previous_held)) {
		return SourcePress::GoUp;
	}
	state.walk.patch_range_source = source;
	open = &patch_cable_strength_range;
	return SourcePress::Open;
}

// The nodes. Defined here rather than in the header -- see `nodes/patch_cables.h` for why (the chain is
// cyclic, so at least one edge has to be an `extern` address rather than a `constexpr` initialiser).

const Node source_selection_regular =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_WITH), &kSourceSelectionRegularItem),
                      &source_selection_regular_chain, kSourceSelectionRegularChain);

const Node patch_cable_strength_regular =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_WITH), &kPatchCableStrengthRegularItem),
                      &patch_cable_strength_chain, kPatchCableStrengthChain);

const Node source_selection_range =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_DEPTH), &kSourceSelectionRangeItem),
                      &source_selection_range_chain, kSourceSelectionRangeChain);

const Node patch_cable_strength_range =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_DEPTH), &kPatchCableStrengthRangeItem),
                      &patch_cable_strength_chain, kPatchCableStrengthChain);

} // namespace deluge::ui::scenes::menu::nodes

namespace deluge::ui::scenes::menu::patching {

using nodes::find_cable;
using nodes::kFirstGlobal;
using nodes::kLocalVolume;
using nodes::kPostFxVolume;
using nodes::kPostReverbSendVolume;
using nodes::sound_state_of;

bool source_may_patch(const MenuContext& ctx, PatchSource source, int32_t param) {
	const deluge::instruments::classic::SoundState* state = sound_state_of(ctx);
	if (state == nullptr) {
		return false;
	}
	const bool is_drum = ctx.addresses.slot->kind == deluge::project::SlotAddress::Kind::DRUM;
	return deluge::instruments::classic::cable_allowed(
	    static_cast<deluge::instruments::classic::PatchSource>(source),
	    deluge::instruments::classic::flat_param_id(static_cast<uint32_t>(param)), *state, is_drum);
}

bool source_is_allowed(const MenuContext& ctx, PatchSource source, deluge::synth::CableTarget destination) {
	// Main's `SourceSelection::sourceIsAllowed` (`origin/main:src/deluge/gui/menu_item/source_selection.cpp`), patching
	// to another cable's range: a global source can control any range, a local one only a cable that goes to a local
	// param.
	if (!destination.is_param()) {
		if (source < PatchSource::ENVELOPE_0) {
			return true;
		}
		return destination.param_index() < kFirstGlobal;
	}
	const int32_t p = destination.param_index();
	const auto allowed = [&](int32_t target) { return source_may_patch(ctx, source, target); };
	// Also main's `sourceIsAllowed`: the volume triple is one destination as far as the user is concerned, so any of
	// the three accepting the source makes it offerable.
	if (p == kPostFxVolume) {
		return allowed(kPostFxVolume) || allowed(kLocalVolume) || allowed(kPostReverbSendVolume);
	}
	return allowed(p);
}

bool source_is_patched(const MenuContext& ctx, PatchSource source, deluge::synth::CableTarget destination) {
	if (find_cable(ctx, source, destination).has_value()) {
		return true;
	}
	if (destination.param_index() != kPostFxVolume) {
		return false;
	}
	// The three volume params are one destination to the user.
	for (const int32_t p : {static_cast<int32_t>(kPostReverbSendVolume), static_cast<int32_t>(kLocalVolume)}) {
		destination = destination.with_param(p);
		if (find_cable(ctx, source, destination).has_value()) {
			return true;
		}
	}
	return false;
}

namespace {

/// Main's `modSourceShortcuts` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), [column][row].
constexpr PatchSource kModSources[2][8] = {
    {PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE,
     PatchSource::NOT_AVAILABLE, PatchSource::LFO_GLOBAL_1, PatchSource::ENVELOPE_0, PatchSource::X},
    {PatchSource::AFTERTOUCH, PatchSource::VELOCITY, PatchSource::RANDOM, PatchSource::NOTE, PatchSource::SIDECHAIN,
     PatchSource::LFO_LOCAL_1, PatchSource::ENVELOPE_1, PatchSource::Y},
};
/// Main's `modSourceShortcutsSecondLayer`.
constexpr PatchSource kModSourcesSecondLayer[2][8] = {
    {PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE,
     PatchSource::NOT_AVAILABLE, PatchSource::LFO_GLOBAL_2, PatchSource::ENVELOPE_2, PatchSource::NOT_AVAILABLE},
    {PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE, PatchSource::NOT_AVAILABLE,
     PatchSource::NOT_AVAILABLE, PatchSource::LFO_LOCAL_2, PatchSource::ENVELOPE_3, PatchSource::NOT_AVAILABLE},
};

} // namespace

PatchSource mod_source_shortcut(project::ShortcutsVersion version, uint8_t sx, uint8_t y, bool second_layer) {
	if (sx >= 2 || y >= 8) {
		return PatchSource::NOT_AVAILABLE;
	}
	// `setShortcutsVersion(SHORTCUTS_VERSION_1)` clears the X/Y pads
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`'s V1 arm).
	if (!second_layer && y == 7 && version == project::ShortcutsVersion::V1) {
		return PatchSource::NOT_AVAILABLE;
	}
	return second_layer ? kModSourcesSecondLayer[sx][y] : kModSources[sx][y];
}

} // namespace deluge::ui::scenes::menu::patching
