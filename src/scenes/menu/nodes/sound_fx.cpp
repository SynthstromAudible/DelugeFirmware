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
/// @brief The FX pages' `get`/`set` hooks and the exported root span. The `Node` literals are in
///        `sound_fx.h`.

#include "scenes/menu/nodes/sound_fx.h"

#include "ui/oled/column_drawings.h"

#include <algorithm>

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "effects/state.h"
#include "effects/words.h"
#include "gfx/oled/icons.h"
#include "instruments/deluge/state_access.h"
#include "plugin/types/display_mapping.h"
#include "plugin/types/sync.h"
#include "project/song.h"
#include "scenes/bind/ports/write_sink.h"
#include "scenes/menu/nodes/sound_sidechain.h"
#include "serialization/value.h"

#include <array>
#include <cmath>
#include <string>

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

namespace {

/// @brief A float-backed reverb setting as its menu shows it: `round(x * kMaxMenuValue)`
///        (`origin/main:src/deluge/gui/menu_item/reverb/room_size.h` readCurrentValue and siblings). Writes go through
///        the row's user scale.
int32_t reverb_float_to_menu(float value) {
	return static_cast<int32_t>(std::lround(value * plugin::kMaxMenuSteps));
}

/// @brief `stutter::StutterDirection`'s own enumerators.
enum StutterOption : uint8_t { kUseSong = 0, kForward, kReversed, kForwardPingPong, kReversedPingPong };

/// @brief `StutterDirection::showUseSongOption()` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`): the
///        song's own stutter config has nothing to inherit from, so it does not offer "use song". Everything else the
///        menu edits a stutter of -- a sound, a kit's or an audio track's chain -- is not the song's.
bool show_use_song(const MenuContext& ctx) {
	return ctx.addresses.slot.has_value()
	       || (ctx.addresses.chain && ctx.addresses.chain_owner != bind::ChainOwner::SONG);
}

/// @brief The stutter the context edits, as its record holds it: its sound's, else its chain's.
std::optional<deluge::effects::StutterState> stutter_of(const MenuContext& ctx) {
	if (ctx.addresses.slot) {
		const deluge::instrument::SoundState* state =
		    ctx.ports.sound != nullptr ? ctx.ports.sound->sound_state(*ctx.addresses.slot) : nullptr;
		return state != nullptr ? std::optional{deluge::instrument::sound_effects(*state).stutter} : std::nullopt;
	}
	if (ctx.addresses.chain && ctx.ports.tracks != nullptr) {
		const deluge::effects::EffectStates* effects = ctx.ports.tracks->chain_effects(*ctx.addresses.chain);
		return effects != nullptr ? std::optional{effects->stutter} : std::nullopt;
	}
	return std::nullopt;
}

/// @brief Write the stutter row @p path to @p on through the sink, over the whole kit under affect-entire.
void write_stutter(const MenuContext& ctx, std::string_view path, bool on) {
	bind::Addresses where = ctx.addresses;
	where.affect_entire = bindings::writes_whole_kit(ctx);
	(void)ctx.ports.writes->write({.target = {.kind = bind::Target::Kind::EFFECT, .path = path, .whole_kit = true},
	                               .where = where,
	                               .menu_value = on ? 1 : 0});
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Reverb -- engine-wide: the context gives only the song (see `sound_fx.h`'s note)
//
// As the legacy items (`gui/menu_item/reverb/`): the song's `project.reverb` is the truth, and each write is a
// SetSetting that projects it onto the engine.

int32_t get_reverb_sidechain_volume(const MenuContext& ctx) {
	if (ctx.model == nullptr) {
		return 0;
	}
	return ctx.model->reverb.sidechain_volume
	       / 21474836; // `origin/main:src/deluge/gui/menu_item/reverb/sidechain/volume.h`
}
void set_reverb_sidechain_volume(const MenuContext& ctx, int32_t value) {
	bindings::write_song(ctx, "reverb/compressor/volume", value * 21474836);
}

// The reverb's sidechain is the engine's own (`sidechain::Attack` and its siblings with
// `is_reverb_sidechain_`), with no affect-entire loop: there is one reverb sidechain, not one per drum.

int32_t get_reverb_sidechain_sync(const MenuContext& ctx) {
	if (ctx.model == nullptr || ctx.ports.audio == nullptr) {
		return 0;
	}
	// The sync type is runtime-only for the reverb; the level is the song's.
	return deluge::plugin::combined_sync_menu(
	    deluge::plugin::index_of(ctx.model->reverb.sidechain_sync),
	    deluge::plugin::sync_type_choice(ctx.ports.audio->reverb_sidechain_sync_type()));
}
void set_reverb_sidechain_sync(const MenuContext& ctx, int32_t value) {
	if (ctx.ports.writes == nullptr) {
		return;
	}
	// The sync type is the engine's alone; the level is the song's record.
	const auto [level, type_choice] = deluge::plugin::combined_sync_stored(value);
	const deluge::plugin::SyncType type = deluge::plugin::kSyncTypeChoices[static_cast<size_t>(type_choice)];
	ctx.ports.writes->set_reverb_sidechain_sync_type(type);
	bindings::write_song(ctx, "reverb/compressor/syncLevel", level);
}

// ---------------------------------------------------------------------------------------------
// Stutter

size_t stutter_direction_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `StutterDirection::getOptions` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`), FULL spelling.
	size_t n = 0;
	const auto emit = [&](String s) {
		if (n < out.size()) {
			out[n++] = deluge::scenes::text_lookup(sid(s));
		}
	};
	if (show_use_song(ctx)) {
		emit(String::STRING_FOR_USE_SONG);
	}
	emit(String::STRING_FOR_FORWARD);
	emit(String::STRING_FOR_REVERSED);
	emit(String::STRING_FOR_FORWARD_PING_PONG);
	emit(String::STRING_FOR_REVERSED_PING_PONG);
	return n;
}

int32_t get_stutter_direction(const MenuContext& ctx) {
	// `StutterDirection::readCurrentValue` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`) and its private
	// `setValue` shift: the option index drops by one when "use song" is not offered.
	const std::optional<deluge::effects::StutterState> sc = stutter_of(ctx);
	if (!sc) {
		return 0;
	}
	StutterOption option = kForward;
	if (show_use_song(ctx) && sc->use_song_stutter) {
		option = kUseSong;
	}
	else if (sc->reversed && sc->ping_pong) {
		option = kReversedPingPong;
	}
	else if (sc->reversed) {
		option = kReversed;
	}
	else if (sc->ping_pong) {
		option = kForwardPingPong;
	}
	return static_cast<int32_t>(option) - (show_use_song(ctx) ? 0 : 1);
}

void set_stutter_direction(const MenuContext& ctx, int32_t value) {
	// The private `getValue()` shift, then `applyOptionToStutter` over the affect-entire sweep
	// (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`).
	if (ctx.ports.writes == nullptr) {
		return;
	}
	const int32_t v = value + (show_use_song(ctx) ? 0 : 1);
	write_stutter(ctx, "stutter/useSongStutter", v == kUseSong);
	write_stutter(ctx, "stutter/reverse", v == kReversed || v == kReversedPingPong);
	write_stutter(ctx, "stutter/pingPong", v == kForwardPingPong || v == kReversedPingPong);
	if (v == kUseSong && ctx.ports.tracks != nullptr) {
		// Inherit the song's settings wholesale.
		const deluge::effects::EffectStates* song = ctx.ports.tracks->chain_effects(
		    deluge::project::ChainAddress{.kind = deluge::project::ChainAddress::Kind::SONG});
		if (song != nullptr) {
			write_stutter(ctx, "stutter/quantized", song->stutter.quantized);
			write_stutter(ctx, "stutter/reverse", song->stutter.reversed);
			write_stutter(ctx, "stutter/pingPong", song->stutter.ping_pong);
		}
	}
}

bool stutter_rate_quantized(const MenuContext& ctx) {
	// `stutter::Rate::isStutterQuantized` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`): the song's setting
	// when the sound uses the song's stutter.
	const std::optional<deluge::effects::StutterState> sc = stutter_of(ctx);
	if (!sc) {
		return false;
	}
	if (sc->use_song_stutter && ctx.ports.tracks != nullptr) {
		const deluge::effects::EffectStates* song = ctx.ports.tracks->chain_effects(
		    deluge::project::ChainAddress{.kind = deluge::project::ChainAddress::Kind::SONG});
		return song != nullptr && song->stutter.quantized;
	}
	return sc->quantized;
}

namespace {

/// `stutter::Rate::optionValues` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`).
constexpr std::array<int32_t, 5> kRateValues{0, 12, 25, 37, 50};

/// `stutter::Rate::getClosestQuantizedOptionIndex` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`): the first
/// of equally near options.
size_t nearest_rate_option(int32_t value) {
	size_t nearest = 0;
	for (size_t i = 1; i < kRateValues.size(); i++) {
		if (std::abs(value - kRateValues[i]) < std::abs(value - kRateValues[nearest])) {
			nearest = i;
		}
	}
	return nearest;
}

ValueText put_view(std::string_view text, std::span<char> out) {
	const size_t n = std::min(text.size(), out.size());
	std::copy_n(text.data(), n, out.data());
	return ValueText{.length = n};
}

} // namespace

ValueText stutter_rate_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out) {
	// `stutter::Rate::renderInHorizontalMenu`, `getNotificationValue` and `drawPixelsForOled`
	// (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`): quantized, the label of the nearest option value.
	if ((use != ValueTextUse::Column && use != ValueTextUse::Notification) || !stutter_rate_quantized(ctx)) {
		return kValueTextFallback;
	}
	static constexpr std::array<std::string_view, kRateValues.size()> kLabels{"4ths", "8ths", "16ths", "32nds",
	                                                                          "64ths"};
	return put_view(kLabels[nearest_rate_option(value)], out);
}

