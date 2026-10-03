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

#pragma once

// A model patch cable stores the Deluge engine's words raw; these read and write them as the engine's types.

#include "deluge-project/param_automation.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/patch/patch_cable.h"
#include "util/misc.h"

namespace deluge::bridge {

[[nodiscard]] constexpr PatchSource source_of(const deluge::project::DocPatchCable& cable) noexcept {
	return static_cast<PatchSource>(cable.from);
}

[[nodiscard]] constexpr ParamDescriptor destination_of(const deluge::project::DocPatchCable& cable) noexcept {
	ParamDescriptor destination;
	destination.data = cable.destination;
	return destination;
}

[[nodiscard]] constexpr Polarity polarity_of(const deluge::project::DocPatchCable& cable) noexcept {
	return static_cast<Polarity>(cable.polarity);
}

/// @brief A model patch cable from the engine's words and its amount.
[[nodiscard]] inline deluge::project::DocPatchCable doc_cable(PatchSource from, ParamDescriptor destination,
                                                              Polarity polarity, deluge::project::DocAutoParam amount) {
	return {.from = util::to_underlying(from),
	        .destination = destination.data,
	        .polarity = util::to_underlying(polarity),
	        .amount = std::move(amount)};
}

namespace detail {
constexpr uint32_t engine_cable_param_id(uint32_t destination, uint8_t source) {
	ParamDescriptor descriptor;
	descriptor.data = destination;
	descriptor.addSource(static_cast<PatchSource>(source));
	return descriptor.data;
}
} // namespace detail

// The model resolves a cable's amount lane by the id the engine gives it; the two packings must agree.
static_assert(deluge::project::patch_cable_param_id(0xFFFFFF05u, 3) == detail::engine_cable_param_id(0xFFFFFF05u, 3));
static_assert(deluge::project::patch_cable_param_id(0xFFFF0705u, 9) == detail::engine_cable_param_id(0xFFFF0705u, 9));
static_assert(deluge::project::patch_cable_param_id(0xFF080705u, 2) == detail::engine_cable_param_id(0xFF080705u, 2));
static_assert(deluge::project::patch_cable_param_id(0x01080705u, 4) == detail::engine_cable_param_id(0x01080705u, 4));

} // namespace deluge::bridge
