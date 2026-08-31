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
#include "model/mod_controllable/ModFXProcessor.h" // modfx::getModNames
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "model/sync.h"
#include "processing/engines/audio_engine.h"

#include <cmath>

namespace deluge::scenes::menu::nodes {

using deluge::l10n::String;

namespace {

/// @brief The four float-backed reverb settings share one shape: `round(x * kMaxMenuValue)` on the
///        way out, `value / kMaxMenuValue` on the way back (`reverb/room_size.h:28 readCurrentValue` and
///        siblings).
int32_t reverb_float_to_menu(float value) {
	return static_cast<int32_t>(std::lround(value * kMaxMenuValue));
}
float menu_to_reverb_float(int32_t value) {
	return static_cast<float>(value) / kMaxMenuValue;
}

/// @brief `stutter::StutterDirection`'s own enumerators (`stutter/direction.h:29`).
enum StutterOption : uint8_t { kUseSong = 0, kForward, kReversed, kForwardPingPong, kReversedPingPong };

/// @brief `StutterDirection::showUseSongOption()` (`stutter/direction.h:105`): the song's own
///        stutter config has nothing to inherit from, so it does not offer "use song".
bool show_use_song(const MenuContext& ctx) {
	return ctx.mod != nullptr && !ctx.mod->isSong();
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Delay

int32_t get_delay_ping_pong(const MenuContext& ctx) {
	return (ctx.mod != nullptr && ctx.mod->delay.pingPong) ? 1 : 0; // `delay/ping_pong.h:31 readCurrentValue`
}
void set_delay_ping_pong(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           t.mod->delay.pingPong = v != 0; // `ping_pong.h:36`, `:42`
		                           }
	                           });
}

int32_t get_delay_sync(const MenuContext& ctx) {
	if (ctx.mod == nullptr) {
		return 0;
	}
	// `delay::Sync::readCurrentValue` (`delay/sync.h:31-34`).
	const auto& delay = ctx.mod->delay;
	return static_cast<int32_t>(delay.syncType)
	       + (static_cast<int32_t>(delay.syncLevel) - (delay.syncType != SYNC_TYPE_EVEN ? 1 : 0));
}
void set_delay_sync(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           t.mod->delay.syncType = syncValueToSyncType(v); // `sync.h:38-39`
			                           t.mod->delay.syncLevel = syncValueToSyncLevel(v);
		                           }
	                           });
}

int32_t get_delay_type(const MenuContext& ctx) {
	return (ctx.mod != nullptr && ctx.mod->delay.analog) ? 1 : 0; // `delay/analog.h:30 readCurrentValue`
}
void set_delay_type(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           t.mod->delay.analog = v != 0; // `analog.h:32 writeCurrentValue`, `:40`
		                           }
	                           });
}

// ---------------------------------------------------------------------------------------------
// Reverb -- engine-wide, so none of these reads the context (see `sound_fx.h`'s note)

int32_t get_reverb_room_size(const MenuContext& /*ctx*/) {
	return reverb_float_to_menu(AudioEngine::reverb.getRoomSize()); // `reverb/room_size.h:28 readCurrentValue`
}
void set_reverb_room_size(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setRoomSize(menu_to_reverb_float(value)); // `:23`
}

int32_t get_reverb_damping(const MenuContext& /*ctx*/) {
	return reverb_float_to_menu(AudioEngine::reverb.getDamping()); // `reverb/damping.h:27 readCurrentValue`
}
void set_reverb_damping(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setDamping(menu_to_reverb_float(value));
}

int32_t get_reverb_width(const MenuContext& /*ctx*/) {
	return reverb_float_to_menu(AudioEngine::reverb.getWidth()); // `reverb/width.h:28 readCurrentValue`
}
void set_reverb_width(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setWidth(menu_to_reverb_float(value));
}

int32_t get_reverb_model(const MenuContext& /*ctx*/) {
	return static_cast<int32_t>(AudioEngine::reverb.getModel()); // `reverb/model.h:13`
}
void set_reverb_model(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setModel(static_cast<deluge::dsp::Reverb::Model>(value)); // `:14-16`
}

