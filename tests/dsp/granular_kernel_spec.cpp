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

// The Granular kernel: the granular effect as one frame kernel under Mono, rendered in place. Driven as its owner
// drives it (TestGranular below: the ring handed over once and kept, a release a restart, the shutdown armed, the
// knobs configured and the reverb send set each rendered block), it must equal today's granular (reference_granular.h)
// bit for bit on every workload (granular_workloads.h): each block's output, post-FX volume and reverb send, and at
// the end the State and the ring as the kernel reads it.

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/granular/granular_kernel.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "granular_workloads.h"
#include "reference_granular.h"

#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

using deluge::dsp::Mono;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CongNoise;
using Granular = deluge::dsp::fixed::Granular<CongNoise>;

static_assert(deluge::dsp::processor_kernel<Granular>);
static_assert(Granular::kRingFrames == kModFXGrainBufferSize);

namespace {

namespace gw = deluge::dsp::test::granular_workloads;
using deluge::dsp::test::ReferenceGranular;

/// @return The words of @p frames, left then right per frame.
[[nodiscard]] std::vector<uint32_t> frame_words(std::span<const StereoSample> frames) {
	static_assert(sizeof(StereoSample) == 2 * sizeof(uint32_t));
	std::vector<uint32_t> words(frames.size() * 2);
	std::memcpy(words.data(), frames.data(), frames.size_bytes());
	return words;
}

/// @brief Appends a grain's fields to @p words, in the kernel's Grain order.
void push_grain(std::vector<uint32_t>& words, int32_t length, int32_t start, int32_t counter, uint16_t pitch,
                int32_t vol_scale, int32_t vol_scale_max, bool reverse, int32_t pan_l, int32_t pan_r) {
	for (const int32_t w :
	     {length, start, counter, int32_t{pitch}, vol_scale, vol_scale_max, int32_t{reverse}, pan_l, pan_r}) {
		words.push_back(static_cast<uint32_t>(w));
	}
}

/// @brief `Mono<Granular<CongNoise>>` as its owner drives it, holding the ring it hands the kernel, with the
/// reference's calls so that `granular_workloads::play` drives it.
class TestGranular {
public:
	/// @brief Today's clearGrainFXBuffer: the grains stopped, the write index back to the ring's start.
	void clearGrainFXBuffer() { mono_.on<&Granular::clear>(); }

	/// @brief Today's startSkippingRendering: the ring released, so it restarts.
	void startSkippingRendering() { mono_.on<&Granular::restart>(); }

	/// @brief Today's processGrainFX: renders @p frames in place while there is sound or the countdown runs, adding
	/// the reverb feed into @p send and scaling @p volume (Q31).
	void processGrainFX(std::span<StereoSample> frames, int32_t rate, int32_t mix, int32_t density, int32_t randomness,
	                    int32_t* volume, bool sound, q31_t reverb_amount, std::span<q31_t> send) {
		if (sound || mono_.state().wraps_to_shutdown >= 0) {
			if (mono_.state().ring == nullptr) {
				mono_.on<&Granular::set_ring>(ring_.data());
			}
			if (sound) {
				mono_.on<&Granular::arm_shutdown>();
			}
			mono_.configure({.rate = rate, .mix = mix, .density = density, .randomness = randomness});
			*volume = Granular::post_fx_volume(*volume);
			mono_.on<&Granular::set_send>(send.data(), reverb_amount);
			mono_.renderBlock(frames, frames);
			if (mono_.state().wraps_to_shutdown < 0) {
				mono_.on<&Granular::restart>();
			}
		}
		mono_.on<&Granular::note_full>();
	}

	/// @return The ring as the kernel reads it: a slot not written since the ring was taken up reads zero.
	[[nodiscard]] std::vector<uint32_t> ring_words() const {
		std::vector<StereoSample> read(ring_.begin(), ring_.end());
		for (std::size_t i = mono_.state().written; i < read.size(); ++i) {
			read[i] = {0, 0};
		}
		return frame_words(read);
	}

