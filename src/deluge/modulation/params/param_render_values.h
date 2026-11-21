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

#include "modulation/params/param_collection_summary.h"
#include <cstdint>

class ParamManager;
class ParamCollection;

// A live, read-only view for one render operation. Do not retain it across
// collection replacement, cloning or song changes. Values are not cached.
// Include param_set.h where get() is used so the render read can be inlined.
class ParamRenderValues {
public:
	[[nodiscard]] int32_t get(int32_t p) const;

private:
	friend class ParamManager;
	explicit ParamRenderValues(const ParamCollectionSummary& summary)
	    : collection_(*summary.paramCollection), summary_(summary) {}
	// This non-owning view intentionally cannot rebind either half of the pair.
	// NOLINTBEGIN(cppcoreguidelines-avoid-const-or-ref-data-members)
	const ParamCollection& collection_;
	const ParamCollectionSummary& summary_;
	// NOLINTEND(cppcoreguidelines-avoid-const-or-ref-data-members)
};
