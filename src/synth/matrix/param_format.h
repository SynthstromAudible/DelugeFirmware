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
#pragma once

#include "dsp-next/core/fixed_point.h"
#include "modulation/params/param.h"
#include "synth/matrix/descriptors.h" // kParamNeutral

#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

/// @file
/// @brief Each patched param's final value in its own format, and the accessors that read it.
/// @details The matrix holds the final values as one word per param, because it evaluates them four lanes at a time
/// by index; each word is its param's value in that param's format. A named read goes through @ref param_value,
/// which gives the format below. A rate the consumer steps by as an
/// integer (an envelope stage's position step) is an `int32_t`.

namespace deluge::synth::matrix {

namespace params = modulation::params;

/// @brief The format param @p P's final value is in. A level, a position or a pan is Q31 unless named below.
template <int P>
struct ParamFormat {
	using type = dsp::Q<31>;
};

// The filters' resonance and morph: Q29, 2^29 is "1"
template <>
struct ParamFormat<params::LOCAL_LPF_RESONANCE> {
	using type = dsp::Q<29>;
};
template <>
struct ParamFormat<params::LOCAL_HPF_RESONANCE> {
	using type = dsp::Q<29>;
};
template <>
struct ParamFormat<params::LOCAL_LPF_MORPH> {
	using type = dsp::Q<29>;
};
template <>
struct ParamFormat<params::LOCAL_HPF_MORPH> {
	using type = dsp::Q<29>;
};

// Pitch factors: 2^24 is unison
template <>
struct ParamFormat<params::LOCAL_PITCH_ADJUST> {
	using type = dsp::Q<24>;
};
template <>
struct ParamFormat<params::LOCAL_OSC_A_PITCH_ADJUST> {
	using type = dsp::Q<24>;
};
template <>
struct ParamFormat<params::LOCAL_OSC_B_PITCH_ADJUST> {
	using type = dsp::Q<24>;
};
template <>
struct ParamFormat<params::LOCAL_MODULATOR_0_PITCH_ADJUST> {
	using type = dsp::Q<24>;
};
template <>
struct ParamFormat<params::LOCAL_MODULATOR_1_PITCH_ADJUST> {
	using type = dsp::Q<24>;
};
template <>
struct ParamFormat<params::GLOBAL_DELAY_RATE> {
	using type = dsp::Q<24>;
};
// The arp's rate: 2^24 is the base rate (its phase step is the rate / 32)
template <>
struct ParamFormat<params::GLOBAL_ARP_RATE> {
	using type = dsp::Q<24>;
};

// Phase increments: one cycle at 2^32
template <>
struct ParamFormat<params::LOCAL_LFO_LOCAL_FREQ_1> {
	using type = dsp::UQ<32>;
};
template <>
struct ParamFormat<params::LOCAL_LFO_LOCAL_FREQ_2> {
	using type = dsp::UQ<32>;
};
template <>
struct ParamFormat<params::GLOBAL_MOD_FX_RATE> {
	using type = dsp::UQ<32>;
};
template <>
struct ParamFormat<params::GLOBAL_LFO_FREQ_1> {
	using type = dsp::UQ<32>;
};
template <>
struct ParamFormat<params::GLOBAL_LFO_FREQ_2> {
	using type = dsp::UQ<32>;
};

// The post-FX and post-reverb volumes: Q27, 2^27 is unity
template <>
struct ParamFormat<params::GLOBAL_VOLUME_POST_FX> {
	using type = dsp::Q<27>;
};
template <>
struct ParamFormat<params::GLOBAL_VOLUME_POST_REVERB_SEND> {
	using type = dsp::Q<27>;
};

// Integer rates: an envelope stage's position step per sample (2^23 a stage)
template <>
struct ParamFormat<params::LOCAL_ENV_0_ATTACK> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_1_ATTACK> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_2_ATTACK> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_3_ATTACK> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_0_DECAY> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_1_DECAY> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_2_DECAY> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_3_DECAY> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_0_RELEASE> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_1_RELEASE> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_2_RELEASE> {
	using type = int32_t;
};
template <>
struct ParamFormat<params::LOCAL_ENV_3_RELEASE> {
	using type = int32_t;
};

/// @brief The format param @p P's final value is in.
template <int P>
using param_format_t = ParamFormat<P>::type;

namespace detail {
/// @brief A param word read in format @p T.
template <class T>
[[nodiscard, gnu::always_inline]] constexpr T from_word(int32_t word) {
	if constexpr (std::is_same_v<T, int32_t>) {
		return word;
	}
	else {
		// The word is the param's value in its format
		return T::from_raw(static_cast<T::storage_type>(word));
	}
}
} // namespace detail

/// @brief Local param @p P's final value from the words of the local params, in its format.
template <int P>
requires(P < params::FIRST_GLOBAL)
[[nodiscard, gnu::always_inline]] constexpr param_format_t<P> param_value(std::span<const int32_t> words) {
	return detail::from_word<param_format_t<P>>(words[P]);
}

/// @brief Global param @p P's final value from the words of the global params (indexed from FIRST_GLOBAL).
template <int P>
requires(P >= params::FIRST_GLOBAL)
[[nodiscard, gnu::always_inline]] constexpr param_format_t<P> global_param_value(std::span<const int32_t> words) {
	return detail::from_word<param_format_t<P>>(words[P - params::FIRST_GLOBAL]);
}

/// @brief Local param @p First + @p i's final value, for a run of local params that share one format.
template <int First, int N>
requires(First + N <= params::FIRST_GLOBAL)
[[nodiscard, gnu::always_inline]] constexpr param_format_t<First> param_value_at(std::span<const int32_t> words,
                                                                                 int32_t i) {
	static_assert(
	    [] {
		    bool same = true;
		    [&]<int... I>(std::integer_sequence<int, I...>) {
			    same = (std::is_same_v<param_format_t<First>, param_format_t<First + I>> && ...);
		    }(std::make_integer_sequence<int, N>{});
		    return same;
	    }(),
	    "every param of the run has one format");
	return detail::from_word<param_format_t<First>>(words[First + i]);
}

/// @brief Param @p P's neutral value, in its format.
template <int P>
[[nodiscard, gnu::always_inline]] constexpr param_format_t<P> param_neutral() {
	return detail::from_word<param_format_t<P>>(kParamNeutral[P]);
}

} // namespace deluge::synth::matrix
