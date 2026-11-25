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

#include <algorithm>
#include <cstdint>
#include <string_view>

/// @file
/// @brief What an envelope saves beyond its params: its mode, its curve and what MPE mode follows, as main's
/// `EnvelopeConfig` (`modulation/envelope_curve.h` there).

namespace deluge::synth {

/// @brief How an envelope runs; the numbers are main's.
enum class EnvelopeMode : uint8_t {
	ADSR, ///< attack, decay, sustain, release
	AHD,  ///< attack, a hold at full level (the decay param), a decay to 0 (the release param), ignoring note-off
	AR,   ///< while held, slews to the sustain param's level at the attack and release rates; releases on note-off
	MPE,  ///< AR, its target raised by the follow input
};

/// @brief What MPE mode follows.
enum class EnvelopeFollow : uint8_t {
	Z, ///< pressure: MPE Z, channel or polyphonic aftertouch
	Y, ///< MPE Y or the mod wheel
};

/// @brief The least curve control.
inline constexpr int32_t kMinCurve = -50;
/// @brief The greatest curve control.
inline constexpr int32_t kMaxCurve = 50;
/// @brief The curve control of the Deluge's original shape.
inline constexpr int32_t kClassicCurve = 32;

/// @brief Each stage's curve, in eighths of an octave.
struct EnvelopeEighths {
	int8_t attack;
	int8_t decay;
	int8_t release;
};
/// @brief The stages' curves at curve control @p curve: decay and release curve / 4 octaves, the attack half that.
[[nodiscard]] constexpr EnvelopeEighths eighths_of(int8_t curve) {
	const auto twice = static_cast<int8_t>(2 * curve);
	return {.attack = curve, .decay = twice, .release = twice};
}
/// @brief Audio clips release along a shallower curve, 4 octaves; their other stages are classic.
inline constexpr EnvelopeEighths kAudioClipEighths{.attack = 32, .decay = 64, .release = 32};

/// @brief An envelope's mode, curve and follow input.
struct EnvelopeState {
	EnvelopeMode mode{EnvelopeMode::ADSR};
	EnvelopeFollow follow{EnvelopeFollow::Z};
	int8_t curve{static_cast<int8_t>(kClassicCurve)}; ///< in [kMinCurve, kMaxCurve]

	bool operator==(const EnvelopeState&) const = default;

	/// @brief Whether this is the classic ADSR, following Z, which a file does not write.
	[[nodiscard]] constexpr bool is_default() const { return *this == EnvelopeState{}; }
	/// @brief @p curve clamped to [kMinCurve, kMaxCurve].
	[[nodiscard]] static constexpr int8_t clamped_curve(int32_t curve) {
		return static_cast<int8_t>(std::clamp(curve, kMinCurve, kMaxCurve));
	}
};

/// @brief main's word for @p mode: `adsr`, `ahd`, `ar` or `mpe`.
[[nodiscard]] constexpr std::string_view to_string(EnvelopeMode mode) {
	switch (mode) {
	case EnvelopeMode::AHD:
		return "ahd";
	case EnvelopeMode::AR:
		return "ar";
	case EnvelopeMode::MPE:
		return "mpe";
	default:
		return "adsr";
	}
}

/// @brief The mode main's word @p word names; any other word is ADSR, as main reads it.
[[nodiscard]] constexpr EnvelopeMode envelope_mode_from(std::string_view word) {
	if (word == "ahd") {
		return EnvelopeMode::AHD;
	}
	if (word == "ar") {
		return EnvelopeMode::AR;
	}
	if (word == "mpe") {
		return EnvelopeMode::MPE;
	}
	return EnvelopeMode::ADSR;
}

/// @brief main's word for @p follow: `z` or `y`.
[[nodiscard]] constexpr std::string_view to_string(EnvelopeFollow follow) {
	return follow == EnvelopeFollow::Y ? "y" : "z";
}

/// @brief The follow main's word @p word names: `y` is Y, any other word Z.
[[nodiscard]] constexpr EnvelopeFollow envelope_follow_from(std::string_view word) {
	return word == "y" ? EnvelopeFollow::Y : EnvelopeFollow::Z;
}

} // namespace deluge::synth
