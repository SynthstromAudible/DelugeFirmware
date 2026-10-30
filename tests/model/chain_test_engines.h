#pragma once

// The chain codec the model tests give the model: the effects library's, as the app's is.

#include "deluge-project/fx_chain.h"
#include "engines/effects/chain.h"
#include "engines/effects/codec.h"

namespace deluge::project::test {

namespace detail {

inline effects::Home home_of(ChainHome home) {
	return home == ChainHome::MASTER ? effects::Home::MASTER : effects::Home::CLIP;
}

inline FxChain decode_chain(const serialization::Value& element, ChainHome home, int32_t tick_magnitude) {
	const plugin::CodecContext context{.input_tick_magnitude = tick_magnitude};
	effects::EffectStates states;
	effects::decode(element, states, context);
	FxChain chain{.reverb_return = effects::reverb_return(home_of(home)), .view = effects::view_entries(element)};
	for (effects::SlotEntries& slot : effects::encode_chain(states, home_of(home), context)) {
		chain.slots.push_back(
		    {.effect = std::string(slot.effect), .state = std::move(slot.entries), .tick_magnitude = tick_magnitude});
	}
	return chain;
}

inline void encode_chain(const FxChain& chain, serialization::Value& element, int32_t tick_magnitude) {
	effects::EffectStates states;
	for (const FxSlot& slot : chain.slots) {
		effects::decode_slot(slot.effect, slot.state, states, {.input_tick_magnitude = slot.tick_magnitude});
	}
	const effects::Home home = chain.reverb_return ? effects::Home::MASTER : effects::Home::CLIP;
	effects::write_chain(effects::encode_chain(states, home, {.input_tick_magnitude = tick_magnitude}), chain.view,
	                     element);
}

} // namespace detail

/// @brief The chain codec built on the effects library.
/// @return A ChainCodec whose decode and encode go through the effects library's chain codec.
inline ChainCodec test_chain_codec() {
	return ChainCodec{.decode = &detail::decode_chain, .encode = &detail::encode_chain};
}

} // namespace deluge::project::test
