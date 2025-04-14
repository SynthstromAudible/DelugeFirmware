/**
 * Copyright (c) 2025 Katherine Whitlock
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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp_ng/core/types.hpp"
#include "dsp_ng/core/units.hpp"
#include <argon.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace deluge::dsp::filters {

namespace impl {
struct LinkwitzRileyBase {
	LinkwitzRileyBase() { setCutoff(2000.f); }

	constexpr void setCutoff(Frequency cutoff) {
		cutoff_ = cutoff;
		g_ = std::tan(std::numbers::pi_v<float> * cutoff_ / kSampleRate);
		h_ = (1.f / (1.f + std::numbers::sqrt2_v<float> * g_ + g_ * g_));
	}

	Frequency cutoff_{};
	float g_ = 0;
	float h_ = 0;
};
} // namespace impl

/// @brief A 4th-order Linkwitz-Riley (LR-4) crossover filter
/// @tparam T The sample type of the filter (currently only float and StereoSample<float> are supported)
/// @details A Linkwitz-Riley filter is a type of crossover filter that is used to split an audio signal into two
/// frequency bands: low and high. This allows processing on the component signals, such as EQ or compression, before
/// recombining them. Being a 4th-order filter, it has a 24dB/octave slope.
template <typename T>
class LinkwitzRiley;

/// @brief A 4th-order Linkwitz-Riley filter class for stereo signals.
template <>
class LinkwitzRiley<StereoSample<float>> : public impl::LinkwitzRileyBase {
public:
	using impl::LinkwitzRileyBase::LinkwitzRileyBase;

	std::pair<StereoSample<float>, StereoSample<float>> render(StereoSample<float> input) {
		auto inner_block = [this](ArgonHalf<float>& s, ArgonHalf<float> x) {
			s = s.MultiplyAdd(g_, x); // s += (g * x)
			ArgonHalf<float> y = s;   // y = s
			s = s.MultiplyAdd(g_, x); // s += (g * x)
			return y;
		};

		StereoSample<float> output_low{};
		StereoSample<float> output_high{};

		auto& [s1, s2, s3, s4] = state;
		auto x_vec = ArgonHalf<float>::Load(&input.l);

		ArgonHalf<float> yH = (x_vec.MultiplySubtract((R2 + g_), s1) - s2) * h_;

		ArgonHalf<float> yB = inner_block(s1, yH);
		ArgonHalf<float> yL = inner_block(s2, yB);

		ArgonHalf<float> yH2 = (yL.MultiplySubtract((R2 + g_), s3) - s4) * h_;

		ArgonHalf<float> yB2 = inner_block(s2, yH2);
		ArgonHalf<float> yL2 = inner_block(s2, yB2);

		ArgonHalf<float> high = yL.MultiplySubtract(R2, yB) + yH - yL2;

		yL2.StoreTo(&output_low.l);
		high.StoreTo(&output_high.l);

		return {output_low, output_high};
	}

private:
	static constexpr auto R2 = std::numbers::sqrt2_v<float>;
	std::array<ArgonHalf<float>, 4> state{};
};

/// @brief A 4th-order Linkwitz-Riley filter class for mono signals.
template <>
class LinkwitzRiley<float> : public impl::LinkwitzRileyBase {
public:
	using impl::LinkwitzRileyBase::LinkwitzRileyBase;

	std::pair<float, float> render(float input) {
		auto inner_block = [this](float& s, float x) {
			s += g_ * x;
			float y = s; // y = s
			s += g_ * x;
			return y;
		};

		auto& [s1, s2, s3, s4] = state;

		float yH = (input - ((R2 + g_) * s1) - s2) * h_;

		float yB = inner_block(s1, yH);
		float yL = inner_block(s2, yB);

		float yH2 = (yL - ((R2 + g_) * s3) - s4) * h_;

		float yB2 = inner_block(s2, yH2);
		float yL2 = inner_block(s2, yB2);

		float high = yL - (R2 * yB) + yH - yL2;

		return {yL2, high};
	}

private:
	static constexpr auto R2 = std::numbers::sqrt2_v<float>;
	std::array<float, 4> state{};
};

/// @brief Linkwitz-Riley filters are the go-to choice for crossover filters, so just alias them.
template <typename T>
using Crossover = LinkwitzRiley<T>;
} // namespace deluge::dsp::filters
