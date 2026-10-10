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

// The delay's feedback shaper. Stereo<FeedbackShaper> is pinned, sample by sample, against the delay's two shaping
// loops copied verbatim (ReferenceShaper below, over the stereo reference response in
// bench/reference_impulse_response.h): at both drives (8, a sound's, and 5, a host chain's), in both modes, at feedback
// 0, ONE_Q31, a mid value and a negative one, over 4096 seeded stereo samples at a quarter of full scale in blocks of
// every length 1 to 9. At full scale (INT32_MIN held, INT32_MAX held, the two alternating, the taps' signs reversed in
// time, and full-scale noise) the response's sums can leave q31, and the reference's response wraps its adds as the
// hardware's do. Across blocks switching analog, digital and back, the response's State is bit for bit untouched by a
// digital block, and the next analog block resumes from it.

#include "dsp-next/fixed/delay/feedback_shaper.h"

#include "bench/reference_impulse_response.h"
#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/fixed/delay/analog_impulse_response.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using deluge::bench::reference::ReferenceImpulseResponse;
using deluge::bench::reference::WrappingReferenceImpulseResponse;

namespace {

using Mode = FeedbackShaper::Mode;

static_assert(processor_kernel<FeedbackShaper>);
static_assert(moded_kernel<FeedbackShaper>);
static_assert(!std::is_polymorphic_v<FeedbackShaper>);
static_assert(processor<Stereo<FeedbackShaper>>);
static_assert(deluge::dsp::detail::plain_value<FeedbackShaper::State>
              && deluge::dsp::detail::plain_value<FeedbackShaper::Coefficients>);
static_assert(deluge::dsp::detail::first_mode_v<FeedbackShaper::Modes> == Mode::DIGITAL,
              "the default mode is listed first");
static_assert(FeedbackShaper::Coefficients{}.mode == Mode::DIGITAL);
static_assert(FeedbackShaper::coefficients({}).mode == FeedbackShaper::Coefficients{}.mode,
              "the default settings name the default mode");
static_assert(FeedbackShaper::coefficients({.analog = true}).mode == Mode::ANALOG);
static_assert(FeedbackShaper::coefficients({.feedback = 123, .drive = 5}).feedback == 123);
static_assert(FeedbackShaper::coefficients({.feedback = 123, .drive = 5}).drive == 5);
static_assert(FeedbackShaper::Settings{}.drive == 8 && FeedbackShaper::Coefficients{}.drive == 8);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The samples of the long runs.
constexpr std::size_t kLongRun = 4096;
/// The block lengths every case runs.
constexpr std::size_t kMaxLength = 9;
/// The drives: a sound's and a host chain's.
constexpr std::array<int32_t, 2> kDrives{8, 5};
/// The feedback amounts: none, full, a mid value, and a negative one.
constexpr std::array<q31, 4> kFeedbacks{0, ONE_Q31, 0x5a3c1e0f, -0x2b7d9f13};

/// The delay's per-block settings the shaping loops read, named as the delay names them.
struct ReferenceBlock {
	int32_t feedback;
	bool analog;
	int32_t analog_saturation;
};

/// The delay's feedback shaping, its two loops copied verbatim, over the impulse response @p IR.
template <typename IR>
struct ReferenceShaper {
	IR analog_ir_;

	void shapeFeedback(std::span<StereoSample> work, const ReferenceBlock& settings) {
		if (settings.analog) {
			for (StereoSample& sample : work) {
				sample = analog_ir_.process(sample);
			}
			// Less headroom than digital: the analog saturation sounds fine driven.
			const auto drive = static_cast<uint32_t>(settings.analog_saturation);
			for (StereoSample& sample : work) {
				sample.l = tanh(multiply_32x32_rshift32(sample.l, settings.feedback), drive) << 2;
				sample.r = tanh(multiply_32x32_rshift32(sample.r, settings.feedback), drive) << 2;
			}
			return;
		}

		// More headroom than analog: a pure digital clip sounds bad.
		for (StereoSample& sample : work) {
			sample.l = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.l, settings.feedback)) << 2;
			sample.r = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.r, settings.feedback)) << 2;
		}
	}
};