int32_t get_reverb_pan(const MenuContext& /*ctx*/) {
	return bindings::pan_to_menu(AudioEngine::reverbPan); // `reverb/pan.h:32`
}
void set_reverb_pan(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverbPan = bindings::menu_to_pan(value); // `:30`
}

int32_t get_reverb_hpf(const MenuContext& /*ctx*/) {
	return reverb_float_to_menu(AudioEngine::reverb.getHPF()); // `reverb/hpf.h:30 readCurrentValue`
}
void set_reverb_hpf(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setHPF(menu_to_reverb_float(value));
}

int32_t get_reverb_lpf(const MenuContext& /*ctx*/) {
	return reverb_float_to_menu(AudioEngine::reverb.getLPF()); // `reverb/lpf.h:29 readCurrentValue`
}
void set_reverb_lpf(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverb.setLPF(menu_to_reverb_float(value));
}

int32_t get_reverb_sidechain_volume(const MenuContext& /*ctx*/) {
	return AudioEngine::reverbSidechainVolume / 21474836; // `reverb/sidechain/volume.h:27 readCurrentValue`
}
void set_reverb_sidechain_volume(const MenuContext& /*ctx*/, int32_t value) {
	AudioEngine::reverbSidechainVolume = value * 21474836; // `:24-25`
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

int32_t get_reverb_sidechain_shape(const MenuContext& /*ctx*/) {
	// `reverb::sidechain::Shape::readCurrentValue` (`reverb/sidechain/shape.h:28-30`) -- the same
	// arithmetic as the standard scaling.
	return bindings::standard_to_menu(AudioEngine::reverbSidechainShape);
}
void set_reverb_sidechain_shape(const MenuContext& /*ctx*/, int32_t value) {
	// `:26-29`. Deliberately NOT `bindings::menu_to_standard`: legacy's write here is the plain
	// formula with no endpoint special cases, so at 0 and 50 the two differ by one LSB. Transcribed
	// as written.
	AudioEngine::reverbSidechainShape = static_cast<uint32_t>(value) * 85899345 - 2147483648;
	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
}

int32_t get_reverb_sidechain_attack(const MenuContext& /*ctx*/) {
	return sidechain_attack_to_menu(&AudioEngine::reverbSidechain); // `sidechain/utils.cpp:6-11`
}
void set_reverb_sidechain_attack(const MenuContext& /*ctx*/, int32_t value) {
	// `sidechain::Attack::writeCurrentValue`'s `is_reverb_sidechain_` arm (`sidechain/attack.h:38-56`)
	// skips the affect-entire kit loop entirely -- there is one reverb sidechain, not one per drum.
	sidechain_attack_from_menu(&AudioEngine::reverbSidechain, value);
}

int32_t get_reverb_sidechain_release(const MenuContext& /*ctx*/) {
	return sidechain_release_to_menu(&AudioEngine::reverbSidechain);
}
void set_reverb_sidechain_release(const MenuContext& /*ctx*/, int32_t value) {
	sidechain_release_from_menu(&AudioEngine::reverbSidechain, value);
}

int32_t get_reverb_sidechain_sync(const MenuContext& /*ctx*/) {
	return sidechain_sync_to_menu(&AudioEngine::reverbSidechain);
}
void set_reverb_sidechain_sync(const MenuContext& /*ctx*/, int32_t value) {
	sidechain_sync_from_menu(&AudioEngine::reverbSidechain, value);
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
	// `StutterDirection::readCurrentValue` (`stutter/direction.h:42-59`) and its private `setValue`
	// shift (`:92-95`): the option index drops by one when "use song" is not offered.
	if (ctx.mod == nullptr) {
		return 0;
	}
	const StutterConfig& sc = ctx.mod->stutterConfig;
	StutterOption option = kForward;
	if (show_use_song(ctx) && sc.useSongStutter) {
		option = kUseSong;
	}
	else if (sc.reversed && sc.pingPong) {
		option = kReversedPingPong;
	}
	else if (sc.reversed) {
		option = kReversed;
	}
	else if (sc.pingPong) {
		option = kForwardPingPong;
	}
	return static_cast<int32_t>(option) - (show_use_song(ctx) ? 0 : 1);
}

void set_stutter_direction(const MenuContext& ctx, int32_t value) {
	// The private `getValue()` shift (`stutter/direction.h:86-90`), then
	// `applyOptionToStutterConfig` (`:101-112`) over the affect-entire sweep (`:63-83`).
	const int32_t option = value + (show_use_song(ctx) ? 0 : 1);
	bindings::apply_to_targets(ctx, option, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext& ctx) {
		                           if (t.mod == nullptr) {
			                           return;
		                           }
		                           StutterConfig& sc = t.mod->stutterConfig;
		                           sc.useSongStutter = v == kUseSong;
		                           sc.reversed = v == kReversed || v == kReversedPingPong;
		                           sc.pingPong = v == kForwardPingPong || v == kReversedPingPong;
		                           if (sc.useSongStutter && ctx.song != nullptr) {
			                           // `:107-111` -- inherit the song's settings wholesale.
			                           const StutterConfig& song_sc = ctx.song->globalEffectable.stutterConfig;
			                           sc.quantized = song_sc.quantized;
			                           sc.reversed = song_sc.reversed;
			                           sc.pingPong = song_sc.pingPong;
		                           }
	                           });
}

