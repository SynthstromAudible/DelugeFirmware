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

#include "synth/matrix/compiler.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <utility>

namespace deluge::synth::matrix {

namespace {

namespace params = modulation::params;

[[nodiscard]] constexpr std::size_t align_up(std::size_t n, std::size_t alignment) {
	return (n + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] std::size_t num_routed_cables(const RoutingBuild& routing, int32_t g) {
	std::size_t n = 0;
	for (int32_t d = 0; d < routing.num_destinations[g]; ++d) {
		n += routing.destinations[g][d].end_cable - routing.destinations[g][d].first_cable;
	}
	return n;
}

[[nodiscard]] std::size_t scope_size(std::size_t num_destinations, std::size_t num_cables) {
	return align_up(num_destinations * sizeof(Destination), alignof(Destination)) // destinations
	       + (2 * num_cables)                                                     // slots (at most one per cable)
	       + (4 * num_cables);                                                    // the four cable tables
}

/// Hands out consecutive pieces of the program's block.
class Carver {
public:
	explicit Carver(std::byte* at) : at_{at} {}
	template <class T>
	T* take(std::size_t count) {
		const auto address = reinterpret_cast<std::uintptr_t>(at_);
		at_ += align_up(address, alignof(T)) - address;
		T* out = reinterpret_cast<T*>(at_);
		at_ += count * sizeof(T);
		return out;
	}

private:
	std::byte* at_;
};

[[nodiscard]] bool is_wave_index(int32_t p) {
	return p == params::LOCAL_OSC_A_WAVE_INDEX || p == params::LOCAL_OSC_B_WAVE_INDEX;
}

/// The cable tables of one scope, being filled.
struct CableTables {
	uint8_t* slot;
	uint8_t* depth;
	uint8_t* range;
	DepthMode* mode;
};

/// Fills the tables for @p routed's cables from index @p cable on. @return the next free index.
template <class SlotFor>
uint8_t compile_cables(const RoutingBuild& routing, const RoutedDestination& routed, const Destination& dest,
                       bool depth, int32_t param, uint8_t cable, const CableTables& tables, SlotFor& slot_for) {
	for (int32_t c = routed.first_cable; c < routed.end_cable; ++c, ++cable) {
		const RoutedCable& routed_cable = routing.cables[c];
		tables.slot[cable] =
		    slot_for(routed_cable.source, conversion_of(routed_cable.source, routed_cable.polarity, depth));
		tables.depth[cable] = static_cast<uint8_t>(c);
		tables.range[cable] =
		    (depth || routed_cable.range_index < 0) ? kNoRange : static_cast<uint8_t>(routed_cable.range_index);
		tables.mode[cable] =
		    dest.chain == ChainKind::ADD ? depth_mode_of(routed_cable.source, param) : DepthMode::PLAIN;
	}
	return cable;
}

void compile_scope(const RoutingBuild& routing, int32_t g, Carver& carve, ScopeView& out) {
	const int32_t num_destinations = routing.num_destinations[g];
	const std::size_t num_cables = num_routed_cables(routing, g);

	auto* destinations = carve.take<Destination>(num_destinations);
	auto* slot_source = carve.take<uint8_t>(num_cables);
	auto* slot_conversion = carve.take<Conversion>(num_cables);
	const CableTables tables{
	    .slot = carve.take<uint8_t>(num_cables),
	    .depth = carve.take<uint8_t>(num_cables),
	    .range = carve.take<uint8_t>(num_cables),
	    .mode = carve.take<DepthMode>(num_cables),
	};

	const int32_t first_param = g == 0 ? 0 : int32_t{params::FIRST_GLOBAL};
	out.sources_patched = routing.sources_patched[g];

	uint8_t num_slots = 0;
	auto slot_for = [&](PatchSource source, Conversion conversion) {
		for (uint8_t s = 0; s < num_slots; ++s) {
			if (slot_source[s] == std::to_underlying(source) && slot_conversion[s] == conversion) {
				return s;
			}
		}
		slot_source[num_slots] = std::to_underlying(source);
		slot_conversion[num_slots] = conversion;
		return num_slots++;
	};

	uint8_t cable = 0;
	uint8_t num_depth = 0;
	for (int32_t d = 0; d < num_destinations; ++d) {
		const RoutedDestination& routed = routing.destinations[g][d];
		const bool depth = !routed.target.is_param();
		const int32_t param = routed.target.param_index();
		Destination& dest = destinations[d];
		dest.first_cable = cable;
		dest.num_cables = routed.end_cable - routed.first_cable;
		dest.chain = depth ? ChainKind::DEPTH : chain_of(param);
		dest.curve = depth ? CurveKind::LINEAR : curve_of(param);
		dest.param = depth ? num_depth++ : static_cast<uint8_t>(param);
		dest.wave_index_double = !depth && dest.chain == ChainKind::ADD && is_wave_index(param);
		dest.sources = routed.sources;
		if (!depth) {
			const auto bit = static_cast<uint32_t>(param - first_param);
			out.cabled |= bit < 64 ? uint64_t{1} << bit : 0;
		}
		cable = compile_cables(routing, routed, dest, depth, param, cable, tables, slot_for);
	}
	out.slot_source = slot_source;
	out.slot_conversion = slot_conversion;
	out.cable_slot = tables.slot;
	out.cable_depth = tables.depth;
	out.cable_range = tables.range;
	out.cable_mode = tables.mode;
	out.destinations = destinations;
	out.num_slots = num_slots;
	out.num_depth_destinations = num_depth;
	out.num_destinations = static_cast<uint8_t>(num_destinations);
}

} // namespace

std::size_t program_size(const RoutingBuild& routing) {
	std::size_t n = sizeof(MatrixProgram);
	for (int32_t g = 0; g < 2; ++g) {
		// Each scope's destination table follows byte tables, so may need its alignment rounded up
		n += scope_size(routing.num_destinations[g], num_routed_cables(routing, g)) + (alignof(Destination) - 1);
	}
	return n;
}

const MatrixProgram* compile_program(const RoutingBuild& routing, std::span<std::byte> storage) {
	MatrixProgram* program = std::construct_at(reinterpret_cast<MatrixProgram*>(storage.data()));
	Carver carve{storage.data() + sizeof(MatrixProgram)};
	for (int32_t g = 0; g < 2; ++g) {
		compile_scope(routing, g, carve, program->scopes[g]);
	}
	program->bytes = storage.size();
	return program;
}

const MatrixProgram* copy_program(const MatrixProgram& from, std::span<std::byte> storage) {
	std::memcpy(storage.data(), &from, from.bytes);
	auto* program = std::launder(reinterpret_cast<MatrixProgram*>(storage.data()));
	const auto* old_base = reinterpret_cast<const std::byte*>(&from);
	std::byte* new_base = storage.data();
	auto rebase = [&]<class T>(const T*& pointer) {
		pointer = reinterpret_cast<const T*>(new_base + (reinterpret_cast<const std::byte*>(pointer) - old_base));
	};
	for (ScopeView& scope : program->scopes) {
		rebase(scope.slot_source);
		rebase(scope.slot_conversion);
		rebase(scope.cable_slot);
		rebase(scope.cable_depth);
		rebase(scope.cable_range);
		rebase(scope.cable_mode);
		rebase(scope.destinations);
	}
	return program;
}

} // namespace deluge::synth::matrix
