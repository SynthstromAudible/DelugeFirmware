#pragma once

// The instrument codec the model tests give the model: the Deluge instrument's, as the app's is.

#include "deluge-project/instrument_slot.h"
#include "engines/deluge/engine.h"

namespace deluge::project::test {

namespace detail {

inline engine::StateHandle decode_instrument(const serialization::Value& element, int32_t tick_magnitude) {
	instrument::SoundState state = instrument::new_synth_state();
	(void)instrument::decode(element, state, {.input_tick_magnitude = tick_magnitude});
	return engine::StateHandle::of<instrument::DelugeInstrument>(std::move(state));
}

inline void encode_instrument(const engine::StateHandle& state, serialization::Value& element, int32_t tick_magnitude) {
	for (const auto& [key, value] : state.encode({.input_tick_magnitude = tick_magnitude}).entries()) {
		element.set(key, value);
	}
}

} // namespace detail

/// @brief The instrument codec built on the Deluge instrument.
/// @return An InstrumentCodec whose decode and encode go through the instrument's codec.
inline InstrumentCodec test_instrument_codec() {
	return InstrumentCodec{.decode = &detail::decode_instrument, .encode = &detail::encode_instrument};
}

} // namespace deluge::project::test
