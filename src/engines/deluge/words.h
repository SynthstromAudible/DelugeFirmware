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

#include <cstdint>
#include <string_view>

namespace deluge::instrument {

/// @brief How the two sources make sound: subtractive, FM, or ring-modulated.
enum class SynthMode : uint8_t {
	SUBTRACTIVE,
	FM,
	RINGMOD,
};

/// @brief A source's oscillator. Saved by name (see to_string).
enum class OscType : uint8_t {
	SINE,
	TRIANGLE,
	SQUARE,
	ANALOG_SQUARE,
	SAW,
	ANALOG_SAW_2,
	WAVETABLE,
	SAMPLE,
	DX7,
	INPUT_L,
	INPUT_R,
	INPUT_STEREO,
};

/// @brief An LFO's shape.
enum class LFOType : uint8_t {
	SINE,
	TRIANGLE,
	SQUARE,
	SAW,
	SAMPLE_AND_HOLD,
	RANDOM_WALK,
	WARBLER,
};

/// @brief How a sound shares voices among its notes.
enum class PolyphonyMode : uint8_t {
	AUTO,
	POLY,
	MONO,
	LEGATO,
	CHOKE,
};

/// @brief How a sample source plays past its end. Saved as its index (`loopMode`).
enum class SampleRepeatMode {
	CUT,
	ONCE,
	LOOP,
	STRETCH,
};

/// @brief The file name of @p mode.
[[nodiscard]] constexpr std::string_view to_string(SynthMode mode) noexcept {
	switch (mode) {
	case SynthMode::FM:
		return "fm";
	case SynthMode::RINGMOD:
		return "ringmod";
	case SynthMode::SUBTRACTIVE:
		break;
	}
	return "subtractive";
}

/// @brief The synth mode @p text names; anything else reads as subtractive.
[[nodiscard]] constexpr SynthMode parse_synth_mode(std::string_view text) noexcept {
	if (text == "fm") {
		return SynthMode::FM;
	}
	if (text == "ringmod") {
		return SynthMode::RINGMOD;
	}
	return SynthMode::SUBTRACTIVE;
}

/// @brief The file name of @p type.
[[nodiscard]] constexpr std::string_view to_string(OscType type) noexcept {
	switch (type) {
	case OscType::SQUARE:
		return "square";
	case OscType::SAW:
		return "saw";
	case OscType::ANALOG_SAW_2:
		return "analogSaw";
	case OscType::ANALOG_SQUARE:
		return "analogSquare";
	case OscType::SINE:
		return "sine";
	case OscType::TRIANGLE:
		return "triangle";
	case OscType::SAMPLE:
		return "sample";
	case OscType::WAVETABLE:
		return "wavetable";
	case OscType::INPUT_L:
		return "inLeft";
	case OscType::INPUT_R:
		return "inRight";
	case OscType::INPUT_STEREO:
		return "inStereo";
	case OscType::DX7:
		return "dx7";
	}
	return "triangle";
}

/// @brief The oscillator type @p text names; anything else reads as a triangle.
[[nodiscard]] constexpr OscType parse_osc_type(std::string_view text) noexcept {
	for (OscType t :
	     {OscType::SQUARE, OscType::ANALOG_SQUARE, OscType::ANALOG_SAW_2, OscType::SAW, OscType::SINE, OscType::SAMPLE,
	      OscType::WAVETABLE, OscType::INPUT_L, OscType::INPUT_R, OscType::INPUT_STEREO, OscType::DX7}) {
		if (text == to_string(t)) {
			return t;
		}
	}
	return OscType::TRIANGLE;
}

/// @brief The file name of @p type.
[[nodiscard]] constexpr std::string_view to_string(LFOType type) noexcept {
	switch (type) {
	case LFOType::SQUARE:
		return "square";
	case LFOType::SAW:
		return "saw";
	case LFOType::SINE:
		return "sine";
	case LFOType::SAMPLE_AND_HOLD:
		return "sah";
	case LFOType::RANDOM_WALK:
		return "rwalk";
	case LFOType::WARBLER:
		return "warbler";
	case LFOType::TRIANGLE:
		break;
	}
	return "triangle";
}

/// @brief The LFO shape @p text names; anything else reads as a triangle.
[[nodiscard]] constexpr LFOType parse_lfo_type(std::string_view text) noexcept {
	for (LFOType t : {LFOType::SQUARE, LFOType::SAW, LFOType::SINE, LFOType::SAMPLE_AND_HOLD, LFOType::WARBLER,
	                  LFOType::RANDOM_WALK}) {
		if (text == to_string(t)) {
			return t;
		}
	}
	return LFOType::TRIANGLE;
}

/// @brief The file name of @p mode.
[[nodiscard]] constexpr std::string_view to_string(PolyphonyMode mode) noexcept {
	switch (mode) {
	case PolyphonyMode::MONO:
		return "mono";
	case PolyphonyMode::AUTO:
		return "auto";
	case PolyphonyMode::LEGATO:
		return "legato";
	case PolyphonyMode::CHOKE:
		return "choke";
	case PolyphonyMode::POLY:
		break;
	}
	return "poly";
}

/// @brief The polyphony @p text names; anything else reads as poly.
[[nodiscard]] constexpr PolyphonyMode parse_polyphony_mode(std::string_view text) noexcept {
	if (text == "mono") {
		return PolyphonyMode::MONO;
	}
	if (text == "auto") {
		return PolyphonyMode::AUTO;
	}
	if (text == "legato") {
		return PolyphonyMode::LEGATO;
	}
	if (text == "choke") {
		return PolyphonyMode::CHOKE;
	}
	return PolyphonyMode::POLY;
}

} // namespace deluge::instrument
