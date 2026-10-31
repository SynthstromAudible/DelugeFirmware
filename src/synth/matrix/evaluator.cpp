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

#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_fixed.h"

#include <array>
#include <utility>

namespace deluge::synth::matrix {

namespace {

/// Every pass's depth results, as today's Patcher shares them: one pass runs at a time, on the audio tier.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::array<int32_t, kMaxCables> shared_depth_results{};

template <class B>
[[gnu::always_inline]] inline int32_t source_of(const ScopeView& scope, const ScopePass& pass, int32_t cable) {
	const uint8_t slot = scope.cable_slot[cable];
	return B::convert(scope.slot_conversion[slot], pass.sources[scope.slot_source[slot]]);
}

template <class B>
[[gnu::always_inline]] inline int32_t range_of(const ScopeView& scope, int32_t cable) {
	const uint8_t range = scope.cable_range[cable];
	return range == kNoRange ? B::kOne : shared_depth_results[range];
}

/// A depth destination's combination: its cables multiplied, with no range step and no preset.
template <class B>
int32_t combine_depth(const ScopeView& scope, const ScopePass& pass, const MatrixBlock& block, const Destination& d) {
	int32_t total = B::kOne;
	for (int32_t c = d.first_cable; c < d.first_cable + d.num_cables; ++c) {
		const int32_t term = B::depth_product(source_of<B>(scope, pass, c), block.depths[scope.cable_depth[c]]);
		total = B::multiply_step(total, term);
	}
	return total - B::kOne;
}

/// A multiply chain's combination for param @p p: the preset, then each cable.
template <class B>
int32_t combine_multiply(const ScopeView* scope, const ScopePass& pass, const MatrixBlock& block, const Destination* d,
                         int32_t p) {
	int32_t total = B::multiply_step(B::kOne, B::preset_term(block.preset_value(p), p));
	if (d != nullptr) {
		for (int32_t c = d->first_cable; c < d->first_cable + d->num_cables; ++c) {
			const int32_t term =
			    B::product(source_of<B>(*scope, pass, c), block.depths[scope->cable_depth[c]], range_of<B>(*scope, c));
			total = B::multiply_step(total, term);
		}
	}
	return total - B::kOne;
}

/// An add chain's combination for param @p p: each cable, the wavetable doubling, then the preset.
template <class B>
int32_t combine_add(const ScopeView* scope, const ScopePass& pass, const MatrixBlock& block, const Destination* d,
                    int32_t p) {
	int32_t total = 0;
	if (d != nullptr) {
		for (int32_t c = d->first_cable; c < d->first_cable + d->num_cables; ++c) {
			const int32_t depth = B::depth(scope->cable_mode[c], block.depths[scope->cable_depth[c]]);
			total = B::add_step(total, B::product(source_of<B>(*scope, pass, c), depth, range_of<B>(*scope, c)));
		}
		if (d->wave_index_double) {
			total = B::double_saturating(total);
		}
	}
	return B::add_step(total, B::preset_term(block.preset_value(p), p));
}

template <class B>
int32_t combine_param(const ScopeView* scope, const ScopePass& pass, const MatrixBlock& block, const Destination* d,
                      int32_t p) {
	return chain_of(p) == ChainKind::MULTIPLY ? combine_multiply<B>(scope, pass, block, d, p)
	                                          : combine_add<B>(scope, pass, block, d, p);
}

template <class B>
void write_final(const ScopePass& pass, int32_t first_param, int32_t p, int32_t combination) {
	pass.final_values[p - first_param] = B::curve(curve_of(p), p, combination);
}

[[nodiscard]] int32_t first_param_of(uint8_t scope) {
	return scope == 0 ? 0 : int32_t{params::FIRST_GLOBAL};
}

[[nodiscard]] int32_t end_param_of(uint8_t scope) {
	return scope == 0 ? int32_t{params::FIRST_GLOBAL} : int32_t{params::kNumParams};
}

} // namespace

template <class B>
void Evaluator<B>::run(const ScopePass& pass, const MatrixBlock& block, uint32_t sources_changed) {
	if (block.program == nullptr) {
		return;
	}
	const ScopeView& scope = block.program->scopes[pass.scope];
	if (scope.num_destinations == 0) {
		return;
	}
	sources_changed &= scope.sources_patched;
	if (sources_changed == 0u) {
		return;
	}
	// Depth destinations first: the cables they control read their results
	for (int32_t k = 0; std::cmp_less(k, scope.num_depth_destinations); ++k) {
		const Destination& d = scope.destinations[k];
		if ((d.sources & sources_changed) == 0u) {
			continue;
		}
		shared_depth_results[d.param] = B::depth_result(combine_depth<B>(scope, pass, block, d));
	}
	const int32_t first_param = first_param_of(pass.scope);
	for (int32_t k = scope.num_depth_destinations; std::cmp_less(k, scope.num_destinations); ++k) {
		const Destination& d = scope.destinations[k];
		if ((d.sources & sources_changed) == 0u) {
			continue;
		}
		write_final<B>(pass, first_param, d.param, combine_param<B>(&scope, pass, block, &d, d.param));
	}
}

template <class B>
void Evaluator<B>::run_initial(const ScopePass& pass, const MatrixBlock& block) {
	const ScopeView* scope = block.program != nullptr ? &block.program->scopes[pass.scope] : nullptr;
	const int32_t first_param = first_param_of(pass.scope);
	const uint64_t cabled = scope != nullptr ? scope->cabled : 0;
	for (int32_t p = first_param; p < end_param_of(pass.scope); ++p) {
		if (((cabled >> (p - first_param)) & 1u) == 0u) {
			write_final<B>(pass, first_param, p, combine_param<B>(scope, pass, block, nullptr, p));
		}
	}
	if (scope == nullptr) {
		return;
	}
	for (int32_t k = 0; std::cmp_less(k, scope->num_depth_destinations); ++k) {
		const Destination& d = scope->destinations[k];
		shared_depth_results[d.param] = B::depth_result(combine_depth<B>(*scope, pass, block, d));
	}
	for (int32_t k = scope->num_depth_destinations; std::cmp_less(k, scope->num_destinations); ++k) {
		const Destination& d = scope->destinations[k];
		write_final<B>(pass, first_param, d.param, combine_param<B>(scope, pass, block, &d, d.param));
	}
}

template <class B>
void Evaluator<B>::run_uncabled_param(const ScopePass& pass, const MatrixBlock& block, int32_t p) {
	write_final<B>(pass, first_param_of(pass.scope), p, combine_param<B>(nullptr, pass, block, nullptr, p));
}

template class Evaluator<MatrixFixed>;

} // namespace deluge::synth::matrix
