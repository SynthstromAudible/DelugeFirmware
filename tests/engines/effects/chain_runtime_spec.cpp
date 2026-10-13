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

// The chain runtimes (sound_chain.h, clip_chain.h, master_chain.h) against each home's sequence over the same
// components (bench/reference_chains.h, which the bench times too): two chains built alike, one driven by the runtime
// and one by the reference, block by block over every workload (chain_workloads.h), each side from the same state of
// the synth-wide noise stream the filters and the grain draw from, their output, reverb send, recorder tap and noise
// draws compared word for word. And each runtime's order against chain.h's, and the master's between: once per block,
// after the send and volume, before the filter.

#include "engines/effects/chain.h"
#include "engines/effects/clip_chain.h"
#include "engines/effects/master_chain.h"
#include "engines/effects/sound_chain.h"

#include "bench/reference_chains.h"
#include "chain_workloads.h"
#include "util/waves.h"

#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace deluge::effects;
using deluge::dsp::StereoSample;

namespace {

namespace cw = chain_workloads;

/// @return @p block's samples, left then right per frame.
std::vector<int32_t> words(std::span<const StereoSample> block) {
	std::vector<int32_t> out;
	out.reserve(block.size() * 2);
	for (const StereoSample s : block) {
		out.push_back(s.l);
		out.push_back(s.r);
	}
	return out;
}

/// What one side of a comparison produced on one block.
struct Produced {
	std::vector<int32_t> output;
	std::vector<int32_t> send;
	std::vector<int32_t> seen; ///< the tap's block, or the master's at its between
	int calls = 0;             ///< how often the tap or between ran
	bool sent = false;
	uint32_t noise = 0; ///< the shared noise stream's state after the block: the draws the filters made

