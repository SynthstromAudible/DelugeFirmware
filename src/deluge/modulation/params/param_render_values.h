/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#pragma once

#include "modulation/params/param_collection.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_value.h"
#include <cstdint>

class ParamManager;

// What a parameter set resolves its render values from. A set builds this on demand, so it adds no
// storage. Param is AutoParam in the firmware; the host tests use its AutoParamData.
template <typename Param>
struct ParamRenderSource {
	const Param* params;
	const ClipParamValue* clip_values;
	int32_t* values;
	uint32_t* version;
	// One bit per parameter: an automated parameter renders its curve, any other its clip value.
	const uint32_t* automated;
	int32_t count;

	[[nodiscard]] int32_t resolve(int32_t p) const {
		return (automated[p >> 5] & (uint32_t{1} << (p & 31))) != 0 ? params[p].getValueForRender()
		                                                            : clip_values[p].getValue();
	}
	[[nodiscard]] bool isCurrent() const { return *version == parameter_value_epoch; }
	// Resolve every render value again after a value changed outside playback.
	void refresh() const {
		for (int32_t p = 0; p < count; ++p) {
			values[p] = resolve(p);
		}
		*version = parameter_value_epoch;
#ifdef PARAMETER_RESOLVED_VERIFY
		parameterVerifyRefreshes = parameterVerifyRefreshes + 1;
#endif
	}
};

// Resolve a parameter set's render values again after a value changed outside playback.
[[gnu::noinline]] void refreshRenderValues(const ParamCollectionSummary& summary);
#ifdef PARAMETER_RESOLVED_VERIFY
void verifyRenderValue(const ParamCollectionSummary& summary, int32_t p, int32_t value);
#endif

// A read-only view of a parameter set's resolved render values for one render operation.
// It is brought up to date when made, so it must not be kept across a change to any
// parameter value other than by playback, or across collection replacement, cloning or
// song changes.
class ParamRenderValues {
public:
	// For a source outside a ParamManager, such as in the host tests.
	template <typename Param>
	explicit ParamRenderValues(const ParamRenderSource<Param>& source) : values_(source.values) {
		if (!source.isCurrent()) {
			source.refresh();
		}
	}
	[[nodiscard]] int32_t get(int32_t p) const {
#ifdef PARAMETER_RESOLVED_VERIFY
		if (summary_ != nullptr) {
			verifyRenderValue(*summary_, p, values_[p]);
		}
#endif
		return values_[p];
	}

private:
	friend class ParamManager;
	explicit ParamRenderValues(const ParamCollectionSummary& summary)
	    : values_(summary.paramCollection->renderValues_)
#ifdef PARAMETER_RESOLVED_VERIFY
	      ,
	      summary_(&summary)
#endif
	{
		if (summary.paramCollection->renderVersion_ != parameter_value_epoch) [[unlikely]] {
			refreshRenderValues(summary);
		}
	}
	const int32_t* values_;
#ifdef PARAMETER_RESOLVED_VERIFY
	const ParamCollectionSummary* summary_ = nullptr;
#endif
};