std::optional<int32_t> stutter_rate_turn(const MenuContext& ctx, int32_t value, int32_t offset) {
	// `stutter::Rate::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/stutter/rate.cpp`).
	if (!stutter_rate_quantized(ctx)) {
		return std::nullopt;
	}
	const auto idx = static_cast<int32_t>(nearest_rate_option(value));
	return kRateValues[std::clamp<int32_t>(idx + offset, 0, static_cast<int32_t>(kRateValues.size()) - 1)];
}

std::optional<deluge::ui::oled::ArtColumn> stutter_direction_art(const MenuContext& ctx, int32_t value) {
	// `stutter::StutterDirection::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`)
	// over its shifted `getValue()`.
	const int32_t v = value + (show_use_song(ctx) ? 0 : 1);
	if (v == kUseSong) {
		return deluge::ui::oled::ArtColumn{.text = "song"};
	}
	return deluge::ui::oled::ArtColumn{.icon = &deluge::gfx::oled::icons::direction,
	                                   .reversed = v == kReversed || v == kReversedPingPong,
	                                   .dots = v == kForwardPingPong || v == kReversedPingPong};
}

ValueText stutter_direction_text(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out) {
	// `stutter::StutterDirection::getNotificationValue` (`origin/main:src/deluge/gui/menu_item/stutter/direction.h`):
	// the SHORT option at the list index.
	if (use != ValueTextUse::Notification) {
		return kValueTextFallback;
	}
	if (show_use_song(ctx) && value == 0) {
		return put_view(deluge::scenes::text_lookup(sid(String::STRING_FOR_USE_SONG_SHORT)), out);
	}
	std::string_view names[5];
	const size_t n = stutter_direction_names(ctx, names);
	return value >= 0 && static_cast<size_t>(value) < n ? put_view(names[value], out) : kValueTextFallback;
}

