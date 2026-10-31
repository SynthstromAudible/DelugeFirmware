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

namespace params = modulation::params;

/// One pass's working values: each polarity slot's source as its cables read it, and each depth destination's result.
struct Working {
	std::array<int32_t, kMaxCables> slots;
	std::array<int32_t, kMaxCables> depth_results;
};

/// A scope's first param.
[[nodiscard]] constexpr int32_t first_param_of(uint8_t scope) {
	return scope == 0 ? 0 : int32_t{params::FIRST_GLOBAL};
}

/// A run of consecutive params sharing a curve and a chain: what the uncabled bulk computes four at a time.
struct CurveRun {
	int32_t first;
	int32_t end;
	CurveKind curve;
	ChainKind chain;
};

/// One scope's runs, in param order.
struct ScopeRuns {
	std::array<CurveRun, params::kNumParams> runs;
	size_t count;
};

/// Each scope's runs, from the param table.
inline constexpr std::array<ScopeRuns, 2> kRuns = [] {
	std::array<ScopeRuns, 2> out{};
	for (uint8_t scope = 0; scope < 2; ++scope) {
		const int32_t end = scope == 0 ? int32_t{params::FIRST_GLOBAL} : int32_t{params::kNumParams};
		for (int32_t p = first_param_of(scope); p < end; ++p) {
			ScopeRuns& r = out[scope];
			if (r.count != 0 && r.runs[r.count - 1].curve == curve_of(p) && r.runs[r.count - 1].chain == chain_of(p)) {
				r.runs[r.count - 1].end = p + 1;
			}
			else {
				r.runs[r.count++] = {.first = p, .end = p + 1, .curve = curve_of(p), .chain = chain_of(p)};
			}
		}
	}
	return out;
}();

template <class B>
void convert_slots(const ScopeView& scope, const ScopePass& pass, Working& w) {
	for (int32_t s = 0; s < scope.num_slots; ++s) {
		w.slots[s] = B::convert(scope.slot_conversion[s], pass.sources[scope.slot_source[s]]);
	}
}

/// A depth destination's result: its cables multiplied, with no range step and no preset.
template <class B>
int32_t depth_result(const ScopeView& scope, const MatrixBlock& block, const Working& w, const Destination& d) {
	int32_t total = B::kOne;
	for (int32_t c = d.first_cable; c < d.first_cable + d.num_cables; ++c) {
		total =
		    B::multiply_step(total, B::depth_product(w.slots[scope.cable_slot[c]], block.depths[scope.cable_depth[c]]));
	}
	return B::depth_result(total - B::kOne);
}

/// Every depth destination's result, for this pass: computed on every pass, so a cable it scales always reads the
/// current depth, and no pass reads another's.
template <class B>
void depth_results(const ScopeView& scope, const MatrixBlock& block, Working& w) {
	for (int32_t k = 0; k < scope.num_depth_destinations; ++k) {
		const Destination& d = scope.destinations[k];
		w.depth_results[d.param] = depth_result<B>(scope, block, w, d);
	}
}

/// Cable @p c's term: its slot's value scaled by its depth (shaped on an add chain), then by its depth result.
template <class B>
[[gnu::always_inline]] inline int32_t term_of(const ScopeView& scope, const MatrixBlock& block, const Working& w,
                                              int32_t c, bool add_chain) {
	int32_t depth = block.depths[scope.cable_depth[c]];
	if (add_chain) {
		depth = B::depth(scope.cable_mode[c], depth);
	}
	const uint8_t r = scope.cable_range[c];
	return B::product(w.slots[scope.cable_slot[c]], depth, r == kNoRange ? B::kOne : w.depth_results[r]);
}

/// A param destination's final value: a multiply chain takes the preset, then each cable's term; an add chain sums
/// the terms, doubles them for a wavetable index, then adds the preset.
template <class B>
int32_t patched_value(const ScopeView& scope, const MatrixBlock& block, const Working& w, const Destination& d) {
	const int32_t p = d.param;
	const int32_t end = d.first_cable + d.num_cables;
	int32_t combination = 0;
	if (d.chain == ChainKind::MULTIPLY) {
		int32_t total = B::multiply_step(B::kOne, B::preset_term(block.preset_value(p), p));
		for (int32_t c = d.first_cable; c < end; ++c) {
			total = B::multiply_step(total, term_of<B>(scope, block, w, c, false));
		}
		combination = total - B::kOne;
	}
	else {
		int32_t total = 0;
		for (int32_t c = d.first_cable; c < end; ++c) {
			total = B::add_step(total, term_of<B>(scope, block, w, c, true));
		}
		if (d.wave_index_double) {
			total = B::double_saturating(total);
		}
		combination = B::add_step(total, B::preset_term(block.preset_value(p), p));
	}
	return B::curve(d.curve, p, combination);
}

