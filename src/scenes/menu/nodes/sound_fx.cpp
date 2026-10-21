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
#include "dsp/reverb/reverb.hpp"
#include "engine-api/types/sync.h"
#include "engines/effects/state.h"
#include "engines/effects/words.h"
#include "model/edits/setting_edit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "modulation/sync.h"
#include "processing/engines/audio_engine.h"
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
///        stutter config has nothing to inherit from, so it does not offer "use song".
bool show_use_song(const MenuContext& ctx) {
	return ctx.mod != nullptr && !ctx.mod->isSong();
}

/// @brief @p v as a numeric effect setting.
deluge::serialization::Value number_value(int32_t v) {
	return deluge::serialization::Value::makeNumber(v);
}

/// @brief @p word as the effect setting's file form, as the legacy menus write it.
template <typename Word>
deluge::serialization::Value word_value(Word word) {
	return deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(word)));
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Reverb -- engine-wide: the context gives only the song (see `sound_fx.h`'s note)
//
// As the legacy items (`gui/menu_item/reverb/`): the song's `project.reverb` is the truth, and each write is a
// SetSetting that projects it onto the engine.

int32_t get_reverb_room_size(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return reverb_float_to_menu(ctx.song->project.reverb.room_size);
}
void set_reverb_room_size(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFromMenu("reverb/roomSize", value);
}

int32_t get_reverb_damping(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return reverb_float_to_menu(ctx.song->project.reverb.damping);
}
void set_reverb_damping(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFromMenu("reverb/dampening", value);
}

int32_t get_reverb_width(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return reverb_float_to_menu(ctx.song->project.reverb.width);
}
void set_reverb_width(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFromMenu("reverb/width", value);
}

int32_t get_reverb_model(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return static_cast<int32_t>(ctx.song->project.reverb.model); // `reverb/model.cpp`
}
void set_reverb_model(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFor("reverb/model", deluge::serialization::Value::makeNumber(value));
}

int32_t get_reverb_pan(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return bindings::pan_to_menu(ctx.song->project.reverb.pan); // `reverb/pan.h`
}
void set_reverb_pan(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFor("reverb/pan", deluge::serialization::Value::makeNumber(bindings::menu_to_pan(value)));
}

int32_t get_reverb_hpf(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return reverb_float_to_menu(ctx.song->project.reverb.hpf); // `reverb/hpf.h`
}
void set_reverb_hpf(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFromMenu("reverb/hpf", value);
}

int32_t get_reverb_lpf(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return reverb_float_to_menu(ctx.song->project.reverb.lpf); // `reverb/lpf.h`
}
void set_reverb_lpf(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFromMenu("reverb/lpf", value);
}

int32_t get_reverb_sidechain_volume(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return ctx.song->project.reverb.sidechain_volume / 21474836; // `reverb/sidechain/volume.h`
}
void set_reverb_sidechain_volume(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFor("reverb/compressor/volume",
	                              deluge::serialization::Value::makeNumber(value * 21474836));
}

int32_t get_reverb_sidechain_shape(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	// `reverb::sidechain::Shape::readCurrentValue` -- the same arithmetic as the standard scaling.
	return bindings::standard_to_menu(ctx.song->project.reverb.sidechain_shape);
}
void set_reverb_sidechain_shape(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	// Deliberately NOT `bindings::menu_to_standard`: legacy's write here is the plain formula with
	// no endpoint special cases, so at 0 and 50 the two differ by one LSB. Transcribed as written.
	const int32_t v = static_cast<int32_t>(static_cast<uint32_t>(value) * 85899345 - 2147483648u);
	deluge::edits::songSettingFor("reverb/compressor/shape", deluge::serialization::Value::makeNumber(v));
}

// The reverb's sidechain is the engine's own (`sidechain::Attack` and its siblings with
// `is_reverb_sidechain_`), with no affect-entire loop: there is one reverb sidechain, not one per drum.

int32_t get_reverb_sidechain_attack(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return sidechain_attack_to_menu(ctx.song->project.reverb.sidechain_attack);
}
void set_reverb_sidechain_attack(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFor("reverb/compressor/attack",
	                              deluge::serialization::Value::makeNumber(sidechain_attack_from_menu(value)));
}

int32_t get_reverb_sidechain_release(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	return sidechain_release_to_menu(ctx.song->project.reverb.sidechain_release);
}
void set_reverb_sidechain_release(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	deluge::edits::songSettingFor("reverb/compressor/release",
	                              deluge::serialization::Value::makeNumber(sidechain_release_from_menu(value)));
}

int32_t get_reverb_sidechain_sync(const MenuContext& ctx) {
	if (ctx.song == nullptr) {
		return 0;
	}
	// The sync type is runtime-only for the reverb; the level is the song's.
	return sidechain_sync_to_menu(AudioEngine::reverbSidechainSettings.sync_type,
	                              ctx.song->project.reverb.sidechain_sync);
}
void set_reverb_sidechain_sync(const MenuContext& ctx, int32_t value) {
	if (ctx.song == nullptr) {
		return;
	}
	// The sync type is the engine's alone; the level is the song's record.
	AudioEngine::reverbSidechainSettings.sync_type = syncValueToSyncType(value);
	deluge::edits::songSettingFor(
	    "reverb/compressor/syncLevel",
	    deluge::serialization::Value::makeNumber(deluge::engine::index_of(syncValueToSyncLevel(value))));
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
	if (ctx.mod == nullptr) {
		return 0;
	}
	const auto& sc = ctx.mod->effects().stutter;
	StutterOption option = kForward;
	if (show_use_song(ctx) && sc.use_song_stutter) {
		option = kUseSong;
	}
	else if (sc.reversed && sc.ping_pong) {
		option = kReversedPingPong;
	}
	else if (sc.reversed) {
		option = kReversed;
	}
	else if (sc.ping_pong) {
		option = kForwardPingPong;
	}
	return static_cast<int32_t>(option) - (show_use_song(ctx) ? 0 : 1);
}

void set_stutter_direction(const MenuContext& ctx, int32_t value) {
	// The private `getValue()` shift, then `applyOptionToStutter` over the affect-entire sweep
	// (`stutter/direction.h`).
	const int32_t option = value + (show_use_song(ctx) ? 0 : 1);
	bindings::apply_to_targets(
	    ctx, option, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext& ctx) {
		    if (t.mod == nullptr) {
			    return;
		    }
		    deluge::edits::effectSettingFor(*t.mod, "stutter/useSongStutter", number_value(v == kUseSong ? 1 : 0));
		    deluge::edits::effectSettingFor(*t.mod, "stutter/reverse",
		                                    number_value(v == kReversed || v == kReversedPingPong ? 1 : 0));
		    deluge::edits::effectSettingFor(*t.mod, "stutter/pingPong",
		                                    number_value(v == kForwardPingPong || v == kReversedPingPong ? 1 : 0));
		    if (t.mod->effects().stutter.use_song_stutter && ctx.song != nullptr) {
			    // Inherit the song's settings wholesale.
			    const auto& song_sc = ctx.song->live.global_effectable.effects().stutter;
			    deluge::edits::effectSettingFor(*t.mod, "stutter/quantized", number_value(song_sc.quantized ? 1 : 0));
			    deluge::edits::effectSettingFor(*t.mod, "stutter/reverse", number_value(song_sc.reversed ? 1 : 0));
			    deluge::edits::effectSettingFor(*t.mod, "stutter/pingPong", number_value(song_sc.ping_pong ? 1 : 0));
		    }
	    });
}

int32_t get_stutter_quantize(const MenuContext& ctx) {
	// `stutter/quantized.h`'s `readCurrentValue`
	return (ctx.mod != nullptr && ctx.mod->effects().stutter.quantized) ? 1 : 0;
}
void set_stutter_quantize(const MenuContext& ctx, int32_t value) {
	// `QuantizedStutter::writeCurrentValue` (`stutter/quantized.h`). The affect-entire sweep tests
	// the *context's* `use_song_stutter`, not each drum's, as legacy does.
	const bool blocked_by_song =
	    ctx.mod != nullptr && ctx.mod->effects().stutter.use_song_stutter && bindings::writes_whole_kit(ctx);
	if (blocked_by_song) {
		return;
	}
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    if (t.mod != nullptr) {
			    deluge::edits::effectSettingFor(*t.mod, "stutter/quantized", number_value(v != 0 ? 1 : 0));
		    }
	    });
}

namespace {
constexpr const Node* kRoots[] = {&sound_fx};
} // namespace

const std::span<const Node* const> kSoundFxChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