	/// @return The State the output depends on, as words (see @ref reference_state_words for the order).
	[[nodiscard]] std::vector<uint32_t> state_words() const {
		const Granular::State& s = mono_.state();
		std::vector<uint32_t> words{s.write, static_cast<uint32_t>(s.full), static_cast<uint32_t>(s.initialized),
		                            static_cast<uint32_t>(s.wraps_to_shutdown)};
		for (const Granular::Grain& g : s.grains) {
			push_grain(words, g.length, g.start, g.counter, g.pitch, g.vol_scale, g.vol_scale_max, g.reverse, g.pan_l,
			           g.pan_r);
		}
		for (const int32_t w : {s.lpf_l.z, s.lpf_r.z, s.levels.size, s.levels.period, s.levels.wet, s.levels.dry,
		                        s.levels.feedback, s.knobs.rate, s.knobs.mix, s.knobs.density}) {
			words.push_back(static_cast<uint32_t>(w));
		}
		return words;
	}

private:
	Mono<Granular> mono_;
	std::vector<StereoSample> ring_ = std::vector<StereoSample>(Granular::kRingFrames);
};

/// @return @p fx's state as words: the write index, whether grains may start, whether they are free of the first
/// pass, the countdown, each grain, the wet low-pass memories, the knobs' values and the positions they came from.
[[nodiscard]] std::vector<uint32_t> reference_state_words(const ReferenceGranular& fx) {
	std::vector<uint32_t> words{fx.write_index(), static_cast<uint32_t>(fx.buffer_full()),
	                            static_cast<uint32_t>(fx.grain_initialized()),
	                            static_cast<uint32_t>(fx.wraps_to_shutdown())};
	for (const auto& g : fx.grain_states()) {
		push_grain(words, g.length, g.startPoint, g.counter, g.pitch, g.volScale, g.volScaleMax, g.rev, g.panVolL,
		           g.panVolR);
	}
	const auto lpf = fx.low_pass();
	const ReferenceGranular::Cache c = fx.cache();
	for (const int32_t w : {lpf[0].z, lpf[1].z, c.size, c.period, c.wet, c.dry, c.feedback, c.rate, c.mix, c.density}) {
		words.push_back(static_cast<uint32_t>(w));
	}
	return words;
}

/// @brief A whole run: the trace, then the State and the ring at the end.
struct RunTrace {
	gw::GranularTrace trace;
	std::vector<uint32_t> state;
	std::vector<uint32_t> ring;
};

[[nodiscard]] RunTrace run_reference(const gw::GranularWorkload& workload) {
	const auto fx = std::make_unique<ReferenceGranular>();
	RunTrace run{.trace = gw::play(workload, *fx), .state = {}, .ring = {}};
	run.state = reference_state_words(*fx);
	run.ring = frame_words(fx->ring_frames());
	return run;
}

[[nodiscard]] RunTrace run_kernel(const gw::GranularWorkload& workload) {
	const auto fx = std::make_unique<TestGranular>();
	RunTrace run{.trace = gw::play(workload, *fx), .state = {}, .ring = {}};
	run.state = fx->state_words();
	run.ring = fx->ring_words();
	return run;
}

/// @return @p v's words [@p begin, @p begin + @p n).
[[nodiscard]] std::vector<int32_t> slice(const std::vector<int32_t>& v, std::size_t begin, std::size_t n) {
	return {v.begin() + static_cast<std::ptrdiff_t>(begin), v.begin() + static_cast<std::ptrdiff_t>(begin + n)};
}

/// @return Each workload where the kernel parts from the reference, named with the first block and what parted.
[[nodiscard]] std::vector<std::string> partings() {
	std::vector<std::string> parted;
	for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
		const RunTrace want = run_reference(workload);
		const RunTrace got = run_kernel(workload);
		const auto part = [&](std::size_t b, const char* what) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(b) + ": " + what);
		};
		bool blocks_equal = true;
		std::size_t frame = 0;
		for (std::size_t b = 0; b < workload.blocks.size() && blocks_equal; ++b) {
			const std::size_t n = workload.blocks[b].n;
			if (slice(got.trace.samples, 2 * frame, 2 * n) != slice(want.trace.samples, 2 * frame, 2 * n)) {
				part(b, "output");
				blocks_equal = false;
			}
			else if (got.trace.volumes[b] != want.trace.volumes[b]) {
				part(b, "volume");
				blocks_equal = false;
			}
			else if (slice(got.trace.sends, frame, n) != slice(want.trace.sends, frame, n)) {
				part(b, "send");
				blocks_equal = false;
			}
			frame += n;
		}
		if (blocks_equal && got.state != want.state) {
			part(workload.blocks.size(), "end state");
		}
		else if (blocks_equal && got.ring != want.ring) {
			part(workload.blocks.size(), "end ring");
		}
	}
	return parted;
}

} // namespace

// clang-format off
describe granular_kernel("the Granular kernel", ${
	it("renders every workload as today's granular does, in place: each block's output, post-FX volume and reverb "
	   "send, and the State and the ring at the end", _ {
		expect(partings()).to_equal(std::vector<std::string>{});
	});

	it("renders the first block, State and ring chunk where a workload parts, compared word for word", _ {
		for (const gw::GranularWorkload& workload : gw::all_granular_workloads()) {
			const RunTrace want = run_reference(workload);
			const RunTrace got = run_kernel(workload);
			std::size_t frame = 0;
			for (const gw::GranularBlock& block : workload.blocks) {
				const std::size_t n = block.n;
				const std::size_t b = static_cast<std::size_t>(&block - workload.blocks.data());
				const std::vector<int32_t> got_out = slice(got.trace.samples, 2 * frame, 2 * n);
				const std::vector<int32_t> want_out = slice(want.trace.samples, 2 * frame, 2 * n);
				const std::vector<int32_t> got_send = slice(got.trace.sends, frame, n);
				const std::vector<int32_t> want_send = slice(want.trace.sends, frame, n);
				if (got_out != want_out || got_send != want_send || got.trace.volumes[b] != want.trace.volumes[b]) {
					expect(got_out).to_equal(want_out);
					expect(got_send).to_equal(want_send);
					expect(got.trace.volumes[b]).to_equal(want.trace.volumes[b]);
					break;
				}
				frame += n;
			}
			expect(got.state).to_equal(want.state);
			constexpr std::size_t kChunk = 1024;
			for (std::size_t w = 0; w < want.ring.size(); w += kChunk) {
				const std::vector<uint32_t> got_chunk(got.ring.begin() + static_cast<std::ptrdiff_t>(w),
				                                      got.ring.begin() + static_cast<std::ptrdiff_t>(w + kChunk));
				const std::vector<uint32_t> want_chunk(want.ring.begin() + static_cast<std::ptrdiff_t>(w),
				                                       want.ring.begin() + static_cast<std::ptrdiff_t>(w + kChunk));
				if (got_chunk != want_chunk) {
					expect(got_chunk).to_equal(want_chunk);
					break;
				}
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(granular_kernel);
