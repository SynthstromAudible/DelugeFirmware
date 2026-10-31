/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// Random patching cases for the matrix specs: cable records in both scopes (depth cables among them), usability, and
// legal source, preset and depth values; and the reference patcher's results for them.

#pragma once

#include "reference_patcher.h"

#include "dsp-next/fixed/noise/cong.h"
#include "synth/modulation/patch_routing.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace deluge::synth::test {

namespace params = deluge::modulation::params;

inline constexpr uint8_t kLocal = 0;  ///< the voice scope's routing index
inline constexpr uint8_t kGlobal = 1; ///< the sound scope's

inline constexpr ReferencePatcher::Config kVoiceConfig{
    .first_param = 0,
    .first_non_volume_param = params::FIRST_LOCAL_NON_VOLUME,
    .first_hybrid_param = params::FIRST_LOCAL__HYBRID,
    .first_exp_param = params::FIRST_LOCAL_EXP,
    .end_params = params::FIRST_GLOBAL,
    .globality = kLocal,
};
inline constexpr ReferencePatcher::Config kSoundConfig{
    .first_param = params::FIRST_GLOBAL,
    .first_non_volume_param = params::FIRST_GLOBAL_NON_VOLUME,
    .first_hybrid_param = params::FIRST_GLOBAL_HYBRID,
    .first_exp_param = params::FIRST_GLOBAL_EXP,
    .end_params = params::kNumParams,
    .globality = kGlobal,
};
inline constexpr int32_t kNumGlobalSources = std::to_underlying(kFirstLocalSource);
inline constexpr int32_t kNumGlobalParams = params::kNumParams - params::FIRST_GLOBAL;

/// One patching case: what an instrument holds and what one block reads.
struct PatchCase {
	std::vector<CableRecord> records;
	std::bitset<kMaxCables> usable;
	/// Each block's source values (the initial patching's, then three blocks'): aftertouch unipolar, the rest bipolar.
	std::array<std::array<int32_t, kNumPatchSources>, 4> sources{};
	std::array<int32_t, params::kNumParams> presets{}; ///< as stored
	std::array<int32_t, kMaxCables> depths{};          ///< in routing order, [-2^30, 2^30]
	std::array<uint32_t, 3> sources_changed{};         ///< three blocks' masks after the initial patching
};

/// A case drawn from @p seed: up to 24 cables, each scope's cables reading only the sources it may.
inline PatchCase random_case(uint32_t seed) {
	dsp::fixed::Cong rng{seed | 1u};
	// The generator's low bits cycle with a short period: draw from the high half
	auto below = [&](uint32_t n) { return static_cast<int32_t>((rng.next() >> 16) % n); };
	PatchCase c;
	const int32_t num_param_cables = below(20);
	auto has = [&](CableRecord r) {
		for (const CableRecord& e : c.records) {
			if (e.source == r.source && e.target == r.target) {
				return true;
			}
		}
		return false;
	};
	for (int32_t i = 0; i < num_param_cables; ++i) {
		const bool global = below(4) == 0;
		CableRecord r;
		r.source = static_cast<PatchSource>(global ? below(kNumGlobalSources) : below(kNumPatchSources));
		r.target = CableTarget::param(static_cast<uint8_t>(global ? params::FIRST_GLOBAL + below(kNumGlobalParams)
		                                                          : below(params::FIRST_GLOBAL)));
		r.polarity = below(2) == 0 ? CablePolarity::UNIPOLAR : CablePolarity::BIPOLAR;
		if (!has(r)) {
			c.records.push_back(r);
		}
	}
	// Depth cables, on cables of their own scope
	const auto num_params = static_cast<int32_t>(c.records.size());
	for (int32_t i = 0; i < num_params && c.records.size() < kMaxCables; ++i) {
		if (below(3) != 0) {
			continue;
		}
		const CableRecord& modulated = c.records[i];
		const bool global = modulated.target.param_index() >= params::FIRST_GLOBAL;
		CableRecord r;
		r.source = static_cast<PatchSource>(global ? below(kNumGlobalSources) : below(kNumPatchSources));
		r.target = CableTarget::depth_of(modulated.target.param_index(), modulated.source);
		r.polarity = below(2) == 0 ? CablePolarity::UNIPOLAR : CablePolarity::BIPOLAR;
		if (!has(r)) {
			c.records.push_back(r);
		}
	}
	for (size_t i = 0; i < c.records.size(); ++i) {
		c.usable[i] = below(10) != 0;
	}
	for (auto& block : c.sources) {
		for (int32_t s = 0; s < kNumPatchSources; ++s) {
			const auto v = static_cast<int32_t>(rng.next());
			block[s] =
			    static_cast<PatchSource>(s) == PatchSource::AFTERTOUCH ? (v & std::numeric_limits<int32_t>::max()) : v;
		}
	}
	for (int32_t& p : c.presets) {
		p = static_cast<int32_t>(rng.next());
	}
	for (int32_t& d : c.depths) {
		d = static_cast<int32_t>(rng.next() % ((uint32_t{1} << 31) + 1)) - (1 << 30);
	}
	for (uint32_t& m : c.sources_changed) {
		m = rng.next() & ((uint32_t{1} << kNumPatchSources) - 1);
	}
	return c;
}

