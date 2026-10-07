#pragma once

// The effect states the effects specs share: a state with every setting away from its default, and the printers a
// failing comparison uses.

#include "engines/effects/codec.h"
#include "serialization/tree.h"

#include <ostream>

namespace deluge::effects::testing {

/// The context of a song at input tick magnitude 1.
inline constexpr engine::CodecContext kAtOne{.input_tick_magnitude = 1};

/// Every effect setting away from its default.
inline EffectStates busy() {
	EffectStates s;
	s.filter = {.lpf_mode = FilterMode::SVF_NOTCH, .hpf_mode = FilterMode::SVF_BAND, .route = FilterRoute::PARALLEL};
	s.saturation.clipping_amount = 3;
	s.mod_fx.type = ModFXType::GRAIN;
	s.delay = {.ping_pong = false,
	           .analog = true,
	           .sync_type = engine::SyncType::TRIPLET,
	           .sync_level = engine::SyncLevel::EIGHTH};
	s.stutter = {.use_song_stutter = true, .quantized = false, .reversed = true, .ping_pong = true};
	s.compressor = {.attack = 1 << 24,
	                .release = 2 << 24,
	                .threshold = 3 << 24,
	                .ratio = 4 << 24,
	                .sidechain_hpf = 5 << 24,
	                .blend = 6 << 24};
	s.sidechain = {
	    .attack = 123, .release = 456, .sync_type = engine::SyncType::DOTTED, .sync_level = engine::SyncLevel::QUARTER};
	return s;
}

} // namespace deluge::effects::testing

namespace deluge::effects {
/// States, printed as their encoding, so a failing comparison shows them.
inline std::ostream& operator<<(std::ostream& out, const EffectStates& states) {
	return out << serialization::first_difference(encode(states, testing::kAtOne), serialization::Value::makeMap());
}

/// A delay's state, printed field by field.
inline std::ostream& operator<<(std::ostream& out, const DelayState& delay) {
	return out << "{pingPong " << delay.ping_pong << " analog " << delay.analog << " syncType "
	           << static_cast<int>(delay.sync_type) << " syncLevel " << static_cast<int>(delay.sync_level) << "}";
}
} // namespace deluge::effects
