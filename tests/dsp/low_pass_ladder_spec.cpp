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

// LowPassLadder: the transistor-ladder low-pass as a processor kernel, `Mono` and `Stereo`, with its noise a policy.
// Against the reference (reference_filters.h) over every ladder workload (filter_workloads.h): each block's
// output, the gain, the CONG draws (count and end state per block) and the whole state, at block lengths cycling 1–9,
// 24, 64 and 128, and what it renders must equal the record. Every mode change between the four modes carries the
// state, and a seeded stream shows the draws are left before right in every frame.

#include "dsp-next/fixed/filter/low_pass_ladder.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/noise/cong_noise.h"
#include "dsp-next/fixed/noise/congruential_step.h"
#include "dsp-next/fixed/noise/noise.h"
#include "filter_workload_records.h"
#include "filter_workloads.h"
#include "kernel_filters.h"
#include "reference_filters.h"
#include "util/waves.h" // jcong, CONG

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp::fixed;

namespace {

constexpr int32_t kGain = 536870912;

/// Two sines (3 and 11 cycles per 64 samples, at 2^25 and 2^24): a mono block, or 32 interleaved L/R frames.
const std::vector<int32_t> kInput{
    0,         24536521,  32591589,  24293472,  15500126,  20423895,  36182765,  45647162,  35589850,  10947281,
    -9908702,  -13932263, -6420363,  -4590277,  -18578569, -40018314, -50331648, -40018314, -18578569, -4590277,
    -6420363,  -13932263, -9908702,  10947281,  35589850,  45647162,  36182765,  20423895,  15500126,  24293472,
    32591589,  24536521,  0,         -24536521, -32591589, -24293472, -15500126, -20423895, -36182765, -45647162,
    -35589850, -10947281, 9908702,   13932263,  6420363,   4590277,   18578569,  40018314,  50331648,  40018314,
    18578569,  4590277,   6420363,   13932263,  9908702,   -10947281, -35589850, -45647162, -36182765, -20423895,
    -15500126, -24293472, -32591589, -24536521};

constexpr FilterConfig kMid{.frequency = 1 << 24, .resonance = 1 << 28, .morph = 1 << 27, .allow_oversampling = true};
constexpr FilterConfig kBright{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .allow_oversampling = true};

/// The bright setting with the drive ladder's oversampling not allowed (under CPU load).
constexpr FilterConfig kBrightUnderLoad{
    .frequency = 50000000, .resonance = 500000000, .morph = 1 << 28, .allow_oversampling = false};

using deluge::dsp::Mono;
using deluge::dsp::q31;
using deluge::dsp::Stereo;
using deluge::dsp::StereoSample;
using filter_workloads::WorkloadId;
using filters_reference::ReferenceLowPassLadder;
using Ladder = LowPassLadder<CongNoise>;

static_assert(deluge::dsp::processor_kernel<Ladder>);
static_assert(deluge::dsp::moded_kernel<Ladder>);
static_assert(!std::is_polymorphic_v<Ladder>);
static_assert(deluge::dsp::processor<Mono<Ladder>>);
static_assert(deluge::dsp::processor<Stereo<Ladder>>);

/// Every ladder workload: each mode steady, drive oversampling and not, the mode walk, the oversampling toggle and the
/// fade, mono and stereo.
constexpr std::array<WorkloadId, 16> kLadderWorkloads{
    WorkloadId::LP12_MONO,           WorkloadId::LP12_STEREO,
    WorkloadId::LP24_MONO,           WorkloadId::LP24_STEREO,
    WorkloadId::LPDRIVE_MONO,        WorkloadId::LPDRIVE_STEREO,
    WorkloadId::LPDRIVE_OS_MONO,     WorkloadId::LPDRIVE_OS_STEREO,
    WorkloadId::LPDRIVE_OS_OFF_MONO, WorkloadId::LPDRIVE_OS_OFF_STEREO,
    WorkloadId::LP_MODE_WALK_MONO,   WorkloadId::LP_MODE_WALK_STEREO,
    WorkloadId::LP_OS_TOGGLE_MONO,   WorkloadId::LP_OS_TOGGLE_STEREO,
    WorkloadId::LP_FADE_MONO,        WorkloadId::LP_FADE_STEREO,
};

/// @return Each of @p ids where the kernel parts from the reference, named with the block and what parted.
std::vector<std::string> partings(std::span<const WorkloadId> ids) {
	std::vector<std::string> parted;
	for (const WorkloadId id : ids) {
		const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
		const auto parting = kernel_filters::first_parting(workload);
		if (parting) {
			parted.push_back(std::string{workload.name} + " block " + std::to_string(parting->block) + ": "
			                 + parting->what);
		}
	}
	return parted;
}

/// One block's settings: the ladder's mode, and the config that with it picks the drive ladder's oversampling.
struct Setting {
	LowPassLadderMode mode;
	FilterConfig config;
};

/// Every effective mode: 12dB, 24dB, drive at the rate, and drive oversampled (the same config, oversampling allowed).
const std::array<Setting, 4> kSettings{{
    {.mode = LowPassLadderMode::TRANSISTOR_12DB, .config = kMid},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB, .config = kBright},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB_DRIVE, .config = kBrightUnderLoad},
    {.mode = LowPassLadderMode::TRANSISTOR_24DB_DRIVE, .config = kBright},
}};

