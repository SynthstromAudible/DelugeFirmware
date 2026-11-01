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

#include "effects/chain.h"
#include "effects/codec.h"
#include "effects/plugin.h"
#include "effects/settings.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "plugin-api/types/sync.h"
#include "processing/audio_output.h"
#include "serialization/tree.h"
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
	              .effects = deluge::plugin::StateHandle::of<deluge::effects::DelugeEffects>(std::move(states)),
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
		// Every chain record is made by binding its owner, which gives it the owner's effects: one without them would
		// save an owner element with no effects.
		if constexpr (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("E470");
		}
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
	// The chain the owner plays names it: the master's, or the one an output's record of this song holds (an owner of
	// another song plays that song's record, and gets nothing here).
	const ::GlobalEffectable* effectable = owner.asGlobalEffectable();
	const FxChain* bound = effectable != nullptr ? effectable->boundChain() : nullptr;
	if (bound == nullptr) {
		return std::nullopt;
	}
	if (bound == &song.project.master_effects) {
		return ChainAddress{.kind = ChainAddress::Kind::SONG};
	}
	for (const auto& [id, record] : song.project.output_structure.by_id) {
		if (record.effects && &*record.effects == bound) {
			// An output out of the song's lists (one an undo holds for its redo) plays its record but is not one of
			// the song's owners: a gesture on it goes to its setters.
			const ChainAddress address{.kind = ChainAddress::Kind::OUTPUT, .id = id};
			return chainOwnerAt(address, song) == effectable ? std::optional{address} : std::nullopt;
		}
	}
	return std::nullopt;
}

void adoptChainState(::GlobalEffectable& owner, FxChain& chain, ChainHome home) {
	if (owner.boundChain() == &chain) {
		return;
	}
	chain.slots.clear();
	for (std::string_view effect : deluge::effects::chain_effects(homeOf(home))) {
		chain.slots.push_back(FxSlot{.effect = std::string(effect)});
	}
	chain.reverb_return = deluge::effects::reverb_return(homeOf(home));
	owner.bindChain(chain);
}

void recordOutputChain(::Output& output, ::Song& song) {
	::GlobalEffectable* owner = ownerOf(output);
	if (owner == nullptr || !output.id()) {
		return;
	}
	deluge::project::DocOutput* record = song.project.output_structure.find(*output.id());
	if (record == nullptr || record != output.docOutput_) {
		return;
	}
	if (!record->effects) {
		record->effects.emplace();
	}
	adoptChainState(*owner, *record->effects, ChainHome::CLIP);
}

void auditEffectWrite(const deluge::effects::EffectStates& before, deluge::effects::Home home, std::string_view path,
                      const deluge::serialization::Value& value, int32_t tick_magnitude,
                      const deluge::effects::EffectStates& got) {
	TypedWriteAudit& audit = typedWriteAudit();
	const std::optional<std::string_view> effect = deluge::effects::effect_of_setting(path);
	if (!audit.enabled || !effect) {
		return;
	}
	const deluge::plugin::CodecContext context{.input_tick_magnitude = tick_magnitude};
	deluge::serialization::Value leaf = value;
	if (path.ends_with("/syncLevel")) {
		const int32_t level = deluge::serialization::int_of(&value).value_or(0);
		leaf = deluge::serialization::Value::makeNumber(deluge::plugin::sync_level_to_file(level, tick_magnitude));
	}
	deluge::effects::EffectStates treeWay = before;
	for (deluge::effects::SlotEntries& slot : deluge::effects::encode_chain(before, home, context)) {
		if (slot.effect == *effect) {
			deluge::effects::decode_slot(*effect, withLeaf(slot.entries, path, leaf), treeWay, context);
		}
	}
	audit.writes++;
	// A value the file cannot hold differs only where the file cannot tell: both encode alike.
	if (treeWay == got || deluge::effects::encode(got, context) == deluge::effects::encode(treeWay, context)) {
		return;
	}
	audit.mismatches++;
	if (audit.first.empty()) {
		audit.first = std::string(path) + "=" + value.str() + ": "
		              + deluge::serialization::first_difference(deluge::effects::encode(got, context),
		                                                        deluge::effects::encode(treeWay, context));
	}
}

::GlobalEffectable* chainOwnerOf(::Output& output) {
	return ownerOf(output);
}

} // namespace deluge::bridge