int32_t get_stutter_quantize(const MenuContext& ctx) {
	// `stutter/quantized.h:32 readCurrentValue`
	return (ctx.mod != nullptr && ctx.mod->stutterConfig.quantized) ? 1 : 0;
}
void set_stutter_quantize(const MenuContext& ctx, int32_t value) {
	// `QuantizedStutter::writeCurrentValue` (`stutter/quantized.h:24-42`). The affect-entire sweep
	// tests the *context's* `useSongStutter`, not each drum's -- legacy's own wording
	// ("the same filtering as stated in the isRelevant() function", `:35`) reads
	// `soundEditor.currentModControllable` inside the loop, so this reproduces that exactly.
	const bool blocked_by_song =
	    ctx.mod != nullptr && ctx.mod->stutterConfig.useSongStutter && bindings::writes_whole_kit(ctx);
	if (blocked_by_song) {
		return;
	}
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           t.mod->stutterConfig.quantized = v != 0;
		                           }
	                           });
}

// ---------------------------------------------------------------------------------------------
// Mod FX

size_t mod_fx_type_names(const MenuContext& /*ctx*/, std::span<std::string_view> out) {
	// `mod_fx::Type::getOptions` (`mod_fx/type.h:64-67`) -> `modfx::getModNames()`
	// (`model/global_effectable/global_effectable.cpp:1152`). The strings are l10n views, so the
	// spans they hand back outlive this call.
	const auto names = modfx::getModNames();
	size_t n = 0;
	for (std::string_view name : names) {
		if (n < out.size()) {
			out[n++] = name;
		}
	}
	return n;
}

int32_t get_mod_fx_type(const MenuContext& ctx) {
	return ctx.mod != nullptr ? static_cast<int32_t>(ctx.mod->modFXType_) : 0; // `mod_fx/type.h:34 readCurrentValue`
}
void set_mod_fx_type(const MenuContext& ctx, int32_t value) {
	// `Type::writeCurrentValue` (`mod_fx/type.h:24-56`). Legacy raises `Error::INSUFFICIENT_RAM`
	// through `display->displayError` when `setModFXType` fails (`:41-43`, `:51-53`); `display->` is
	// behind the `gui/`+`hid/` wall, and the notification seam that replaces it is `notify::`, which
	// spec E gives to the scene rather than the table. Recorded as the one thing that does not come
	// across -- the allocation failure itself is still handled, by `setModFXType` returning false and
	// leaving the type alone.
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           (void)t.mod->setModFXType(static_cast<ModFXType>(v));
		                           }
	                           });
}

// ---------------------------------------------------------------------------------------------
// Distortion

int32_t get_saturation(const MenuContext& ctx) {
	return ctx.mod != nullptr ? ctx.mod->clippingAmount : 0; // `fx/clipping.h:33 readCurrentValue`
}
void set_saturation(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           if (t.mod != nullptr) {
			                           // `clipping.h:46 writeCurrentValue`, `:52`
			                           t.mod->clippingAmount = static_cast<uint8_t>(v);
		                           }
	                           });
}

namespace {
constexpr const Node* kRoots[] = {&sound_fx};
} // namespace

const std::span<const Node* const> kSoundFxChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
