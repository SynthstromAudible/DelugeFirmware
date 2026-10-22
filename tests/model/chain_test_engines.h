#pragma once

// The chain codec the model tests give the model: the effects library's, as the app's is.

#include "deluge-project/fx_chain.h"
#include "engines/effects/chain.h"
#include "engines/effects/codec.h"
#include "engines/effects/engine.h"

namespace deluge::project::test {

namespace detail {

inline effects::Home home_of(ChainHome home) {
	return home == ChainHome::MASTER ? effects::Home::MASTER : effects::Home::CLIP;
}

inline FxChain decode_chain(const serialization::Value& element, ChainHome home, int32_t tick_magnitude) {
	effects::EffectStates states;
	effects::decode(element, states, {.input_tick_magnitude = tick_magnitude});
	const effects::ChainView view = effects::read_view(element);
	FxChain chain{.reverb_return = effects::reverb_return(home_of(home)),
	              .effects = engine::StateHandle::of<effects::DelugeEffects>(std::move(states)),
	              .mod_fx_param = view.mod_fx_param,
	              .filter_type = view.filter_type};
	for (std::string_view effect : effects::chain_effects(home_of(home))) {
		chain.slots.push_back({.effect = std::string(effect)});
	}
	return chain;
}

inline void encode_chain(const FxChain& chain, serialization::Value& element, int32_t tick_magnitude) {
	const effects::EffectStates* states = chain.effects.as<effects::DelugeEffects>();
	if (states == nullptr) {
		return;
	}
	const effects::Home home = chain.reverb_return ? effects::Home::MASTER : effects::Home::CLIP;
	effects::write_chain(effects::encode_chain(*states, home, {.input_tick_magnitude = tick_magnitude}),
	                     effects::view_value({.mod_fx_param = chain.mod_fx_param, .filter_type = chain.filter_type}),
	                     element);
}

} // namespace detail

/// @brief The chain codec built on the effects library.
/// @return A ChainCodec whose decode and encode go through the effects library's chain codec.
inline ChainCodec test_chain_codec() {
	return ChainCodec{.decode = &detail::decode_chain, .encode = &detail::encode_chain};
}

} // namespace deluge::project::test
