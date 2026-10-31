/// @file
/// @brief The mod-matrix list's matrix cases: the q31 matrix's patching on the workload. The PatchCableSet is built by
/// hand and compiles its program in setupPatching, so no ModelStack is needed.

#include "bench/cases.h"

#include "bench/mod_matrix_workload.h"
#include "definitions_cxx.hpp"
#include "foundation/panic.h"
#include "memory/heaps.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection_summary.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "synth/matrix/evaluator.h"
#include "synth/matrix/matrix_block.h"
#include "synth/matrix/matrix_fixed.h"
#include "synth/modulation/param_plane.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace deluge::bench {

namespace {

namespace mm = mod_matrix;
namespace params = deluge::modulation::params;

using Evaluator = deluge::synth::matrix::Evaluator<deluge::synth::matrix::MatrixFixed>;
using deluge::synth::matrix::MatrixBlock;
using deluge::synth::matrix::ScopePass;

struct MatrixRig {
	ParamCollectionSummary patched_summary{};
	ParamCollectionSummary cables_summary{};
	PatchedParamSet patched{&patched_summary};
	PatchCableSet cables{&cables_summary};
	GlobalParams global_plane{};
	std::array<VoiceParams, mm::kVoices> voice_planes{};
	uint32_t block_index = 0;
};

/// Built once, in the app's fast heap, where voices live.
MatrixRig& matrix_rig() {
	static MatrixRig* rig = new (deluge::memory::alloc_fast(sizeof(MatrixRig), alignof(MatrixRig))) MatrixRig{};
	return *rig;
}

/// The block a sound gathers for one patching call, as Sound::matrixBlock does: the program, the routed destinations'
/// presets (every param's for the initial patching) and the routed cables' depths.
MatrixBlock control_block(MatrixRig& r, bool all_presets) {
	MatrixBlock block;
	block.program = r.cables.program();
	const deluge::synth::PatchRouting routing = r.cables.routing();
	if (all_presets) {
		for (int32_t p = 0; p < params::kNumParams; p++) {
			block.preset_values[p] = r.patched.getValue(p);
		}
	}
	else {
		for (const deluge::synth::RoutedDestination* list : routing.destinations) {
			for (const deluge::synth::RoutedDestination* d = list; d != nullptr && d->sources != 0; d++) {
				if (d->target.is_param()) {
					block.preset_values[d->target.param_index()] = r.patched.getValue(d->target.param_index());
				}
			}
		}
	}
	for (int32_t c = 0; c < r.cables.numUsablePatchCables; c++) {
		block.depths[c] = r.cables.patchCables[c].param.getCurrentValue();
	}
	return block;
}

void build(MatrixRig& r) {
	std::bitset<kMaxNumPatchCables> usable;
	for (const mm::WorkloadCable& w : mm::kCables) {
		ParamDescriptor target;
		if (w.range_of >= 0) {
			target.setToHaveParamAndSource(w.param, static_cast<PatchSource>(mm::kCables[w.range_of].source));
		}
		else {
			target.setToHaveParamOnly(w.param);
		}
		usable[r.cables.numPatchCables] = true;
		r.cables.appendCable(
		    {static_cast<PatchSource>(w.source),
		     {target.data},
		     w.unipolar ? deluge::synth::CablePolarity::UNIPOLAR : deluge::synth::CablePolarity::BIPOLAR});
	}
	r.cables.setupPatching(r.cables_summary, usable);
	if (r.cables.numUsablePatchCables != mm::kCables.size()) {
		FREEZE_WITH_ERROR("MMB1"); // the workload's cables must all route
	}
	for (const mm::WorkloadCable& w : mm::kCables) {
		ParamDescriptor target;
		if (w.range_of >= 0) {
			target.setToHaveParamAndSource(w.param, static_cast<PatchSource>(mm::kCables[w.range_of].source));
		}
		else {
			target.setToHaveParamOnly(w.param);
		}
		const uint8_t c = r.cables.getPatchCableIndex(static_cast<PatchSource>(w.source), target);
		r.cables.patchCables[c].param.setCurrentValueBasicForSetup(*r.cables.docForParam(c), w.amount);
	}
	for (const mm::WorkloadDestination& d : mm::kDestinations) {
		r.patched.params[d.param].setCurrentValueBasicForSetup(d.preset_q31);
	}
	for (VoiceParams& plane : r.voice_planes) {
		for (const auto& [source, value] : mm::kStaticSources) {
			plane.source_values[source] = value;
		}
	}
}

void advance_global(MatrixRig& r) {
	for (std::size_t i = 0; i < mm::kNumGlobalMoving; ++i) {
		const uint8_t s = mm::kMovingSources[i];
		r.global_plane.source_values[s] = mm::moving_value_q31(r.block_index, 0, s);
	}
}

void take_globals_and_advance(MatrixRig& r, std::size_t v) {
	VoiceParams& plane = r.voice_planes[v];
	for (std::size_t s = 0; s < std::to_underlying(kFirstLocalSource); ++s) {
		plane.source_values[s] = r.global_plane.source_values[s];
	}
	for (std::size_t i = mm::kNumGlobalMoving; i < mm::kMovingSources.size(); ++i) {
		const uint8_t s = mm::kMovingSources[i];
		plane.source_values[s] = mm::moving_value_q31(r.block_index, static_cast<uint32_t>(v) + 1, s);
	}
}

ScopePass sound_pass(MatrixRig& r) {
	return {.scope = GLOBALITY_GLOBAL,
	        .sources = r.global_plane.source_values,
	        .final_values = r.global_plane.param_final_values};
}

ScopePass voice_pass(MatrixRig& r, std::size_t v) {
	return {.scope = GLOBALITY_LOCAL,
	        .sources = r.voice_planes[v].source_values,
	        .final_values = r.voice_planes[v].param_final_values};
}

uint32_t moving_mask(std::size_t from, std::size_t to) {
	uint32_t m = 0;
	for (std::size_t i = from; i < to; ++i) {
		m |= mm::source_bit(mm::kMovingSources[i]);
	}
	return m;
}

} // namespace

