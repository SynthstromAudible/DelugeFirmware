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

// The input folds monitoring adds to the output. output_monitor_fold, at each fold and gain shift, against the frozen
// output-stage fold over a ring of full-scale noise. Mono<TrackMonitor>, at each mode and amplitude ramp, against the
// frozen track loop, its input copied out of the ring in the two segments a wrap leaves, as the track copies it, over
// blocks of every size read from near the ring's end so they wrap.

#include "dsp-next/fixed/mix/output_fold.h"
#include "dsp-next/fixed/mix/track_monitor.h"

#include "dsp-next/core/mono.h"
#include "effects_workloads.h"
#include "reference_effects.h"

#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;
using namespace effects_workloads;

namespace {

static_assert(mixer_kernel<TrackMonitor>);
static_assert(moded_kernel<TrackMonitor>);
static_assert(!std::is_polymorphic_v<TrackMonitor>);

/// The ring's length in frames, and where the reads start: near its end, so blocks wrap.
constexpr std::size_t kRingFrames = 256;
constexpr std::size_t kStart = 200;

/// The output stage's folds and the actions they replace.
constexpr std::array<std::pair<OutputFold, reference::MonitoringAction>, 3> kOutputFolds{{
    {OutputFold::STEREO, reference::MonitoringAction::NONE},
    {OutputFold::LEFT_ONLY, reference::MonitoringAction::REMOVE_RIGHT_CHANNEL},
    {OutputFold::DIFFERENTIAL, reference::MonitoringAction::SUBTRACT_RIGHT_CHANNEL},
}};

/// The track's modes and the input channels they replace.
constexpr std::array<std::pair<TrackMonitor::Mode, reference::AudioInputChannel>, 5> kTrackModes{{
    {TrackMonitor::Mode::STEREO, reference::AudioInputChannel::STEREO},
    {TrackMonitor::Mode::LEFT, reference::AudioInputChannel::LEFT},
    {TrackMonitor::Mode::RIGHT, reference::AudioInputChannel::RIGHT},
    {TrackMonitor::Mode::BALANCED, reference::AudioInputChannel::BALANCED},
    {TrackMonitor::Mode::COMBINE, reference::AudioInputChannel::NONE},
}};

/// The amplitude ramps compared: up from silence, held, down to silence, and up past full.
constexpr std::array<std::pair<int32_t, int32_t>, 4> kRamps{
    {{0, 1 << 27}, {1 << 27, 1 << 27}, {1 << 28, 0}, {0x40000000, 0x7FFFFFFF}}};

/// @p n frames of @p ring from frame @p at, wrapping, copied as the track copies them.
std::vector<StereoSample> ring_read(const std::vector<StereoSample>& ring, std::size_t at, std::size_t n) {
	const std::size_t first = std::min(n, ring.size() - at);
	std::vector<StereoSample> out(ring.begin() + static_cast<std::ptrdiff_t>(at),
	                              ring.begin() + static_cast<std::ptrdiff_t>(at + first));
	out.insert(out.end(), ring.begin(), ring.begin() + static_cast<std::ptrdiff_t>(n - first));
	return out;
}

} // namespace

// clang-format off
describe input_monitor_spec("the input folds monitoring adds to the output", ${
	it("folds as the output stage did, at each fold and shift", _ {
		const std::vector<StereoSample> ring = frames(kRingFrames, 0x1A9E0001);
		for (const auto [fold, action] : kOutputFolds) {
			for (const int shift : {0, 1, 2, 3, 8}) {
				const std::vector<StereoSample> out = frames(kRingFrames, 0x1A9E0002);
				for (std::size_t i = 0; i < ring.size(); ++i) {
					int32_t l = out[i].l;
					int32_t r = out[i].r;
					reference::output_monitor(action, &ring[i].l, l, r, shift);
					const StereoSample add = output_monitor_fold(fold, ring[i], shift);
					expect(wrapping_add(out[i].l, add.l)).to_equal(l);
					expect(wrapping_add(out[i].r, add.r)).to_equal(r);
				}
			}
		}
	});

	it("adds the input at a ramped amplitude as the track did, at each mode", _ {
		std::vector<StereoSample> ring = frames(kRingFrames, 0x1A9E0003);
		const int32_t* ring_end = &ring.back().l + 2;
		for (const auto [mode, channel] : kTrackModes) {
			for (const auto [from, to] : kRamps) {
				Mono<TrackMonitor> k;
				std::vector<StereoSample> a = frames(kRingFrames, 0x1A9E0004);
				std::vector<StereoSample> b = a;
				std::size_t read = kStart;
				std::size_t at = 0;
				for (std::size_t i = 0; at < a.size(); ++i) {
					const std::size_t n = block_at(i, a.size() - at);
					reference::track_monitor(channel, std::span{a}.subspan(at, n), &ring[read].l, ring_end,
					                         static_cast<int32_t>(kRingFrames), from, to);
					const std::vector<StereoSample> input = ring_read(ring, read, n);
					k.configure({.amplitude_start = from, .amplitude_end = to, .mode = mode});
					k.renderBlock(std::span{b}.subspan(at, n), input, std::span{b}.subspan(at, n));
					read = (read + n) % kRingFrames;
					at += n;
				}
				expect(same(a, b)).to_be_true();
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(input_monitor_spec);
