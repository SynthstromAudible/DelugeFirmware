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

#include "engines/deluge/params.h"
#include "engines/global_effectable/params.h"

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

deluge::engine::ParamId paramIdOf(params::Kind kind, int32_t index) {
	const auto at = static_cast<uint8_t>(index);
	switch (kind) {
	case params::Kind::PATCHED:
		return instrument::param_id({instrument::RuntimeSet::PATCHED, at});
	case params::Kind::UNPATCHED_SOUND:
		return instrument::param_id({instrument::RuntimeSet::UNPATCHED, at});
	case params::Kind::UNPATCHED_GLOBAL:
		return global_effectable::param_id(at);
	default:
		return deluge::engine::ParamId{static_cast<uint32_t>(index)};
	}
}

deluge::engine::ParamId firstParamId(params::Kind kind) {
	return paramIdOf(kind, 0);
}

} // namespace deluge::bridge