void MatrixCase::setup(std::size_t /*n*/) {
	MatrixRig& r = matrix_rig();
	if (r.cables.numPatchCables == 0) {
		build(r);
	}
	r.block_index = 0;
	const MatrixBlock cb = control_block(r, true);
	advance_global(r);
	Evaluator::run_initial(sound_pass(r), cb);
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		Evaluator::run_initial(voice_pass(r, v), cb);
	}
}

void MatrixCase::block(std::size_t /*n*/) {
	MatrixRig& r = matrix_rig();
	constexpr uint32_t kAll = (uint32_t{1} << kNumPatchSources) - 1;
	const uint32_t global_mask = kind_ == ModMatrixKind::ALL ? kAll : moving_mask(0, mm::kNumGlobalMoving);
	const uint32_t local_mask = kind_ == ModMatrixKind::ALL ? kAll : moving_mask(0, mm::kMovingSources.size());
	const bool initial = kind_ == ModMatrixKind::INITIAL;
	advance_global(r);
	// The sound gathers once for its own patching and once for its voices', as Sound::render does.
	const MatrixBlock sound_block = control_block(r, initial);
	if (initial) {
		Evaluator::run_initial(sound_pass(r), sound_block);
	}
	else {
		Evaluator::run(sound_pass(r), sound_block, global_mask);
	}
	const MatrixBlock voice_block = control_block(r, initial);
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		if (initial) {
			Evaluator::run_initial(voice_pass(r, v), voice_block);
		}
		else {
			Evaluator::run(voice_pass(r, v), voice_block, local_mask);
		}
	}
	++r.block_index;
}

} // namespace deluge::bench
