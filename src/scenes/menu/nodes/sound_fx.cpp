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

#include "scenes/l10n_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "definitions_cxx.hpp"
#include "deluge-project/song.h"
#include "dsp/reverb/reverb.hpp"
#include "engine-api/types/sync.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/state.h"
#include "engines/effects/words.h"
#include "model/song/song.h"
#include "modulation/sync.h"
#include "processing/engines/audio_engine.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/write_sink.h"
#include "scenes/menu/nodes/sound_sidechain.h"
#include "serialization/value.h"

#include <cmath>
#include <string>

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

namespace {

/// @brief A float-backed reverb setting as its menu shows it: `round(x * kMaxMenuValue)`
///        (`reverb/room_size.h` readCurrentValue and siblings). Writes go through the row's user scale.
int32_t reverb_float_to_menu(float value) {
	return static_cast<int32_t>(std::lround(value * kMaxMenuValue));
}

/// @brief `stutter::StutterDirection`'s own enumerators (`stutter/direction.h:29`).
enum StutterOption : uint8_t { kUseSong = 0, kForward, kReversed, kForwardPingPong, kReversedPingPong };

/// @brief `StutterDirection::showUseSongOption()` (`stutter/direction.h:105`): the song's own
///        stutter config has nothing to inherit from, so it does not offer "use song". Everything else the
///        menu edits a stutter of -- a sound, a kit's or an audio track's chain -- is not the song's.
bool show_use_song(const MenuContext& ctx) {
	return ctx.addresses.slot.has_value()
	       || (ctx.addresses.chain && ctx.addresses.chain_owner != bind::ChainOwner::SONG);
}

/// @brief The stutter the context edits, as its record holds it: its sound's, else its chain's.
std::optional<deluge::effects::StutterState> stutter_of(const MenuContext& ctx) {
	if (ctx.facts == nullptr) {
		return std::nullopt;
	}
	if (ctx.addresses.slot) {
		const deluge::instrument::SoundState* state = ctx.facts->sound_state(*ctx.addresses.slot);
		return state != nullptr ? std::optional{state->effects.stutter} : std::nullopt;
	}
	if (ctx.addresses.chain) {
		const deluge::effects::EffectStates* effects = ctx.facts->chain_effects(*ctx.addresses.chain);
		return effects != nullptr ? std::optional{effects->stutter} : std::nullopt;
	}
	return std::nullopt;
}

/// @brief Write the stutter row @p path to @p on through the sink, over the whole kit under affect-entire.
void write_stutter(const MenuContext& ctx, std::string_view path, bool on) {
	bind::Addresses where = ctx.addresses;
	where.affect_entire = bindings::writes_whole_kit(ctx);
	(void)ctx.sink->write({.target = {.kind = bind::Target::Kind::EFFECT, .path = path, .whole_kit = true},
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
	return ctx.model->reverb.sidechain_volume / 21474836; // `reverb/sidechain/volume.h`
}
void set_reverb_sidechain_volume(const MenuContext& ctx, int32_t value) {
	bindings::write_song(ctx, "reverb/compressor/volume", value * 21474836);
}

// The reverb's sidechain is the engine's own (`sidechain::Attack` and its siblings with
// `is_reverb_sidechain_`), with no affect-entire loop: there is one reverb sidechain, not one per drum.

int32_t get_reverb_sidechain_sync(const MenuContext& ctx) {
	if (ctx.model == nullptr) {
		return 0;
	}
	// The sync type is runtime-only for the reverb; the level is the song's.
	return sidechain_sync_to_menu(AudioEngine::reverbSidechainSettings.sync_type, ctx.model->reverb.sidechain_sync);
}
void set_reverb_sidechain_sync(const MenuContext& ctx, int32_t value) {
	if (ctx.sink == nullptr) {
		return;
	}
	// The sync type is the engine's alone; the level is the song's record.
	AudioEngine::reverbSidechainSettings.sync_type = syncValueToSyncType(value);
	bindings::write_song(ctx, "reverb/compressor/syncLevel",
	                     static_cast<int32_t>(deluge::engine::index_of(syncValueToSyncLevel(value))));
}

// ---------------------------------------------------------------------------------------------
// Stutter

size_t stutter_direction_names(const MenuContext& ctx, std::span<std::string_view> out) {
	// `StutterDirection::getOptions` (`stutter/direction.h:22-40`), FULL spelling.
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
	// `StutterDirection::readCurrentValue` (`stutter/direction.h`) and its private `setValue`
	// shift: the option index drops by one when "use song" is not offered.
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
	// (`stutter/direction.h`).
	if (ctx.sink == nullptr) {
		return;
	}
	const int32_t v = value + (show_use_song(ctx) ? 0 : 1);
	write_stutter(ctx, "stutter/useSongStutter", v == kUseSong);
	write_stutter(ctx, "stutter/reverse", v == kReversed || v == kReversedPingPong);
	write_stutter(ctx, "stutter/pingPong", v == kForwardPingPong || v == kReversedPingPong);
	if (v == kUseSong && ctx.facts != nullptr) {
		// Inherit the song's settings wholesale.
		const deluge::effects::EffectStates* song =
		    ctx.facts->chain_effects(deluge::project::ChainAddress{.kind = deluge::project::ChainAddress::Kind::SONG});
		if (song != nullptr) {
			write_stutter(ctx, "stutter/quantized", song->stutter.quantized);
			write_stutter(ctx, "stutter/reverse", song->stutter.reversed);
			write_stutter(ctx, "stutter/pingPong", song->stutter.ping_pong);
		}
	}
}

int32_t get_stutter_quantize(const MenuContext& ctx) {
	// `stutter/quantized.h`'s `readCurrentValue`
	const std::optional<deluge::effects::StutterState> sc = stutter_of(ctx);
	return sc && sc->quantized ? 1 : 0;
}
void set_stutter_quantize(const MenuContext& ctx, int32_t value) {
	// `QuantizedStutter::writeCurrentValue` (`stutter/quantized.h`). The affect-entire sweep tests
	// the *context's* `use_song_stutter`, not each drum's, as legacy does.
	if (ctx.sink == nullptr) {
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

} // namespace deluge::scenes::menu::nodes
