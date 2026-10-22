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

#include "model/project_bridge/chain_bridge.h"

#include "model/mod_controllable/delay_type.h"

#include "engines/effects/chain.h"
#include "engines/effects/codec.h"
#include "engines/effects/engine.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "util/functions.h"

namespace deluge::bridge {

using deluge::project::ChainAddress;
using deluge::project::ChainHome;
using deluge::project::FxChain;
using deluge::project::FxSlot;

namespace {

deluge::effects::Home homeOf(ChainHome home) {
	return home == ChainHome::MASTER ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP;
}

// The model holds the view as indexes of the effects' view words, which name the app's enums in order.
static_assert(util::to_underlying(ModFXParam::DEPTH) == 0 && util::to_underlying(ModFXParam::FEEDBACK) == 1
              && util::to_underlying(ModFXParam::OFFSET) == 2 && kNumModFXParams == 3);
static_assert(util::to_underlying(FilterType::LPF) == 0 && util::to_underlying(FilterType::HPF) == 1
              && util::to_underlying(FilterType::EQ) == 2 && kNumFilterTypes == 3);

/// A chain of @p home whose effects are @p states and whose knobs edit @p view.
FxChain chainFrom(deluge::effects::EffectStates states, ChainHome home, deluge::effects::ChainView view) {
	FxChain chain{.reverb_return = deluge::effects::reverb_return(homeOf(home)),
	              .effects = deluge::engine::StateHandle::of<deluge::effects::DelugeEffects>(std::move(states)),
	              .mod_fx_param = view.mod_fx_param,
	              .filter_type = view.filter_type};
	for (std::string_view effect : deluge::effects::chain_effects(homeOf(home))) {
		chain.slots.push_back(FxSlot{.effect = std::string(effect)});
	}
	return chain;
}

FxChain decodeChain(const deluge::serialization::Value& element, ChainHome home, int32_t tick_magnitude) {
	deluge::effects::EffectStates states;
	deluge::effects::decode(element, states, {.input_tick_magnitude = tick_magnitude});
	return chainFrom(std::move(states), home, deluge::effects::read_view(element));
}

void encodeChain(const FxChain& chain, deluge::serialization::Value& element, int32_t tick_magnitude) {
	const deluge::effects::EffectStates* states = chain.effects.as<deluge::effects::DelugeEffects>();
	if (states == nullptr) {
		return;
	}
	const deluge::effects::Home home =
	    chain.reverb_return ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP;
	deluge::effects::write_chain(
	    deluge::effects::encode_chain(*states, home, {.input_tick_magnitude = tick_magnitude}),
	    deluge::effects::view_value({.mod_fx_param = chain.mod_fx_param, .filter_type = chain.filter_type}), element);
}

/// The output's own effects owner, when it runs a host chain (a kit, an audio track).
::GlobalEffectable* ownerOf(::Output& output) {
	switch (output.type) {
	case OutputType::KIT:
		return static_cast<::Kit*>(&output);
	case OutputType::AUDIO:
		return static_cast<::AudioOutput*>(&output);
	default:
		return nullptr;
	}
}

/// @brief @p owner's state set from @p record, when the record decodes to anything else (it does not when the codec
/// round-trips, so a load sets no state twice).
void projectRecord(::GlobalEffectable& owner, const FxChain& record) {
	const deluge::model::HostChainState decoded = chainStateOf(record);
	if (decoded != owner.chainState()) {
		owner.setChainState(decoded);
	}
}

template <class F>
void forEachOutput(::Song& song, F&& f) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		f(*output);
	}
	for (Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		f(*sleeping);
	}
}

} // namespace

const deluge::project::ChainCodec& chainCodec() {
	static const deluge::project::ChainCodec codec{.decode = &decodeChain, .encode = &encodeChain};
	return codec;
}

::GlobalEffectable* chainOwnerAt(ChainAddress address, ::Song& song) {
	if (address.kind == ChainAddress::Kind::SONG) {
		return &song.live.global_effectable;
	}
	::GlobalEffectable* found = nullptr;
	forEachOutput(song, [&](::Output& output) {
		if (found == nullptr && output.id() == address.id) {
			found = ownerOf(output);
		}
	});
	return found;
}

std::optional<ChainAddress> chainAddressOf(const ::ModControllableAudio& owner, ::Song& song) {
	if (&owner == &song.live.global_effectable) {
		return ChainAddress{.kind = ChainAddress::Kind::SONG};
	}
	std::optional<ChainAddress> found;
	forEachOutput(song, [&](::Output& output) {
		const ::GlobalEffectable* own = ownerOf(output);
		if (!found && own != nullptr && own == &owner && output.id()) {
			found = ChainAddress{.kind = ChainAddress::Kind::OUTPUT, .id = *output.id()};
		}
	});
	return found;
}

FxChain encodeChainOf(const deluge::model::HostChainState& state, ChainHome home) {
	return chainFrom(state.effects, home,
	                 {.mod_fx_param = static_cast<uint8_t>(util::to_underlying(state.mod_fx_param)),
	                  .filter_type = static_cast<uint8_t>(util::to_underlying(state.filter_type))});
}

deluge::model::HostChainState chainStateOf(const FxChain& chain) {
	deluge::model::HostChainState state;
	if (const deluge::effects::EffectStates* effects = chain.effects.as<deluge::effects::DelugeEffects>()) {
		state.effects = *effects;
	}
	state.mod_fx_param = static_cast<ModFXParam>(std::min<int32_t>(chain.mod_fx_param, kNumModFXParams - 1));
	state.filter_type = static_cast<FilterType>(std::min<int32_t>(chain.filter_type, kNumFilterTypes - 1));
	return state;
}

void fillChains(::Song& song) {
	forEachOutput(song, [&](::Output& output) { recordOutputChain(output, song); });
	::GlobalEffectable& master = song.live.global_effectable;
	song.project.master_effects = encodeChainOf(master.chainState(), ChainHome::MASTER);
	projectRecord(master, song.project.master_effects);
	// Setting a state reserves each delay's tape in the current song, which during a load is not yet this one: the
	// reserves are worked out again in this song.
	deluge::model::refreshDelayReserves(song);
}

void recordOutputChain(::Output& output, ::Song& song) {
	::GlobalEffectable* owner = ownerOf(output);
	if (owner == nullptr || !output.id()) {
		return;
	}
	deluge::project::DocOutput* record = song.project.output_structure.find(*output.id());
	if (record == nullptr) {
		return;
	}
	record->effects = encodeChainOf(owner->chainState(), ChainHome::CLIP);
	projectRecord(*owner, *record->effects);
}

} // namespace deluge::bridge