/// @p n stereo samples of seeded noise shifted down by @p shift, the channels from two streams.
std::vector<StereoSample> noise(std::size_t n, uint32_t seed, int shift) {
	SeededNoise left{seed};
	SeededNoise right{seed ^ 0x5bd1e995u};
	std::vector<StereoSample> v(n);
	for (StereoSample& s : v) {
		s = {.l = left.render() >> shift, .r = right.render() >> shift};
	}
	return v;
}

/// The full-scale patterns: INT32_MIN held, INT32_MAX held, the two alternating, and the taps' signs reversed in time.
constexpr int kFullScalePatterns = 4;

/// @p n stereo samples at full scale in @p pattern, the right channel opposite the left. The last pattern gives every
/// 26th sample's product with each tap the tap's sign, which drives the response's output past full scale.
std::vector<StereoSample> full_scale(std::size_t n, int pattern) {
	std::vector<StereoSample> v(n);
	for (std::size_t i = 0; i < n; ++i) {
		const std::size_t reversed = AnalogImpulseResponse::kTaps - 1 - (i % AnalogImpulseResponse::kTaps);
		const bool high = pattern == 0   ? false
		                  : pattern == 1 ? true
		                  : pattern == 2 ? i % 2 == 1
		                                 : AnalogImpulseResponse::kResponse[reversed] > 0;
		v[i] = {.l = high ? kMax : kMin, .r = high ? kMin : kMax};
	}
	return v;
}

/// @p frames as interleaved samples, left first.
std::vector<q31> interleaved(std::span<const StereoSample> frames) {
	std::vector<q31> v;
	for (const StereoSample& f : frames) {
		v.push_back(f.l);
		v.push_back(f.r);
	}
	return v;
}

/// The kernel's settings for the delay's @p block.
FeedbackShaper::Settings settings_of(const ReferenceBlock& block) {
	return {
	    .feedback = block.feedback, .analog = block.analog, .drive = static_cast<uint32_t>(block.analog_saturation)};
}

/// A shaper and its reference, run block by block over the same input.
template <typename IR>
struct Pair {
	Stereo<FeedbackShaper> kernel;
	ReferenceShaper<IR> reference;
	std::vector<q31> got;
	std::vector<q31> want;

	/// One block of @p in at @p block's settings through both.
	void block(std::span<const StereoSample> in, const ReferenceBlock& block) {
		kernel.configure(settings_of(block));
		std::vector<StereoSample> out(in.size());
		kernel.renderBlock(in, out);
		std::vector<StereoSample> work(in.begin(), in.end());
		reference.shapeFeedback(work, block);
		const std::vector<q31> k = interleaved(out);
		const std::vector<q31> r = interleaved(work);
		got.insert(got.end(), k.begin(), k.end());
		want.insert(want.end(), r.begin(), r.end());
	}

	/// @p in in blocks of @p length at @p settings, the last block short.
	void run(const std::vector<StereoSample>& in, std::size_t length, const ReferenceBlock& settings) {
		for (std::size_t start = 0; start < in.size(); start += length) {
			block(std::span<const StereoSample>{in}.subspan(start, std::min(length, in.size() - start)), settings);
		}
	}
};

/// @p s's response history and its index, the whole of its State, for comparing States bit for bit.
std::vector<q31> history_of(const FeedbackShaper::State& s) {
	static_assert(sizeof(FeedbackShaper::State) == sizeof(s.response.history) + sizeof(s.response.oldest),
	              "the history and its index are the whole State");
	std::vector<q31> v{s.response.history.begin(), s.response.history.end()};
	v.push_back(static_cast<q31>(s.response.oldest));
	return v;
}

} // namespace