/// The routing @p c compiles to, and the block one pass reads from it.
struct CompiledCase {
	RoutingBuild build;
	ReferenceBlock block;
};

inline void compile_case(const PatchCase& c, CompiledCase& out) {
	compile_routing(c.records, c.usable, out.build);
	out.block.routing = {{out.build.num_destinations[0] != 0 ? out.build.destinations[0].data() : nullptr,
	                      out.build.num_destinations[1] != 0 ? out.build.destinations[1].data() : nullptr},
	                     {out.build.sources_patched[0], out.build.sources_patched[1]},
	                     out.build.cables.data()};
	out.block.preset_values = c.presets;
	out.block.depths = c.depths;
}

/// Each scope's final values after the initial patching and each of the case's three blocks.
struct ReferenceResult {
	std::array<std::array<int32_t, params::FIRST_GLOBAL>, 4> voice{};
	std::array<std::array<int32_t, kNumGlobalParams>, 4> sound{};
};

/// Where the depth results live: one array for every pass (the Patcher's global array), or one per pass.
enum class Ranges : uint8_t { shared, per_pass };

/// Each scope's final values after the initial patching and each of the case's three blocks, the sound's pass then the
/// voice's in each block, as the audio tier runs them.
inline ReferenceResult reference_results(const PatchCase& c, const CompiledCase& compiled, Ranges ranges) {
	ReferenceResult out;
	std::array<int32_t, kNumPatchSources> voice_sources = c.sources[0];
	std::array<int32_t, kNumGlobalSources> sound_sources{};
	std::copy_n(c.sources[0].begin(), kNumGlobalSources, sound_sources.begin());
	std::array<int32_t, params::FIRST_GLOBAL> voice_finals{};
	std::array<int32_t, kNumGlobalParams> sound_finals{};
	std::array<int32_t, kMaxCables> voice_ranges{};
	std::array<int32_t, kMaxCables> sound_ranges{};
	ReferencePatcher voice{kVoiceConfig, voice_sources, voice_finals, voice_ranges};
	ReferencePatcher sound{kSoundConfig, sound_sources, sound_finals,
	                       ranges == Ranges::shared ? std::span{voice_ranges} : std::span{sound_ranges}};
	sound.perform_initial_patching(compiled.block);
	out.sound[0] = sound_finals;
	voice.perform_initial_patching(compiled.block);
	out.voice[0] = voice_finals;
	for (size_t b = 0; b < c.sources_changed.size(); ++b) {
		voice_sources = c.sources[b + 1];
		std::copy_n(c.sources[b + 1].begin(), kNumGlobalSources, sound_sources.begin());
		sound.perform_patching(c.sources_changed[b], compiled.block);
		out.sound[b + 1] = sound_finals;
		voice.perform_patching(c.sources_changed[b], compiled.block);
		out.voice[b + 1] = voice_finals;
	}
	return out;
}

} // namespace deluge::synth::test
