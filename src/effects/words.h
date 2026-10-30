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

namespace deluge::effects {

/// @brief A filter's circuit. The ladders come first and are contiguous; SVF_BAND is the first high-pass mode and
/// SVF_NOTCH the last low-pass one; OFF is last and means no filter.
enum class FilterMode : uint8_t {
	TRANSISTOR_12DB,
	TRANSISTOR_24DB,
	TRANSISTOR_24DB_DRIVE,
	SVF_BAND,
	SVF_NOTCH,
	HPLADDER,
	OFF,
};

/// @brief The order a sound's or chain's low- and high-pass filters run in.
enum class FilterRoute : uint8_t {
	HIGH_TO_LOW,
	LOW_TO_HIGH,
	PARALLEL,
};

/// @brief The mod-FX unit's effect.
enum class ModFXType : uint8_t {
	NONE,
	FLANGER,
	CHORUS,
	PHASER,
	CHORUS_STEREO,
	WARBLE,
	DIMENSION,
	GRAIN,
};

/// @brief @p mode as a file writes it.
[[nodiscard]] constexpr std::string_view to_string(FilterMode mode) noexcept {
	switch (mode) {
	case FilterMode::TRANSISTOR_12DB:
		return "12dB";
	case FilterMode::TRANSISTOR_24DB:
		return "24dB";
	case FilterMode::TRANSISTOR_24DB_DRIVE:
		return "24dBDrive";
	case FilterMode::SVF_BAND:
		return "SVF_Band";
	case FilterMode::SVF_NOTCH:
		return "SVF_Notch";
	case FilterMode::HPLADDER:
		return "HPLadder";
	case FilterMode::OFF:
		break;
	}
	return "Off";
}

/// @brief The filter mode a file names @p text; OFF for a name no mode has.
[[nodiscard]] constexpr FilterMode parse_filter_mode(std::string_view text) noexcept {
	for (FilterMode mode : {FilterMode::TRANSISTOR_12DB, FilterMode::TRANSISTOR_24DB, FilterMode::TRANSISTOR_24DB_DRIVE,
	                        FilterMode::SVF_BAND, FilterMode::SVF_NOTCH, FilterMode::HPLADDER}) {
		if (to_string(mode) == text) {
			return mode;
		}
	}
	return FilterMode::OFF;
}

/// @brief @p route as a file writes it.
[[nodiscard]] constexpr std::string_view to_string(FilterRoute route) noexcept {
	switch (route) {
	case FilterRoute::HIGH_TO_LOW:
		return "H2L";
	case FilterRoute::LOW_TO_HIGH:
		return "L2H";
	case FilterRoute::PARALLEL:
		break;
	}
	return "PARA";
}

/// @brief The filter route a file names @p text; PARALLEL for a name no route has.
[[nodiscard]] constexpr FilterRoute parse_filter_route(std::string_view text) noexcept {
	if (text == "H2L") {
		return FilterRoute::HIGH_TO_LOW;
	}
	if (text == "L2H") {
		return FilterRoute::LOW_TO_HIGH;
	}
	return FilterRoute::PARALLEL;
}

/// @brief @p type as a file writes it.
[[nodiscard]] constexpr std::string_view to_string(ModFXType type) noexcept {
	switch (type) {
	case ModFXType::FLANGER:
		return "flanger";
	case ModFXType::CHORUS:
		return "chorus";
	case ModFXType::PHASER:
		return "phaser";
	case ModFXType::CHORUS_STEREO:
		return "StereoChorus";
	case ModFXType::WARBLE:
		return "TapeWarble";
	case ModFXType::DIMENSION:
		return "dimension";
	case ModFXType::GRAIN:
		return "grainFX";
	case ModFXType::NONE:
		break;
	}
	return "none";
}

/// @brief The mod-FX type a file names @p text; NONE for a name no type has.
[[nodiscard]] constexpr ModFXType parse_mod_fx_type(std::string_view text) noexcept {
	for (ModFXType type : {ModFXType::FLANGER, ModFXType::CHORUS, ModFXType::PHASER, ModFXType::CHORUS_STEREO,
	                       ModFXType::WARBLE, ModFXType::DIMENSION, ModFXType::GRAIN}) {
		if (to_string(type) == text) {
			return type;
		}
	}
	return ModFXType::NONE;
}

/// @brief The song reverb's model, numbered as a song file saves it.
enum class ReverbModel : uint8_t {
	FREEVERB = 0, ///< Freeverb, for songs saved before there was a choice.
	MUTABLE = 1,  ///< The Mutable plate, a new song's.
	DIGITAL = 2,  ///< Dattorro's plate.
};

/// @brief Whether a delay is on, and which feedback it plays. OFF holds no tapes and never renders.
enum class DelayType : uint8_t {
	OFF,
	DIGITAL,
	ANALOG,
};

/// @return @p type's name, as a file and an Edit write it.
[[nodiscard]] constexpr std::string_view to_string(DelayType type) noexcept {
	switch (type) {
	case DelayType::OFF:
		return "off";
	case DelayType::ANALOG:
		return "analog";
	case DelayType::DIGITAL:
		break;
	}
	return "digital";
}

/// @brief The delay type a file names @p text; DIGITAL for a name no type has.
[[nodiscard]] constexpr DelayType parse_delay_type(std::string_view text) noexcept {
	for (DelayType type : {DelayType::OFF, DelayType::ANALOG}) {
		if (to_string(type) == text) {
			return type;
		}
	}
	return DelayType::DIGITAL;
}

/// @return The type one press of the delay's feedback knob gives after @p type: digital, analog, off, digital.
[[nodiscard]] constexpr DelayType next_delay_type(DelayType type) noexcept {
	switch (type) {
	case DelayType::DIGITAL:
		return DelayType::ANALOG;
	case DelayType::ANALOG:
		return DelayType::OFF;
	case DelayType::OFF:
		break;
	}
	return DelayType::DIGITAL;
}

} // namespace deluge::effects