/// Param @p p's combination with no cables, the chains' scalar steps from its preset alone.
template <class B>
int32_t uncabled_combination(const MatrixBlock& block, int32_t p) {
	const int32_t term = B::preset_term(block.preset_value(p), p);
	return chain_of(p) == ChainKind::MULTIPLY ? B::multiply_step(B::kOne, term) - B::kOne : B::add_step(0, term);
}

/// Every param in [@p from, @p to) as if uncabled, four at a time while four remain, through @p lanes; the rest
/// through the scalar curve @p kind.
template <class B, class Lanes4>
void uncabled_run(const ScopePass& pass, const MatrixBlock& block, int32_t first, int32_t from, int32_t to,
                  CurveKind kind, Lanes4 lanes) {
	using L = B::Lanes;
	int32_t p = from;
	for (; p + 4 <= to; p += 4) {
		const L terms = B::preset_terms(L::Load(&block.preset_values[p]), L::Load(&kParamRange[p]));
		lanes(L::Load(&kParamNeutral[p]), terms).StoreTo(&pass.final_values[p - first]);
	}
	for (; p < to; ++p) {
		pass.final_values[p - first] = B::curve(kind, p, uncabled_combination<B>(block, p));
	}
}

/// The uncabled params' final values: the square-, linear- and bipolar-curve runs four at a time (the cabled among them
/// are overwritten after), the exponential and release-table ones one by one, skipping the cabled.
template <class B>
void uncabled_params(const ScopePass& pass, const MatrixBlock& block, uint64_t cabled) {
	using L = B::Lanes;
	const int32_t first = first_param_of(pass.scope);
	const ScopeRuns& runs = kRuns[pass.scope];
	for (size_t k = 0; k < runs.count; ++k) {
		const CurveRun& run = runs.runs[k];
		const bool multiply = run.chain == ChainKind::MULTIPLY;
		auto combination = [multiply](L terms) { return multiply ? B::uncabled_multiply(terms) : terms; };
		switch (run.curve) {
		case CurveKind::SQUARE:
			uncabled_run<B>(pass, block, first, run.first, run.end, run.curve,
			                [&](L neutral, L terms) { return B::curve_square(neutral, combination(terms)); });
			break;
		case CurveKind::LINEAR:
			uncabled_run<B>(pass, block, first, run.first, run.end, run.curve,
			                [&](L neutral, L terms) { return B::curve_linear(neutral, combination(terms)); });
			break;
		case CurveKind::BIPOLAR:
			uncabled_run<B>(pass, block, first, run.first, run.end, run.curve,
			                [&](L neutral, L terms) { return B::curve_bipolar(neutral, combination(terms)); });
			break;
		default:
			for (int32_t p = run.first; p < run.end; ++p) {
				if (((cabled >> (p - first)) & 1u) == 0u) {
					pass.final_values[p - first] = B::curve(run.curve, p, uncabled_combination<B>(block, p));
				}
			}
			break;
		}
	}
}

} // namespace

template <class B>
void Evaluator<B>::run(const ScopePass& pass, const MatrixBlock& block, uint32_t sources_changed) {
	if (block.program == nullptr) {
		return;
	}
	const ScopeView& scope = block.program->scopes[pass.scope];
	sources_changed &= scope.sources_patched;
	if (sources_changed == 0u) {
		return;
	}
	Working w; // NOLINT(cppcoreguidelines-pro-type-member-init): each read entry is written first
	convert_slots<B>(scope, pass, w);
	// Depth destinations first: the cables they control read their results
	depth_results<B>(scope, block, w);
	const int32_t first = first_param_of(pass.scope);
	for (int32_t k = scope.num_depth_destinations; k < scope.num_destinations; ++k) {
		const Destination& d = scope.destinations[k];
		if ((d.sources & sources_changed) != 0u) {
			pass.final_values[d.param - first] = patched_value<B>(scope, block, w, d);
		}
	}
}

template <class B>
void Evaluator<B>::run_initial(const ScopePass& pass, const MatrixBlock& block) {
	const ScopeView* scope = block.program != nullptr ? &block.program->scopes[pass.scope] : nullptr;
	uncabled_params<B>(pass, block, scope != nullptr ? scope->cabled : 0);
	if (scope == nullptr || scope->num_destinations == 0) {
		return;
	}
	Working w; // NOLINT(cppcoreguidelines-pro-type-member-init): each read entry is written first
	convert_slots<B>(*scope, pass, w);
	depth_results<B>(*scope, block, w);
	const int32_t first = first_param_of(pass.scope);
	for (int32_t k = scope->num_depth_destinations; k < scope->num_destinations; ++k) {
		const Destination& d = scope->destinations[k];
		pass.final_values[d.param - first] = patched_value<B>(*scope, block, w, d);
	}
}

template <class B>
void Evaluator<B>::run_uncabled_param(const ScopePass& pass, const MatrixBlock& block, int32_t p) {
	pass.final_values[p - first_param_of(pass.scope)] = B::curve(curve_of(p), p, uncabled_combination<B>(block, p));
}

template class Evaluator<MatrixFixed>;

} // namespace deluge::synth::matrix