int32_t get_stutter_quantize(const MenuContext& ctx) {
	// `origin/main:src/deluge/gui/menu_item/stutter/quantized.h`'s `readCurrentValue`
	const std::optional<deluge::effects::StutterState> sc = stutter_of(ctx);
	return sc && sc->quantized ? 1 : 0;
}
void set_stutter_quantize(const MenuContext& ctx, int32_t value) {
	// `QuantizedStutter::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/stutter/quantized.h`). The
	// affect-entire sweep tests the *context's* `use_song_stutter`, not each drum's, as legacy does.
	if (ctx.ports.writes == nullptr) {
		return;
	}
	const std::optional<deluge::effects::StutterState> sc = stutter_of(ctx);
	if (sc && sc->use_song_stutter && bindings::writes_whole_kit(ctx)) {
		return;
	}
	write_stutter(ctx, "stutter/quantized", value != 0);
}

namespace {
constexpr const Node* kRoots[] = {&sound_fx};
} // namespace

const std::span<const Node* const> kSoundFxChildren{kRoots};

void eq_page_draw(ui::oled::OledFrame& panel, const MenuContext& ctx, int32_t focused) {
	// `EqMenu::ensureCorrectItemsOrderAndGetValues` (`origin/main:src/deluge/gui/menu_item/eq/eq_menu.h`): treble
	// frequency counts up to 32 only.
	ui::oled::eq_graph(panel, static_cast<float>(bindings::read(eq_bass, ctx)) / 50.f,
	                   static_cast<float>(bindings::read(eq_treble, ctx)) / 50.f,
	                   static_cast<float>(bindings::read(eq_bass_freq, ctx)) / 50.f,
	                   static_cast<float>(std::clamp<int32_t>(bindings::read(eq_treble_freq, ctx), 0, 32)) / 32.f,
	                   focused);
}

} // namespace deluge::scenes::menu::nodes