// clang-format off
describe feedback_shaper("FeedbackShaper", ${
	it("equals the delay's shaping at both drives, both modes and every feedback, over 4096 seeded samples", _ {
		const std::vector<StereoSample> in = noise(kLongRun, 0x2468ace1, 2);
		for (const int32_t drive : kDrives) {
			for (const bool analog : {true, false}) {
				for (const q31 feedback : kFeedbacks) {
					for (std::size_t length = 1; length <= kMaxLength; ++length) {
						Pair<ReferenceImpulseResponse> pair;
						pair.run(in, length, {.feedback = feedback, .analog = analog, .analog_saturation = drive});
						expect(pair.got).to_equal(pair.want);
					}
				}
			}
		}
	});

	it("equals the delay's shaping at full scale, the response's adds wrapping", _ {
		std::vector<std::vector<StereoSample>> inputs{noise(kLongRun, 0x13579bdf, 0)};
		for (int pattern = 0; pattern < kFullScalePatterns; ++pattern) {
			inputs.push_back(full_scale(64, pattern));
		}
		for (const std::vector<StereoSample>& in : inputs) {
			for (const int32_t drive : kDrives) {
				for (const bool analog : {true, false}) {
					for (const q31 feedback : kFeedbacks) {
						for (const std::size_t length : {std::size_t{1}, std::size_t{4}, kMaxLength}) {
							Pair<WrappingReferenceImpulseResponse> pair;
							pair.run(in, length,
							         {.feedback = feedback, .analog = analog, .analog_saturation = drive});
							expect(pair.got).to_equal(pair.want);
						}
					}
				}
			}
		}
	});

	it("leaves the response untouched in digital, and resumes it in the next analog block", _ {
		const std::array<bool, 7> analog{true, false, false, true, false, true, true};
		for (const int32_t drive : kDrives) {
			for (std::size_t length = 1; length <= kMaxLength; ++length) {
				Pair<ReferenceImpulseResponse> pair;
				for (std::size_t block = 0; block < analog.size(); ++block) {
					const std::vector<StereoSample> in =
					    noise(length, 0x7e57 + static_cast<uint32_t>((block * 17) + length), 2);
					const FeedbackShaper::State left = pair.kernel.left();
					const FeedbackShaper::State right = pair.kernel.right();
					pair.block(in, {.feedback = kFeedbacks[2], .analog = analog[block], .analog_saturation = drive});
					if (!analog[block]) {
						expect(history_of(pair.kernel.left())).to_equal(history_of(left));
						expect(history_of(pair.kernel.right())).to_equal(history_of(right));
					}
				}
				expect(pair.got).to_equal(pair.want);
			}
		}
	});

	it("resumes from the response's tail: a fresh response after the digital blocks gives other output", _ {
		const std::vector<StereoSample> first = noise(8, 0xfee1, 2);
		const std::vector<StereoSample> digital = noise(8, 0xfee2, 2);
		const std::vector<StereoSample> last = noise(8, 0xfee3, 2);
		const ReferenceBlock analog{.feedback = kFeedbacks[2], .analog = true, .analog_saturation = 8};
		const ReferenceBlock clipped{.feedback = kFeedbacks[2], .analog = false, .analog_saturation = 8};

		Stereo<FeedbackShaper> resumed;
		std::vector<StereoSample> out(8);
		resumed.configure(settings_of(analog));
		resumed.renderBlock(first, out);
		resumed.configure(settings_of(clipped));
		resumed.renderBlock(digital, out);
		resumed.configure(settings_of(analog));
		resumed.renderBlock(last, out);

		Stereo<FeedbackShaper> fresh{settings_of(analog)};
		std::vector<StereoSample> fresh_out(8);
		fresh.renderBlock(last, fresh_out);
		expect(interleaved(out)).not_().to_equal(interleaved(fresh_out));
	});
});
// clang-format on

CPPSPEC_MAIN(feedback_shaper);
