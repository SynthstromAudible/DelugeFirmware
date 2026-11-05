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

// The params' formats and the accessor that reads and writes their words.

#include "synth/matrix/param_format.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

namespace matrix = deluge::synth::matrix;
namespace params = deluge::modulation::params;
using deluge::dsp::Q;
using deluge::dsp::UQ;

static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_VOLUME>, Q<31>>);
static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_PAN>, Q<31>>);
static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_LPF_RESONANCE>, Q<29>>);
static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_PITCH_ADJUST>, Q<24>>);
static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_LFO_LOCAL_FREQ_1>, UQ<32>>);
static_assert(std::is_same_v<matrix::param_format_t<params::GLOBAL_VOLUME_POST_FX>, Q<27>>);
static_assert(std::is_same_v<matrix::param_format_t<params::LOCAL_ENV_0_ATTACK>, int32_t>);

namespace {
/// Every param's word set to a distinct value.
std::array<int32_t, params::kNumParams> numbered_words() {
	std::array<int32_t, params::kNumParams> words{};
	for (std::size_t p = 0; p < words.size(); ++p) {
		words[p] = static_cast<int32_t>(0x01010101u * (p + 1)) - (1 << 30);
	}
	return words;
}
} // namespace

// clang-format off
describe param_format_spec("each param's format", ${
	it("reads each param's own word, in its format", _ {
		const auto words = numbered_words();
		expect(matrix::param_value<params::LOCAL_PAN>(words).raw()).to_equal(words[params::LOCAL_PAN]);
		expect(matrix::param_value<params::LOCAL_PITCH_ADJUST>(words).raw()).to_equal(words[params::LOCAL_PITCH_ADJUST]);
		expect(matrix::param_value<params::LOCAL_ENV_0_ATTACK>(words)).to_equal(words[params::LOCAL_ENV_0_ATTACK]);
		expect(static_cast<int32_t>(matrix::param_value<params::LOCAL_LFO_LOCAL_FREQ_1>(words).raw()))
		    .to_equal(words[params::LOCAL_LFO_LOCAL_FREQ_1]);
	});
	it("reads a run of params by its offset", _ {
		const auto words = numbered_words();
		expect(matrix::param_value_at<params::LOCAL_OSC_A_VOLUME, 2>(words, 1).raw())
		    .to_equal(words[params::LOCAL_OSC_B_VOLUME]);
		expect(matrix::param_value_at<params::LOCAL_ENV_0_RELEASE, 4>(words, 3)).to_equal(words[params::LOCAL_ENV_3_RELEASE]);
	});
	it("reads a global param from the globals' words", _ {
		std::array<int32_t, params::kNumParams - params::FIRST_GLOBAL> globals{};
		globals[params::GLOBAL_DELAY_RATE - params::FIRST_GLOBAL] = 12345;
		expect(matrix::global_param_value<params::GLOBAL_DELAY_RATE>(globals).raw()).to_equal(12345);
	});
	it("gives each neutral in its param's format", _ {
		expect(matrix::param_neutral<params::LOCAL_PITCH_ADJUST>().raw()).to_equal(matrix::kParamNeutral[params::LOCAL_PITCH_ADJUST]);
		expect(matrix::param_neutral<params::LOCAL_ENV_0_ATTACK>()).to_equal(matrix::kParamNeutral[params::LOCAL_ENV_0_ATTACK]);
	});
});
// clang-format on

CPPSPEC_MAIN(param_format_spec);
