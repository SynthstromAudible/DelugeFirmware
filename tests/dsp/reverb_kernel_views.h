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

/// @file
/// @brief The reverb kernels' state as reverb_views.h lays it out for the digests, and the impulse measurements the
/// kernel and component specs share.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/reverb/freeverb.h"
#include "dsp-next/float/reverb/plate.h"
#include "reverb_views.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace deluge::dsp::test {

/// @return Freeverb's State over @p memory as FreeverbView lays it out: the lines from kLayout (combs L1..L8,
/// R1..R8, allpasses L1..L4, R1..R4), each line's index, the comb stores (L then R) and the DC blocker's memory.
[[nodiscard]] inline FreeverbView freeverb_view(const fixed::Freeverb::State& s, std::span<const int32_t> memory) {
	using Freeverb = fixed::Freeverb;
	FreeverbView v{};
	const auto line = [memory](Freeverb::Line l) { return memory.subspan(l.base, l.length); };
	for (std::size_t i = 0; i < Freeverb::kCombs; ++i) {
		v.lines[i] = line(Freeverb::kLayout.comb[0][i]);
		v.lines[Freeverb::kCombs + i] = line(Freeverb::kLayout.comb[1][i]);
		v.index[i] = s.left.comb_index[i];
		v.index[Freeverb::kCombs + i] = s.right.comb_index[i];
		v.store[i] = s.left.comb_store[i];
		v.store[Freeverb::kCombs + i] = s.right.comb_store[i];
	}
	for (std::size_t i = 0; i < Freeverb::kAllpasses; ++i) {
		v.lines[(2 * Freeverb::kCombs) + i] = line(Freeverb::kLayout.allpass[0][i]);
		v.lines[(2 * Freeverb::kCombs) + Freeverb::kAllpasses + i] = line(Freeverb::kLayout.allpass[1][i]);
		v.index[(2 * Freeverb::kCombs) + i] = s.left.allpass_index[i];
		v.index[(2 * Freeverb::kCombs) + Freeverb::kAllpasses + i] = s.right.allpass_index[i];
	}
	v.dc = s.dc.y;
	return v;
}

/// @return The plate's State over @p ring as PlateView lays it out.
[[nodiscard]] inline PlateView plate_view(const fp::Plate::State& s, std::span<const float> ring) {
	PlateView v{
	    .ring = ring,
	    .write = s.write,
	    .lfo_y_0 = {},
	    .lfo_y_1 = {},
	    .lp_decay_1 = s.lp_decay_1,
	    .lp_decay_2 = s.lp_decay_2,
	    .lp_band = s.lp_band,
	    .hp_l = s.hp_l,
	    .hp_r = s.hp_r,
	    .lp_l = s.lp_l,
	    .lp_r = s.lp_r,
	};
	s.lfo.y_0.StoreTo(v.lfo_y_0.data());
	s.lfo.y_1.StoreTo(v.lfo_y_1.data());
	return v;
}

/// The block the impulse measurements render in.
inline constexpr std::size_t kImpulseBlock = 64;

/// @return The per-block peak of the left channel after one impulse of 2^28 in the send, over @p seconds;
/// @p render(send, out) adds one block of reverb to @p out.
template <class Render>
[[nodiscard]] std::vector<double> impulse_envelope(Render&& render, double seconds) {
	std::vector<int32_t> input(kImpulseBlock, 0);
	std::vector<StereoSample> output(kImpulseBlock);
	input[0] = 1 << 28;
	const auto blocks = static_cast<std::size_t>(seconds * 44100.0 / kImpulseBlock);
	std::vector<double> envelope;
	envelope.reserve(blocks);
	for (std::size_t block = 0; block < blocks; ++block) {
		std::ranges::fill(output, StereoSample{.l = 0, .r = 0});
		render(std::span<int32_t>{input}, std::span<StereoSample>{output});
		std::ranges::fill(input, 0);
		double peak = 0.0;
		for (const StereoSample& sample : output) {
			peak = std::max(peak, std::abs(static_cast<double>(sample.l)));
		}
		envelope.push_back(peak);
	}
	return envelope;
}

/// @return Seconds for the impulse response to fall 60 dB below its peak, or @p seconds if it never does.
template <class Render>
[[nodiscard]] double rt60(Render&& render, double seconds) {
	const std::vector<double> envelope = impulse_envelope(render, seconds);
	const double decayed = *std::ranges::max_element(envelope) / 1000.0;
	for (std::size_t block = envelope.size(); block-- > 0;) {
		if (envelope[block] > decayed) {
			return static_cast<double>(block + 1) * kImpulseBlock / 44100.0;
		}
	}
	return seconds;
}

} // namespace deluge::dsp::test
