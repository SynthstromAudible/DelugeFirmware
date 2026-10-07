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

#include "engines/effects/chain.h"
#include "engines/effects/codec.h"
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

/// @p chain's slots and view from @p slots, written at @p tick_magnitude.
FxChain chainFrom(std::vector<deluge::effects::SlotEntries> slots, ChainHome home, int32_t tick_magnitude,
                  deluge::serialization::Value view) {
	FxChain chain{.reverb_return = deluge::effects::reverb_return(homeOf(home)), .view = std::move(view)};
	for (deluge::effects::SlotEntries& slot : slots) {
		chain.slots.push_back(FxSlot{
		    .effect = std::string(slot.effect), .state = std::move(slot.entries), .tick_magnitude = tick_magnitude});
	}
	return chain;
}

FxChain decodeChain(const deluge::serialization::Value& element, ChainHome home, int32_t tick_magnitude) {
	const deluge::engine::CodecContext context{.input_tick_magnitude = tick_magnitude};
	deluge::effects::EffectStates states;
	deluge::effects::decode(element, states, context);
	return chainFrom(deluge::effects::encode_chain(states, homeOf(home), context), home, tick_magnitude,
	                 deluge::effects::view_entries(element));
}

void encodeChain(const FxChain& chain, deluge::serialization::Value& element, int32_t tick_magnitude) {
	deluge::effects::EffectStates states;
	for (const FxSlot& slot : chain.slots) {
		deluge::effects::decode_slot(slot.effect, slot.state, states, {.input_tick_magnitude = slot.tick_magnitude});
	}
	const deluge::effects::Home home =
	    chain.reverb_return ? deluge::effects::Home::MASTER : deluge::effects::Home::CLIP;
	deluge::effects::write_chain(deluge::effects::encode_chain(states, home, {.input_tick_magnitude = tick_magnitude}),
	                             chain.view, element);
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

FxChain encodeChainOf(::GlobalEffectable& owner, ChainHome home, int32_t tick_magnitude) {
	auto view = deluge::serialization::Value::makeMap();
	view.set("modFXCurrentParam",
	         deluge::serialization::Value::makeScalar(modFXParamToString(owner.currentModFXParam)));
	view.set("currentFilterType",
	         deluge::serialization::Value::makeScalar(filterTypeToString(owner.currentFilterType)));
	return chainFrom(
	    deluge::effects::encode_chain(owner.effects(), homeOf(home), {.input_tick_magnitude = tick_magnitude}), home,
	    tick_magnitude, std::move(view));
}

void fillChains(::Song& song) {
	const int32_t magnitude = song.getInputTickMagnitude();
	forEachOutput(song, [&](::Output& output) { recordOutputChain(output, song); });
	song.project.master_effects = encodeChainOf(song.live.global_effectable, ChainHome::MASTER, magnitude);
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
	record->effects = encodeChainOf(*owner, ChainHome::CLIP, song.getInputTickMagnitude());
}

} // namespace deluge::bridge
