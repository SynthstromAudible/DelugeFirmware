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

#include "model/fx/chain_params.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"
#include "model/fx/effect_reads.h"
#include "modulation/params/arp_param_set.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"

#include <algorithm>
#include <span>

namespace deluge::model {

namespace params = deluge::modulation::params;

namespace {

/// @brief The first and last ids of the unpatched params a sound and a host chain share.
constexpr uint32_t kFirstSharedId = params::UNPATCHED_START;
constexpr uint32_t kLastSharedId = params::UNPATCHED_START + params::UNPATCHED_NUM_SHARED - 1;

/// @brief Sets the rows of @p table with ids in [@p first, @p last], other than @p skip, to their defaults.
/// @param set      The owner's unpatched params.
/// @param table    The owner's table.
/// @param first    The first id to set.
/// @param last     The last id to set.
/// @param skip     The ids in that range to leave as they are.
void setRowsToDefaults(UnpatchedParamSet& set, std::span<const plugin::ParamInfo> table, uint32_t first, uint32_t last,
                       std::span<const plugin::ParamId> skip) {
	for (const plugin::ParamInfo& info : table) {
		if (info.id.value < first || info.id.value > last || std::ranges::contains(skip, info.id)) {
			continue;
		}
		const int32_t p = unpatchedIndex(info.id);
		set.params[p].setBase(*set.docForParam(p), info.default_value);
	}
}

} // namespace

void initSharedParams(ParamManager* paramManager, params::Kind defaults) {
	const std::span<const plugin::ParamInfo> table =
	    defaults == params::Kind::UNPATCHED_SOUND ? std::span<const plugin::ParamInfo>{instruments::classic::kParams}
	                                              : std::span<const plugin::ParamInfo>{host::chains::kParams};
	setRowsToDefaults(*paramManager->getUnpatchedParamSet(), table, kFirstSharedId, kLastSharedId, {});
	// A sound's or a kit's arp starts at its owner's values too.
	if (ArpParamSet* arp = paramManager->getArpParamSet()) {
		arp->set_to_defaults();
	}
}

void initGlobalParams(ParamManager* paramManager) {
	initSharedParams(paramManager, params::Kind::UNPATCHED_GLOBAL);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = params::Kind::UNPATCHED_GLOBAL;
	setRowsToDefaults(*unpatchedParams, host::chains::kParams, kLastSharedId + 1,
	                  host::chains::kParamBlock.first.value + host::chains::kParamBlock.count - 1,
	                  host::chains::kNotInitialised);
}

void initAudioClipParams(ParamManagerForTimeline* paramManager) {
	initGlobalParams(paramManager);
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_VOLUME].setBase(*unpatchedParams->docForParam(params::UNPATCHED_VOLUME),
	                                                          -536870912);
}

} // namespace deluge::model