	bool operator==(const Produced&) const = default;
};

/// @return The first workload and block where @p a and @p b differ, or empty.
std::string first_difference(std::string_view workload, int block, const Produced& a, const Produced& b) {
	if (a == b) {
		return {};
	}
	std::string what = a.output != b.output ? "output"
	                   : a.send != b.send   ? "send"
	                   : a.seen != b.seen   ? "seen"
	                   : a.noise != b.noise ? "noise"
	                                        : "calls";
	return std::string{workload} + " block " + std::to_string(block) + ": " + what;
}

/// The filter scratch both sides use, as large as a stereo block.
struct Scratch {
	std::vector<q31_t> fade = std::vector<q31_t>(cw::kMaxFrames * 2);
	std::vector<q31_t> parallel = std::vector<q31_t>(cw::kMaxFrames * 2);
	[[nodiscard]] deluge::dsp::fixed::FilterScratch view() { return {.fade = fade, .parallel = parallel}; }
};

/// @return The first difference between SoundChain and the reference over every workload, or empty.
std::string sound_difference() {
	for (const cw::Workload& w : cw::kWorkloads) {
		const EffectStates s = cw::settings(w);
		DelayArena arena_a{cw::kArenaMemory};
		DelayArena arena_b{cw::kArenaMemory};
		std::string found;
		{
			SoundChain run{cw::kAllocator, cw::kAllocator, arena_a};
			SoundChain ref{cw::kAllocator, cw::kAllocator, arena_b};
			cw::hand_over(run.core(), s);
			cw::hand_over(ref.core(), s);
			Stutter stutter_a{cw::kAllocator};
			Stutter stutter_b{cw::kAllocator};
			if (w.stutter) {
				cw::begin(stutter_a, s);
				cw::begin(stutter_b, s);
			}
			for (int b = 0; b < cw::kBlocks && found.empty(); ++b) {
				const std::size_t frames = cw::kBlockFrames[b % cw::kBlockFrames.size()];
				const bool sound = cw::sounding(w, b);
				SoundChainInputs in{
				    .fx = cw::fx_inputs(w, s),
				    .bitcrush = cw::kBitcrush,
				    .post_fx_volume = 0x0A000000,
				    .post_reverb_volume = 134217728,
				    .reverb_send = 0x08000000,
				    .sound = sound,
				    .compressor_threshold = w.threshold,
				    .stutter = nullptr,
				    .stutter_rate = cw::kStutterRate,
				};
				in.fx.mod_fx_type = s.mod_fx.type; // a sound's type in effect is its setting
				Produced a;
				Produced r;
				const uint32_t noise = jcong; // the filters' and grain's shared stream: each side starts from it
				for (auto [chain, side, stutter] :
				     {std::tuple{&run, &a, &stutter_a}, std::tuple{&ref, &r, &stutter_b}}) {
					jcong = noise;
					SoundChainInputs mine = in;
					mine.fx.delay = cw::delay_state(chain->core(), w, s, sound);
					mine.stutter = w.stutter ? stutter : nullptr;
					std::vector<StereoSample> block = cw::input(w, b, frames);
					std::vector<int32_t> send(frames, 0);
					side->sent = chain == &run ? run.process(block, mine, send)
					                           : chain_reference::sound(ref.core(), mine, block, send);
					side->output = words(block);
					side->send = send;
					side->noise = jcong;
				}
				found = first_difference(w.name, b, a, r);
			}
			cw::release(run.core());
			cw::release(ref.core());
		}
		if (!found.empty()) {
			return found;
		}
	}
	return {};
}

/// @return The first difference between ClipChain and the reference over every workload, or empty.
std::string clip_difference() {
	for (const cw::Workload& w : cw::kWorkloads) {
		const EffectStates s = cw::settings(w);
		DelayArena arena_a{cw::kArenaMemory};
		DelayArena arena_b{cw::kArenaMemory};
		std::string found;
		{
			ClipChain run{cw::kAllocator, cw::kAllocator, arena_a, 134217728};
			ClipChain ref{cw::kAllocator, cw::kAllocator, arena_b, 134217728};
			cw::hand_over(run.core(), s);
			cw::hand_over(ref.core(), s);
			Stutter stutter_a{cw::kAllocator};
			Stutter stutter_b{cw::kAllocator};
			if (w.stutter) {
				cw::begin(stutter_a, s);
				cw::begin(stutter_b, s);
			}
			Scratch scratch_a;
			Scratch scratch_b;
			for (int b = 0; b < cw::kBlocks && found.empty(); ++b) {
				const std::size_t frames = cw::kBlockFrames[b % cw::kBlockFrames.size()];
				const bool rendered = cw::sounding(w, b);
				const ClipChainInputs in{
				    .fx = cw::fx_inputs(w, s),
				    .bitcrush = cw::kBitcrush,
				    .filter = cw::kFilter,
				    .filter_scratch = {},
				    .volume_post_fx = 0x0A000000,
				    .reverb_send = 0x08000000,
				    .pan = w.stereo ? 0x10000000 : 0,
				    .sidechain_on = w.sidechain,
				    .sidechain_hit = w.sidechain && b % 7 == 0 ? 0x40000000 : 0,
				    .sidechain_shape = -601295438,
				    .sidechain_depth = (0x40000000 >> 2) + 536870912,
				    .tick_inverse = 0x00800000,
				    .post_reverb_volume_neutral = 134217728,
				    .compressor_threshold = w.threshold,
				    .stutter = nullptr,
				    .stutter_rate = cw::kStutterRate,
				};
				Produced a;
				Produced r;
				const uint32_t noise = jcong; // the filters' and grain's shared stream: each side starts from it
				for (auto [chain, side, stutter, scratch] :
				     {std::tuple{&run, &a, &stutter_a, &scratch_a}, std::tuple{&ref, &r, &stutter_b, &scratch_b}}) {
					jcong = noise;
					ClipChainInputs mine = in;
					mine.fx.delay = cw::delay_state(chain->core(), w, s, rendered);
					mine.stutter = w.stutter ? stutter : nullptr;
					mine.filter_scratch = scratch->view();
					std::vector<StereoSample> block = cw::input(w, b, frames);
					std::vector<int32_t> send(frames, 0);
					auto tap = [side](std::span<const StereoSample> seen) {
						side->seen = words(seen);
						++side->calls;
					};
					if (chain == &run) {
						ClipBlock levels = run.begin_block(mine, frames);
						side->sent = run.process(block, mine, levels, rendered, send, tap);
					}
					else {
						side->sent = chain_reference::clip(ref.core(), ref.filter(), ref.saturation(), mine, block,
						                                   rendered, send, tap);
					}
					side->output = words(block);
					side->send = send;
					side->noise = jcong;
				}
				found = first_difference(w.name, b, a, r);
			}
			cw::release(run.core());
			cw::release(ref.core());
		}
		if (!found.empty()) {
			return found;
		}
	}
	return {};
}

/// @return The first difference between MasterChain and the reference over every workload, or empty.
std::string master_difference() {
	for (const cw::Workload& w : cw::kWorkloads) {
		const EffectStates s = cw::settings(w);
		DelayArena arena_a{cw::kArenaMemory};
		DelayArena arena_b{cw::kArenaMemory};
		std::string found;
		{
			MasterChain run{cw::kAllocator, cw::kAllocator, arena_a};
			MasterChain ref{cw::kAllocator, cw::kAllocator, arena_b};
			cw::hand_over(run.core(), s);
			cw::hand_over(ref.core(), s);
			Stutter stutter_a{cw::kAllocator};
			Stutter stutter_b{cw::kAllocator};
			if (w.stutter) {
				cw::begin(stutter_a, s);
				cw::begin(stutter_b, s);
			}
			Scratch scratch_a;
			Scratch scratch_b;
			for (int b = 0; b < cw::kBlocks && found.empty(); ++b) {
				const std::size_t frames = cw::kBlockFrames[b % cw::kBlockFrames.size()];
				const MasterChainInputs in{
				    .fx = cw::fx_inputs(w, s),
				    .volume_post_fx = 0x0A000000,
				    .post_reverb_volume = 134217728,
				    .reverb_send = 0x04000000,
				};
				Produced a;
				Produced r;
				const uint32_t noise = jcong; // the filters' and grain's shared stream: each side starts from it
				for (auto [chain, side, stutter, scratch] :
				     {std::tuple{&run, &a, &stutter_a, &scratch_a}, std::tuple{&ref, &r, &stutter_b, &scratch_b}}) {
					jcong = noise;
					MasterChainInputs mine = in;
					mine.fx.delay = cw::delay_state(chain->core(), w, s, true);
					const MasterPostInputs post{
					    .filter = cw::kFilter,
					    .filter_scratch = scratch->view(),
					    .bitcrush = cw::kBitcrush,
					    .master_volume = 167763968,
					    .do_pan = w.stereo,
					    .pan_l = 0x30000000,
					    .pan_r = 0x10000000,
					    .song_volume = 0x40000000,
					    .stutter = w.stutter ? stutter : nullptr,
					    .stutter_rate = cw::kStutterRate,
					};
					std::vector<StereoSample> block = cw::input(w, b, frames);
					std::vector<int32_t> send(frames, 0);
					auto between = [&, side](bool sent) -> std::optional<MasterPostInputs> {
						side->seen = words(block);
						side->seen.push_back(sent ? 1 : 0);
						++side->calls;
						return w.post ? std::optional{post} : std::nullopt;
					};
					side->sent = chain == &run
					                 ? run.render(block, mine, send, between)
					                 : chain_reference::master(ref.core(), ref.filter(), mine, block, send, between);
					side->output = words(block);
					side->send = send;
					side->noise = jcong;
				}
				found = first_difference(w.name, b, a, r);
				if (found.empty() && a.calls != 1) {
					found = std::string{w.name} + " block " + std::to_string(b) + ": between ran "
					        + std::to_string(a.calls) + " times";
				}
			}
			cw::release(run.core());
			cw::release(ref.core());
		}
		if (!found.empty()) {
			return found;
		}
	}
	return {};
}

/// @return @p home's chain.h order without @p excluded.
std::vector<std::string_view> chain_order(Home home, std::initializer_list<std::string_view> excluded) {
	std::vector<std::string_view> out;
	for (std::string_view effect : chain_effects(home)) {
		if (std::ranges::find(excluded, effect) == excluded.end()) {
			out.push_back(effect);
		}
	}
	return out;
}

std::vector<std::string_view> as_vector(std::span<const std::string_view> order) {
	return {order.begin(), order.end()};
}

} // namespace

