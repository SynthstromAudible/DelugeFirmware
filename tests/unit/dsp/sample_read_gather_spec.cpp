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
// The sample read's gathers (dsp/fixed/sample_read/gather.h) against the sliding window they replace: each output's
// taps read from staging, newest first, equal the window an Interpolator slid by the same steps holds, and each
// output's word equals its interpolation's, for every rate, kernel and output count.

#include "dsp/fixed/sample_read/gather.h"

#include "dsp/core/fixed_point.h"
#include "dsp/core/fixed_point_neon.h"
#include "dsp/fixed/interpolation/interpolator.h"
#include "dsp/fixed/noise/cong.h"
#include "dsp/fixed/sample_read/stage.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <string>
#include <vector>

#include "support/spec_main.h"

namespace {
using deluge::dsp::Q;
using deluge::dsp::QVec;
using deluge::dsp::fixed::Cong;
using deluge::dsp::fixed::Interpolator;
using deluge::dsp::fixed::sample_read::GatherPos;
using deluge::dsp::fixed::sample_read::kHistoryTaps;
using deluge::dsp::fixed::sample_read::kStageTaps;
using deluge::dsp::fixed::sample_read::sinc_gather;
using deluge::dsp::fixed::sample_read::Staging;

constexpr uint32_t kFrac = (1u << 24) - 1;

/// A sink keeping each output's words.
struct Collect {
	std::vector<int32_t> l;
	std::vector<int32_t> r;
	void put(QVec<31> a, QVec<31> b, int32_t count) {
		for (int32_t i = 0; i < count; i++) {
			l.push_back(a.lane(static_cast<std::size_t>(i)).raw());
			r.push_back(b.lane(static_cast<std::size_t>(i)).raw());
		}
	}
};

/// A history and the frames after it, random, laid out in staging as a read's are: the history in the top slots,
/// frame j in the slot below the one before.
struct Fixture {
	std::vector<int16_t> fl;
	std::vector<int16_t> fr;
	Interpolator window{};
	Staging staging{};

	Fixture(uint32_t seed, int32_t frames, bool narrow) {
		Cong c{seed};
		auto feed = window.fill_tail(0);
		for (int32_t k = 0; k < kHistoryTaps; k++) {
			// A narrowed window holds zeros past its width until they slide out
			const bool zero = narrow && k >= 8;
			feed.put(zero ? int16_t{0} : static_cast<int16_t>(c.next()),
			         zero ? int16_t{0} : static_cast<int16_t>(c.next()));
		}
		for (int32_t j = 0; j < frames; j++) {
			fl.push_back(static_cast<int16_t>(c.next()));
			fr.push_back(static_cast<int16_t>(c.next()));
		}
		for (int32_t k = 0; k < kHistoryTaps; k++) {
			staging.l[kStageTaps - kHistoryTaps + k] = window.window_l()[k];
			staging.r[kStageTaps - kHistoryTaps + k] = window.window_r()[k];
		}
		for (int32_t j = 0; j < frames; j++) {
			staging.l[kStageTaps - kHistoryTaps - 1 - j] = fl[j];
			staging.r[kStageTaps - kHistoryTaps - 1 - j] = fr[j];
		}
	}
};

/// The sliding reference: the window stepped before each output (but the first when @p step_first is false), a step
/// longer than the window jumping the frames before its last 16, then interpolated.
template <int Channels>
Collect slide(Fixture& f, uint32_t phase, uint32_t inc, int32_t kernel, int32_t outputs, bool step_first,
              int32_t& newest_frame, uint32_t& end_phase) {
	Collect out;
	int32_t next = 0;
	for (int32_t i = 0; i < outputs; i++) {
		if (i != 0 || step_first) {
			phase += inc;
			auto steps = static_cast<int32_t>(phase >> 24);
			phase &= kFrac;
			if (steps > kHistoryTaps) {
				next += steps - kHistoryTaps;
				steps = kHistoryTaps;
			}
			auto feed = f.window.advance(static_cast<std::size_t>(steps));
			while (feed.remaining() != 0) {
				feed.put(f.fl[next], f.fr[next]);
				next++;
			}
		}
		const deluge::dsp::StereoSample s = f.window.interpolate(Channels, kernel, phase);
		out.l.push_back(s.l.raw());
		out.r.push_back(Channels == 2 ? s.r.raw() : s.l.raw());
	}
	newest_frame = next - 1;
	end_phase = phase;
	return out;
}

/// Frames @p outputs outputs step past the history at @p inc from @p phase.
int32_t frames_for(uint32_t phase, uint32_t inc, int32_t outputs, bool step_first) {
	const uint64_t steps = step_first ? outputs : outputs - 1;
	return static_cast<int32_t>((phase + (static_cast<uint64_t>(inc) * steps)) >> 24);
}

template <int Channels>
std::string compare(uint32_t inc, int32_t kernel, int32_t outputs, bool step_first, bool narrow) {
	const uint32_t phase = (inc * 7u) & kFrac;
	const int32_t frames = frames_for(phase, inc, outputs, step_first);
	if (frames > kStageTaps - kHistoryTaps) {
		return ""; // past staging: the read splits these
	}
	Fixture f(inc ^ static_cast<uint32_t>(outputs * 131 + kernel), frames + 1, narrow);
	int32_t newest_frame = 0;
	uint32_t end_phase = 0;
	const Collect want = slide<Channels>(f, phase, inc, kernel, outputs, step_first, newest_frame, end_phase);
	Collect got;
	const GatherPos end =
	    sinc_gather<Channels>(f.staging, GatherPos{.newest = kStageTaps - kHistoryTaps, .phase = phase}, inc, kernel,
	                          outputs, step_first, got);
	const std::string where = std::to_string(Channels) + " channels, inc " + std::to_string(inc) + ", kernel "
	                          + std::to_string(kernel) + ", outputs " + std::to_string(outputs)
	                          + (step_first ? ", step first" : "");
	if (got.l != want.l || (Channels == 2 && got.r != want.r)) {
		return "outputs differ: " + where;
	}
	if (end.phase != end_phase || end.newest != kStageTaps - kHistoryTaps - 1 - newest_frame) {
		return "end position differs: " + where;
	}
	return "";
}

constexpr std::array<uint32_t, 8> kIncrements{1u << 24,     0x0010'0000U, 0x00F0'0000U, 3u << 23,
                                              0x01F0'0000U, 4u << 24,     17u << 24,    40u << 24};

} // namespace

// clang-format off
describe sample_read_gather_spec_body("deluge::dsp::fixed::sample_read gathers", ${
	it("gathers each output's sixteen taps as the sliding window holds them, every rate and kernel", _ {
		std::string bad;
		int32_t checked = 0;
		for (const uint32_t inc : kIncrements) {
			for (int32_t kernel = 0; kernel < 7; kernel++) {
				for (const int32_t outputs : {1, 2, 3, 4, 5, 7, 8, 9, 64, 128}) {
					for (const bool step_first : {false, true}) {
						bad += compare<1>(inc, kernel, outputs, step_first, false);
						bad += compare<2>(inc, kernel, outputs, step_first, false);
						checked++;
					}
				}
			}
		}
		expect(bad).to_equal(std::string{});
		expect(checked).to_equal(8 * 7 * 10 * 2);
	});

	it("reads zeros a narrowed window holds until they slide out", _ {
		std::string bad;
		for (const uint32_t inc : {0x0010'0000U, 3u << 23}) {
			for (const int32_t outputs : {3, 12, 40}) {
				bad += compare<2>(inc, 0, outputs, false, true);
			}
		}
		expect(bad).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(sample_read_gather_spec, sample_read_gather_spec_body)
