/// @file
/// @brief The mod-matrix list's Patcher cases: today's q31 patching on the workload. The PatchCableSet is built by hand
/// in setupPatching's layout, so no ModelStack is needed.

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
#include "synth/modulation/control_block.h"
#include "synth/modulation/param_plane.h"
#include "synth/modulation/patcher.h"

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

const Patcher::Config kVoiceConfig{
    .firstParam = 0,
    .firstNonVolumeParam = params::FIRST_LOCAL_NON_VOLUME,
    .firstHybridParam = params::FIRST_LOCAL__HYBRID,
    .firstExpParam = params::FIRST_LOCAL_EXP,
    .endParams = params::FIRST_GLOBAL,
    .globality = GLOBALITY_LOCAL,
};
const Patcher::Config kSoundConfig{
    .firstParam = params::FIRST_GLOBAL,
    .firstNonVolumeParam = params::FIRST_GLOBAL_NON_VOLUME,
    .firstHybridParam = params::FIRST_GLOBAL_HYBRID,
    .firstExpParam = params::FIRST_GLOBAL_EXP,
    .endParams = params::kNumParams,
    .globality = GLOBALITY_GLOBAL,
};

template <std::size_t... I>
std::array<Patcher, sizeof...(I)> make_voice_patchers(std::array<VoiceParams, sizeof...(I)>& planes,
                                                      std::index_sequence<I...> /*unused*/) {
	return {Patcher{kVoiceConfig, planes[I].sourceValues, planes[I].paramFinalValues}...};
}

struct PatcherRig {
	ParamCollectionSummary patched_summary{};
	ParamCollectionSummary cables_summary{};
	PatchedParamSet patched{&patched_summary};
	PatchCableSet cables{&cables_summary};
	GlobalParams global_plane{};
	std::array<VoiceParams, mm::kVoices> voice_planes{};
	Patcher global{kSoundConfig, global_plane.sourceValues, global_plane.paramFinalValues};
	std::array<Patcher, mm::kVoices> voices =
	    make_voice_patchers(voice_planes, std::make_index_sequence<mm::kVoices>{});
	uint32_t block_index = 0;
};

/// Built once, in the app's fast heap, where voices live.
PatcherRig& patcher_rig() {
	static PatcherRig* rig = new (deluge::memory::alloc_fast(sizeof(PatcherRig), alignof(PatcherRig))) PatcherRig{};
	return *rig;
}

/// The block a sound gathers for one patching call, as Sound::patchControlBlock does: the routing, the routed
/// destinations' presets (every param's for the initial patching) and the routed cables' depths.
ControlBlock control_block(PatcherRig& r, bool all_presets) {
	ControlBlock block;
	block.routing = r.cables.routing();
	if (all_presets) {
		for (int32_t p = 0; p < params::kNumParams; p++) {
			block.preset_values[p] = r.patched.getValue(p);
		}
	}
	else {
		for (const deluge::synth::RoutedDestination* list : block.routing.destinations) {
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

void build(PatcherRig& r) {
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
			plane.sourceValues[source] = value;
		}
	}
}

void advance_global(PatcherRig& r) {
	for (std::size_t i = 0; i < mm::kNumGlobalMoving; ++i) {
		const uint8_t s = mm::kMovingSources[i];
		r.global_plane.sourceValues[s] = mm::moving_value_q31(r.block_index, 0, s);
	}
}

void take_globals_and_advance(PatcherRig& r, std::size_t v) {
	VoiceParams& plane = r.voice_planes[v];
	for (std::size_t s = 0; s < std::to_underlying(kFirstLocalSource); ++s) {
		plane.sourceValues[s] = r.global_plane.sourceValues[s];
	}
	for (std::size_t i = mm::kNumGlobalMoving; i < mm::kMovingSources.size(); ++i) {
		const uint8_t s = mm::kMovingSources[i];
		plane.sourceValues[s] = mm::moving_value_q31(r.block_index, static_cast<uint32_t>(v) + 1, s);
	}
}

uint32_t moving_mask(std::size_t from, std::size_t to) {
	uint32_t m = 0;
	for (std::size_t i = from; i < to; ++i) {
		m |= mm::source_bit(mm::kMovingSources[i]);
	}
	return m;
}

} // namespace

void PatcherCase::setup(std::size_t /*n*/) {
	PatcherRig& r = patcher_rig();
	if (r.cables.numPatchCables == 0) {
		build(r);
	}
	r.block_index = 0;
	const ControlBlock cb = control_block(r, true);
	advance_global(r);
	r.global.performInitialPatching(cb);
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		r.voices[v].performInitialPatching(cb);
	}
}

void PatcherCase::block(std::size_t /*n*/) {
	PatcherRig& r = patcher_rig();
	constexpr uint32_t kAll = (uint32_t{1} << kNumPatchSources) - 1;
	const uint32_t global_mask = kind_ == ModMatrixKind::PATCHER_ALL ? kAll : moving_mask(0, mm::kNumGlobalMoving);
	const uint32_t local_mask = kind_ == ModMatrixKind::PATCHER_ALL ? kAll : moving_mask(0, mm::kMovingSources.size());
	const bool initial = kind_ == ModMatrixKind::PATCHER_INITIAL;
	advance_global(r);
	// The sound gathers once for its own patching and once for its voices', as Sound::render does.
	const ControlBlock sound_block = control_block(r, initial);
	if (initial) {
		r.global.performInitialPatching(sound_block);
	}
	else {
		r.global.performPatching(global_mask, sound_block);
	}
	const ControlBlock voice_block = control_block(r, initial);
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		if (initial) {
			r.voices[v].performInitialPatching(voice_block);
		}
		else {
			r.voices[v].performPatching(local_mask, voice_block);
		}
	}
	++r.block_index;
}

} // namespace deluge::bench