// clang-format off
describe chain_runtime("the effect chain runtimes", ${
	it("render a sound's chain as the reference does, over every workload", _ {
		expect(sound_difference()).to_equal(std::string{});
	});

	it("render a clip chain as the reference does, over every workload", _ {
		expect(clip_difference()).to_equal(std::string{});
	});

	it("render the master around the reverb as the reference does, over every workload", _ {
		expect(master_difference()).to_equal(std::string{});
	});

	it("render a clip chain's effects in chain.h's order, the sidechain's duck before them", _ {
		expect(as_vector(ClipChain::order())).to_equal(chain_order(Home::CLIP, {"sidechain"}));
	});

	it("render the master's effects in chain.h's order, its saturation and sidechain unrendered", _ {
		expect(as_vector(MasterChain::order())).to_equal(chain_order(Home::MASTER, {"saturation", "sidechain"}));
	});

	it("return the reverb to the master before the effect chain.h places after it", _ {
		const std::optional<uint32_t> at = reverb_return(Home::MASTER);
		expect(at.has_value()).to_be_true();
		expect(MasterChain::order()[*at]).to_equal(chain_effects(Home::MASTER)[*at]);
	});

	it("render a sound's effects in a clip chain's order, without the filter and saturation its voices hold", _ {
		expect(as_vector(SoundChain::order())).to_equal(chain_order(Home::CLIP, {"saturation", "filter", "sidechain"}));
	});
});
// clang-format on

CPPSPEC_SPEC(chain_runtime)
