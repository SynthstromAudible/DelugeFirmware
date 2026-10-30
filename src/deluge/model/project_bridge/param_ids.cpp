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

#include "model/project_bridge/param_ids.h"

#include "definitions_cxx.hpp"
#include "modulation/patch/patch_cable.h"

#include "effects/global_effectable/params.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/params.h"

#include <algorithm>
#include <array>
#include <optional>

namespace deluge::bridge {

namespace params = deluge::modulation::params;
namespace instrument = deluge::instrument;
namespace global_effectable = deluge::global_effectable;

// The runtime's numbering may move only with the engines' maps; these anchor each run's ends and sizes.
static_assert(instrument::param::kOscAVolume.value == params::LOCAL_OSC_A_VOLUME);
static_assert(instrument::param::kEnv4Release.value == params::LOCAL_ENV_3_RELEASE);
static_assert(instrument::param::kVolumePostFX.value == params::GLOBAL_VOLUME_POST_FX);
static_assert(instrument::param::kArpRate.value == params::GLOBAL_ARP_RATE);
static_assert(instrument::kParamBlocks[0].count == params::kNumParams);
static_assert(instrument::kUnpatchedFirstId == params::UNPATCHED_START);
static_assert(instrument::param::kStutterRate.value == params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE);
static_assert(instrument::param::kPortamento.value == params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO);
static_assert(instrument::kParamBlocks[1].count == params::UNPATCHED_SOUND_MAX_NUM);
static_assert(global_effectable::kFirstId == params::UNPATCHED_START);
static_assert(global_effectable::param::kTempo.value == params::UNPATCHED_START + params::UNPATCHED_TEMPO);
static_assert(global_effectable::kParamBlock.count == params::UNPATCHED_GLOBAL_MAX_NUM);
static_assert(instrument::kParamBlocks.size() <= deluge::project::kEngineParamBlocks);
// An unpatched set not yet named (kind NONE) is numbered and stored the same whichever kind it becomes.
static_assert(instrument::kUnpatchedFirstId == global_effectable::kFirstId);

static_assert(static_cast<int>(instrument::Polarity::UNIPOLAR) == static_cast<int>(::Polarity::UNIPOLAR));
static_assert(static_cast<int>(instrument::Polarity::BIPOLAR) == static_cast<int>(::Polarity::BIPOLAR));

deluge::plugin::ParamId cableDepthIdOf(uint32_t word) {
	deluge::plugin::ParamId id{word & 0xFFu};
	for (uint32_t shift = 8; shift <= 24; shift += 8) {
		const uint32_t source = (word >> shift) & 0xFFu;
		if (source == 0xFFu) {
			break;
		}
		id = instrument::cable_depth_id(static_cast<instrument::PatchSource>(source), id).value_or(id);
	}
	return id;
}

uint32_t cableWordOf(deluge::plugin::ParamId depth) {
	std::array<uint32_t, 3> sources{0xFFu, 0xFFu, 0xFFu};
	size_t n = 0;
	deluge::plugin::ParamId at = depth;
	while (std::optional<instrument::CableRoute> route = instrument::cable_route(at)) {
		sources[n++] = static_cast<uint32_t>(route->source);
		at = route->destination;
	}
	// The route unwinds from the outermost source in; the word holds the innermost at bits 8-15.
	std::reverse(sources.begin(), sources.begin() + static_cast<std::ptrdiff_t>(n));
	return (at.value & 0xFFu) | sources[0] << 8 | sources[1] << 16 | sources[2] << 24;
}

deluge::plugin::ParamId paramIdOf(params::Kind kind, int32_t index) {
	const auto at = static_cast<uint8_t>(index);
	switch (kind) {
	case params::Kind::PATCHED:
		return instrument::param_id({instrument::RuntimeSet::PATCHED, at});
	// An unpatched set reports NONE until its owner's init names it; a sound's and a global effectable's unpatched
	// params share their numbering, so it is numbered as unpatched either way.
	case params::Kind::NONE:
	case params::Kind::UNPATCHED_SOUND:
		return instrument::param_id({instrument::RuntimeSet::UNPATCHED, at});
	case params::Kind::UNPATCHED_GLOBAL:
		return global_effectable::param_id(at);
	case params::Kind::PATCH_CABLE: // the index is the runtime's packed cable word
		return cableDepthIdOf(static_cast<uint32_t>(index));
	default:
		return deluge::plugin::ParamId{static_cast<uint32_t>(index)};
	}
}

std::optional<int32_t> runtimeIndexOf(params::Kind kind, deluge::plugin::ParamId id) {
	switch (kind) {
	case params::Kind::PATCHED:
	case params::Kind::UNPATCHED_SOUND: {
		std::optional<instrument::RuntimeSlot> slot = instrument::runtime_slot(id);
		const auto set =
		    kind == params::Kind::PATCHED ? instrument::RuntimeSet::PATCHED : instrument::RuntimeSet::UNPATCHED;
		if (!slot || slot->set != set) {
			return std::nullopt;
		}
		return slot->index;
	}
	case params::Kind::NONE:
		// An unpatched set its owner's init has not named yet may be either kind's; both number from the same first id.
		if (std::optional<int32_t> index = runtimeIndexOf(params::Kind::UNPATCHED_SOUND, id)) {
			return index;
		}
		return runtimeIndexOf(params::Kind::UNPATCHED_GLOBAL, id);
	case params::Kind::UNPATCHED_GLOBAL:
		if (std::optional<uint8_t> index = global_effectable::runtime_index(id)) {
			return *index;
		}
		return std::nullopt;
	case params::Kind::PATCH_CABLE:
		return static_cast<int32_t>(cableWordOf(id));
	default:
		return static_cast<int32_t>(id.value);
	}
}

deluge::plugin::ParamId firstParamId(params::Kind kind) {
	return paramIdOf(kind, 0);
}

deluge::project::ParamSpace spaceOf(params::Kind kind) {
	switch (kind) {
	case params::Kind::EXPRESSION:
		return deluge::project::ParamSpace::EXPRESSION;
	case params::Kind::MIDI:
		return deluge::project::ParamSpace::MIDI_CC;
	default:
		return deluge::project::ParamSpace::ENGINE;
	}
}

std::optional<deluge::project::DocParamSet>* storeFor(deluge::project::DocParamManager& doc, params::Kind kind) {
	switch (kind) {
	case params::Kind::PATCHED:
		return &doc.engine_params[0];
	case params::Kind::NONE: // an unpatched set its owner's init has not named yet
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		return &doc.engine_params[1];
	case params::Kind::EXPRESSION:
		return &doc.expression;
	default:
		return nullptr;
	}
}

const std::optional<deluge::project::DocParamSet>* storeFor(const deluge::project::DocParamManager& doc,
                                                            params::Kind kind) {
	return storeFor(const_cast<deluge::project::DocParamManager&>(doc), kind);
}

deluge::project::ParamAddress addressOf(deluge::project::OwnerId owner, params::Kind kind, int32_t index,
                                        std::optional<uint32_t> clip_id) {
	return {owner, spaceOf(kind), paramIdOf(kind, index), clip_id};
}

} // namespace deluge::bridge