/// The cutoffs the coefficient checks sweep: dark to bright, with the drive ladder's oversampling range (a log cutoff
/// past 51 on quick_log's scale, two units per octave from 2^24 at 52) stepped through every entry of its threshold
/// and limit tables from 52 to 56 and the interior of every segment between them (21e6 at 52.6, 30e6 at 53.6, 40e6
/// and 50e6 at 54.4 and 55.0, 55e6 at 55.4), then up to ONE_Q31.
constexpr std::array<q31, 15> kSweepFrequencies{
    q31{1 << 20},   q31{1 << 22},  q31{1 << 24},   q31{21000000}, q31{30000000},
    q31{1 << 25},   q31{40000000}, q31{50000000},  q31{55000000}, q31{1 << 26},
    q31{100000000}, q31{1 << 27},  q31{200000000}, q31{1 << 28},  q31{ONE_Q31},
};

/// @return The resonances (Q29) just either side of where the reference's drive ladder starts oversampling at
/// @p frequency (the last resonance that does not and the first two that do), or none if it never or always does.
std::vector<q31> threshold_resonances(q31 frequency) {
	const auto oversamples = [frequency](q31 resonance) {
		ReferenceLowPassLadder reference;
		std::ignore = reference.configure(
		    kGain, {.frequency = frequency, .resonance = resonance, .morph = 0, .allow_oversampling = true},
		    LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		return reference.oversample();
	};
	q31 below = 0;
	q31 above = 536870911;
	if (oversamples(below) || !oversamples(above)) {
		return {};
	}
	while (above - below > 1) {
		const q31 middle = below + ((above - below) / 2);
		(oversamples(middle) ? above : below) = middle;
	}
	return {below, above, above + 1};
}

/// The configs the coefficient checks sweep: each cutoff with resonance off, low, mid, high, past the limits and either
/// side of the oversampling threshold, morph on and off, each with oversampling allowed and not.
std::vector<FilterConfig> sweep_configs() {
	std::vector<FilterConfig> configs;
	for (const q31 frequency : kSweepFrequencies) {
		// 1 << 26 keeps the drive ladder, whose resonance is not capped, below the input's saturation threshold.
		std::vector<q31> resonances{0, 1 << 26, 1 << 27, 400000000, 500000000, 536870911};
		for (const q31 resonance : threshold_resonances(frequency)) {
			resonances.push_back(resonance);
		}
		for (const q31 resonance : resonances) {
			for (const q31 morph : {q31{0}, q31{1 << 28}}) {
				for (const bool allow : {true, false}) {
					configs.push_back(
					    {.frequency = frequency, .resonance = resonance, .morph = morph, .allow_oversampling = allow});
				}
			}
		}
	}
	return configs;
}

/// A channel's follower and its four sections' memories, Q31.
using ChannelWords = std::array<q31, 5>;

template <class Noise>
ChannelWords words(const typename LowPassLadder<Noise>::State& s) {
	return {s.noise.y, s.sections[0].z, s.sections[1].z, s.sections[2].z, s.sections[3].z};
}

ChannelWords words(const ReferenceLowPassLadder::Channel& c) {
	return {c.noise_last, c.sections[0].memory(), c.sections[1].memory(), c.sections[2].memory(),
	        c.sections[3].memory()};
}

/// @return How many steps of the noise generator take @p from to @p to (at most @p limit), or limit + 1.
uint32_t draws_between(uint32_t from, uint32_t to, uint32_t limit) {
	uint32_t n = 0;
	while (from != to && n <= limit) {
		from = CongruentialStep::next(from);
		++n;
	}
	return n;
}

ChannelWords words(const Ladder::State& s) {
	return words<CongNoise>(s);
}

/// The stream the seeded policy draws from.
SeededNoise seeded_stream{filter_workloads::kCongSeed};

/// A noise policy drawing from @ref seeded_stream.
struct SeededPolicy {
	[[gnu::always_inline]] static q31 render() { return seeded_stream.render(); }
};

using SeededLadder = LowPassLadder<SeededPolicy>;

/// The kernel's settings for @p setting at the test gain.
Ladder::Settings settings_for(const Setting& setting) {
	return {.gain = kGain, .config = setting.config, .mode = setting.mode};
}

/// kInput as interleaved stereo frames.
std::vector<StereoSample> input_frames() {
	std::vector<StereoSample> frames(kInput.size() / 2);
	for (std::size_t i = 0; i < frames.size(); ++i) {
		frames[i] = {.l = kInput[2 * i], .r = kInput[(2 * i) + 1]};
	}
	return frames;
}

} // namespace

