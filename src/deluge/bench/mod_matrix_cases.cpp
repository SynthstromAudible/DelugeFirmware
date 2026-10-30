/// @file
/// @brief The mod-matrix list's Patcher cases: today's q31 patching on the workload. The PatchCableSet is built by hand
/// in setupPatching's layout, so no ModelStack is needed.

#include "bench/cases.h"

#include "bench/mod_matrix_workload.h"
#include "definitions_cxx.hpp"
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
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

extern int32_t rangeFinalValues[];

namespace deluge::bench {

namespace {

namespace mm = mod_matrix;
namespace params = deluge::modulation::params;

const int32_t kNeutralRange = 536870912;

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
	std::array<Destination, 12> local_dests{};
	std::array<Destination, 4> global_dests{};
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

ControlBlock control_block(PatcherRig& r) {
	return {.patched_params = &r.patched, .patch_cable_set = &r.cables, .smoothing_param = -1, .smoothing_value = 0};
}

std::size_t build_destinations(PatcherRig& r, bool global, std::span<Destination> out) {
	std::size_t n = 0;
	for (std::size_t c = 0; c < mm::kCables.size();) {
		const mm::WorkloadCable& w = mm::kCables[c];
		const bool cable_global = w.range_of < 0 && w.param >= params::FIRST_GLOBAL;
		if (cable_global != global) {
			++c;
			continue;
		}
		Destination& d = out[n++];
		d.destinationParamDescriptor = r.cables.patchCables[c].destinationParamDescriptor;
		d.firstCable = static_cast<uint8_t>(c);
		d.sources = 0;
		while (c < mm::kCables.size()
		       && r.cables.patchCables[c].destinationParamDescriptor == d.destinationParamDescriptor) {
			d.sources |= mm::source_bit(mm::kCables[c].source);
			++c;
		}
		d.endCable = static_cast<uint8_t>(c);
	}
	out[n].destinationParamDescriptor.setToNull();
	out[n].sources = 0;
	return n;
}

void build(PatcherRig& r) {
	for (const mm::WorkloadCable& w : mm::kCables) {
		PatchCable* cable = r.cables.addCable();
		cable->from = static_cast<PatchSource>(w.source);
		cable->polarity = w.unipolar ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		if (w.range_of >= 0) {
			cable->destinationParamDescriptor.setToHaveParamAndSource(
			    w.param, static_cast<PatchSource>(mm::kCables[w.range_of].source));
		}
		else {
			cable->destinationParamDescriptor.setToHaveParamOnly(w.param);
		}
		cable->param.setCurrentValueBasicForSetup(w.amount);
		cable->rangeAdjustmentPointer = &kNeutralRange;
	}
	r.cables.numUsablePatchCables = r.cables.numPatchCables;
	const std::size_t n_local = build_destinations(r, false, r.local_dests);
	build_destinations(r, true, r.global_dests);
	// Depth destinations lead the local list: link each to the cable it modulates, as setupPatching does.
	for (std::size_t i = 0; i < n_local && r.local_dests[i].destinationParamDescriptor.data < 0xFFFFFF00U; ++i) {
		const auto target = static_cast<std::size_t>(mm::kCables[r.local_dests[i].firstCable].range_of);
		const uint32_t range_sources = r.local_dests[i].sources;
		r.cables.patchCables[target].rangeAdjustmentPointer = &rangeFinalValues[i];
		r.local_dests[i].sources |= mm::source_bit(mm::kCables[target].source);
		for (std::size_t j = i + 1; j < n_local; ++j) {
			if (target >= r.local_dests[j].firstCable && target < r.local_dests[j].endCable) {
				r.local_dests[j].sources |= range_sources;
			}
		}
	}
	for (std::size_t i = 0; r.local_dests[i].sources != 0; ++i) {
		r.cables.sourcesPatchedToAnything[GLOBALITY_LOCAL] |= r.local_dests[i].sources;
	}
	for (std::size_t i = 0; r.global_dests[i].sources != 0; ++i) {
		r.cables.sourcesPatchedToAnything[GLOBALITY_GLOBAL] |= r.global_dests[i].sources;
	}
	r.cables.destinations[GLOBALITY_LOCAL] = r.local_dests.data();
	r.cables.destinations[GLOBALITY_GLOBAL] = r.global_dests.data();
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
	const ControlBlock cb = control_block(r);
	advance_global(r);
	r.global.performInitialPatching(cb);
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		r.voices[v].performInitialPatching(cb);
	}
}

void PatcherCase::block(std::size_t /*n*/) {
	PatcherRig& r = patcher_rig();
	const ControlBlock cb = control_block(r);
	constexpr uint32_t kAll = (uint32_t{1} << kNumPatchSources) - 1;
	const uint32_t global_mask = kind_ == ModMatrixKind::PATCHER_ALL ? kAll : moving_mask(0, mm::kNumGlobalMoving);
	const uint32_t local_mask = kind_ == ModMatrixKind::PATCHER_ALL ? kAll : moving_mask(0, mm::kMovingSources.size());
	advance_global(r);
	if (kind_ == ModMatrixKind::PATCHER_INITIAL) {
		r.global.performInitialPatching(cb);
	}
	else {
		r.global.performPatching(global_mask, cb);
	}
	for (std::size_t v = 0; v < mm::kVoices; ++v) {
		take_globals_and_advance(r, v);
		if (kind_ == ModMatrixKind::PATCHER_INITIAL) {
			r.voices[v].performInitialPatching(cb);
		}
		else {
			r.voices[v].performPatching(local_mask, cb);
		}
	}
	++r.block_index;
}

} // namespace deluge::bench