// clang-format off
describe low_pass_ladder("LowPassLadder", ${
	it("renders every ladder workload as the reference does, mono and stereo: each block's output, gain and CONG "
	   "draws, and the whole state", _ {
		expect(partings(kLadderWorkloads)).to_equal(std::vector<std::string>{});
	});

	it("renders every ladder workload as recorded, the CONG draws per block included", _ {
		for (const WorkloadId id : kLadderWorkloads) {
			const filter_workloads::FilterWorkload& workload = filter_workloads::filter_workload(id);
			expect(kernel_filters::record(workload)).to_equal(filter_workloads::kRecorded.at(static_cast<size_t>(id)));
		}
	});

	it("configures every coefficient, chooses oversampling and compensates the gain as the reference's configure "
	   "does", _ {
		std::size_t oversampled = 0;
		std::size_t at_rate = 0;
		std::size_t thresholds = 0;
		for (const q31 frequency : kSweepFrequencies) {
			thresholds += threshold_resonances(frequency).empty() ? 0 : 1;
		}
		for (const FilterConfig& config : sweep_configs()) {
			for (const LowPassLadderMode mode : {LowPassLadderMode::TRANSISTOR_12DB, LowPassLadderMode::TRANSISTOR_24DB,
			                                     LowPassLadderMode::TRANSISTOR_24DB_DRIVE}) {
				ReferenceLowPassLadder reference;
				reference.zero();
				const q31 gain = reference.configure(kGain, config, mode);
				const Ladder::Coefficients c = Ladder::coefficients({.gain = kGain, .config = config, .mode = mode});
				expect(Ladder::gain(c)).to_equal(gain);
				Ladder::Ladder expected = Ladder::Ladder::T12;
				if (mode == LowPassLadderMode::TRANSISTOR_24DB) {
					expected = Ladder::Ladder::T24;
				}
				else if (mode == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
					expected = reference.oversample() ? Ladder::Ladder::DRIVE_OVERSAMPLED : Ladder::Ladder::DRIVE;
					if (reference.oversample()) {
						++oversampled;
					}
					else {
						++at_rate;
					}
				}
				expect(Ladder::ladder_of(c.mode)).to_equal(expected);

				// Every coefficient: the adapter's digest folds them all, as the reference's folds its members.
				kernel_filters::KernelLowPassLadder<false> kernel;
				kernel.zero();
				expect(kernel.configure(kGain, config, mode)).to_equal(gain);
				filter_workloads::Digest kernel_digest;
				filter_workloads::Digest reference_digest;
				kernel.digest(kernel_digest);
				reference.digest(reference_digest);
				expect(kernel_digest.value).to_equal(reference_digest.value);
			}
		}
		expect(oversampled).to_be_greater_than(0u);
		expect(at_rate).to_be_greater_than(0u);
		// The threshold falls inside the resonance range from log cutoff 52 to 56, in every table segment.
		expect(thresholds).to_be_greater_than(6u);
	});

	it("draws one CONG value per channel per sample, two when the drive ladder oversamples, and renders as the "
	   "reference does", _ {
		// The drive settings at the rate and oversampled, as the reference chooses them.
		ReferenceLowPassLadder at_rate;
		std::ignore = at_rate.configure(kGain, kSettings[2].config, kSettings[2].mode);
		expect(kSettings[2].mode).to_equal(LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(at_rate.oversample()).to_be_false();
		ReferenceLowPassLadder oversampled;
		std::ignore = oversampled.configure(kGain, kSettings[3].config, kSettings[3].mode);
		expect(kSettings[3].mode).to_equal(LowPassLadderMode::TRANSISTOR_24DB_DRIVE);
		expect(oversampled.oversample()).to_be_true();
		expect(Ladder::ladder_of(Ladder::coefficients(settings_for(kSettings[2])).mode)).to_equal(Ladder::Ladder::DRIVE);
		expect(Ladder::ladder_of(Ladder::coefficients(settings_for(kSettings[3])).mode))
		    .to_equal(Ladder::Ladder::DRIVE_OVERSAMPLED);

		std::vector<q31> scratch(kInput.size());
		for (const Setting& setting : kSettings) {
			const uint32_t per_sample =
			    Ladder::ladder_of(Ladder::coefficients(settings_for(setting)).mode) == Ladder::Ladder::DRIVE_OVERSAMPLED ? 2
			                                                                                                        : 1;
			for (const bool stereo : {false, true}) {
				ReferenceLowPassLadder reference;
				reference.zero();
				std::ignore = reference.configure(kGain, setting.config, setting.mode);
				std::vector<q31> expected = kInput;
				filter_workloads::reset_cong();
				if (stereo) {
					reference.renderStereo(expected, scratch);
				}
				else {
					reference.renderMono(expected, scratch);
				}
				const uint32_t reference_end = jcong;

				std::vector<q31> out = kInput;
				filter_workloads::reset_cong();
				if (stereo) {
					Stereo<Ladder> ladder{settings_for(setting)};
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);
				}
				else {
					Mono<Ladder> ladder{settings_for(setting)};
					ladder.renderBlock(out, out);
				}
				const uint32_t limit = 4 * static_cast<uint32_t>(kInput.size());
				expect(draws_between(filter_workloads::kCongSeed, jcong, limit))
				    .to_equal(per_sample * static_cast<uint32_t>(kInput.size()));
				expect(jcong).to_equal(reference_end);
				expect(out).to_equal(expected);
			}
		}
		filter_workloads::reset_cong();
	});

	it("takes the input form its config calls for, every ladder in every form rendering as the reference does, full "
	   "scale included", _ {
		std::array<bool, 7> reached{};
		// Full-scale samples either side of the wrap, where the unresonant form's rounding is checked hardest.
		std::vector<q31> full_scale;
		for (const q31 x : {INT32_MIN, INT32_MIN + 1, INT32_MIN + 2, -3, -2, -1, 0, 1, 2, 3, INT32_MAX - 2,
		                    INT32_MAX - 1, INT32_MAX}) {
			full_scale.insert(full_scale.end(), {x, x});
		}
		const std::vector<q31>& extremes = full_scale;
		std::vector<q31> scratch(std::max(kInput.size(), extremes.size()));
		for (const FilterConfig& config : sweep_configs()) {
			for (const LowPassLadderMode mode : {LowPassLadderMode::TRANSISTOR_12DB, LowPassLadderMode::TRANSISTOR_24DB,
			                                     LowPassLadderMode::TRANSISTOR_24DB_DRIVE}) {
				const Ladder::Settings settings{.gain = kGain, .config = config, .mode = mode};
				const Ladder::Coefficients c = Ladder::coefficients(settings);
				reached[static_cast<std::size_t>(c.mode)] = true;

				const bool unresonant = c.morph <= 0 && c.processed_resonance == 0
				                        && c.divide_by_total_moveability_and_processed_resonance == (q31{1} << 30);
				const Ladder::Input expected = unresonant ? Ladder::Input::UNRESONANT : Ladder::Input::FED_BACK;
				expect(Ladder::input_of(c.mode)).to_equal(expected);

				for (const auto& [input, stereo] : {std::pair{&kInput, false}, std::pair{&kInput, true},
				                                    std::pair{&extremes, false}, std::pair{&extremes, true}}) {
					ReferenceLowPassLadder reference;
					reference.zero();
					std::ignore = reference.configure(kGain, config, mode);
					std::vector<q31> want = *input;
					filter_workloads::reset_cong();
					if (stereo) {
						reference.renderStereo(want, scratch);
					}
					else {
						reference.renderMono(want, scratch);
					}
					const uint32_t reference_end = jcong;

					std::vector<q31> out = *input;
					filter_workloads::reset_cong();
					if (stereo) {
						Stereo<Ladder> ladder{settings};
						const auto frames = deluge::dsp::as_frames(out);
						ladder.renderBlock(frames, frames);
					}
					else {
						Mono<Ladder> ladder{settings};
						ladder.renderBlock(out, out);
					}
					expect(jcong).to_equal(reference_end);
					expect(out).to_equal(want);
				}
			}
		}
		filter_workloads::reset_cong();
		for (std::size_t m = 0; m < reached.size(); ++m) {
			expect(reached[m]).to_be_true();
		}
	});

	it("draws for the left channel before the right in every frame, in every mode (a seeded stream)", _ {
		const std::vector<StereoSample> input = input_frames();
		for (const Setting& setting : kSettings) {
			const SeededLadder::Settings settings{.gain = kGain, .config = setting.config, .mode = setting.mode};
			const uint32_t per_sample =
			    SeededLadder::ladder_of(SeededLadder::coefficients(settings).mode) == SeededLadder::Ladder::DRIVE_OVERSAMPLED
			        ? 2
			        : 1;

			seeded_stream = SeededNoise{filter_workloads::kCongSeed};
			Stereo<SeededLadder> stereo{settings};
			std::vector<StereoSample> out(input.size());
			stereo.renderBlock(input, out);
			const uint32_t stereo_end = seeded_stream.state();

			// The same draws, made by two mono ladders stepped a frame at a time: the left channel's, then the right's.
			seeded_stream = SeededNoise{filter_workloads::kCongSeed};
			Mono<SeededLadder> left{settings};
			Mono<SeededLadder> right{settings};
			std::vector<StereoSample> expected(input.size());
			for (std::size_t i = 0; i < input.size(); ++i) {
				expected[i].l = left.render(input[i].l);
				expected[i].r = right.render(input[i].r);
			}
			expect(stereo_end).to_equal(seeded_stream.state());
			expect(draws_between(filter_workloads::kCongSeed, stereo_end, 4 * static_cast<uint32_t>(kInput.size())))
			    .to_equal(per_sample * static_cast<uint32_t>(kInput.size()));
			for (std::size_t i = 0; i < input.size(); ++i) {
				expect(out[i].l).to_equal(expected[i].l);
				expect(out[i].r).to_equal(expected[i].r);
			}
			expect(words<SeededPolicy>(stereo.left())).to_equal(words<SeededPolicy>(left.state()));
			expect(words<SeededPolicy>(stereo.right())).to_equal(words<SeededPolicy>(right.state()));
		}
	});

	it("carries each channel's state across every mode change, oversampling switched on and off, as the reference "
	   "does", _ {
		std::vector<q31> scratch(kInput.size());
		for (const Setting& from : kSettings) {
			for (const Setting& to : kSettings) {
				ReferenceLowPassLadder reference;
				reference.zero();
				Stereo<Ladder> ladder;
				uint32_t reference_cong = filter_workloads::kCongSeed;
				uint32_t kernel_cong = filter_workloads::kCongSeed;
				for (const Setting& setting : {from, to, from}) {
					std::vector<q31> expected = kInput;
					jcong = reference_cong;
					std::ignore = reference.configure(kGain, setting.config, setting.mode);
					reference.renderStereo(expected, scratch);
					reference_cong = jcong;

					const Ladder::State left = ladder.left();
					const Ladder::State right = ladder.right();
					ladder.configure(settings_for(setting));
					expect(words(ladder.left())).to_equal(words(left));
					expect(words(ladder.right())).to_equal(words(right));

					std::vector<q31> out = kInput;
					jcong = kernel_cong;
					const auto frames = deluge::dsp::as_frames(out);
					ladder.renderBlock(frames, frames);
					kernel_cong = jcong;

					expect(out).to_equal(expected);
					expect(kernel_cong).to_equal(reference_cong);
					expect(words(ladder.left())).to_equal(words(reference.left()));
					expect(words(ladder.right())).to_equal(words(reference.right()));
				}
			}
		}
		filter_workloads::reset_cong();
	});

	it("silences the sections on reset and keeps the noise follower, as the reference's reset does", _ {
		std::vector<q31> scratch(kInput.size());
		ReferenceLowPassLadder reference;
		reference.zero();
		std::ignore = reference.configure(kGain, kMid, LowPassLadderMode::TRANSISTOR_24DB);
		std::vector<q31> expected = kInput;
		filter_workloads::reset_cong();
		reference.renderMono(expected, scratch);
		reference.reset(false);

		Mono<Ladder> ladder{Ladder::Settings{.gain = kGain, .config = kMid, .mode = LowPassLadderMode::TRANSISTOR_24DB}};
		std::vector<q31> out = kInput;
		filter_workloads::reset_cong();
		ladder.renderBlock(out, out);
		ladder.reset();

		expect(ladder.state().noise.y).not_().to_equal(0);
		expect(words(ladder.state())).to_equal(words(reference.left()));
		expect(words(ladder.state())).to_equal(ChannelWords{ladder.state().noise.y, 0, 0, 0, 0});
		filter_workloads::reset_cong();
	});
});
// clang-format on

CPPSPEC_MAIN(low_pass_ladder);
