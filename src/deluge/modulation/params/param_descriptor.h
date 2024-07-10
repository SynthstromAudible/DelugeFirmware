/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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
#include "util/misc.h"
#include <cstdint>

/// Wrapper class around an int32, representing a parameter. The least significant 8 bits represent the actual
/// parameter. The next 8 bits represent a PatchSource modulating that parameter, the 8 bits after that represent the
/// source modulating that modulation, and so on.
class ParamDescriptor {
public:
	ParamDescriptor() = default;
	ParamDescriptor(const ParamDescriptor&) = default;
	ParamDescriptor(ParamDescriptor&&) = default;
	ParamDescriptor& operator=(const ParamDescriptor&) = default;
	ParamDescriptor& operator=(ParamDescriptor&&) = default;

	/// Construct a ParamDescriptor directly from its value.
	explicit ParamDescriptor(int32_t data_) : data(data_) {}

	constexpr void setToHaveParamOnly(int32_t p) { data = p | 0xFFFFFF00; }

	constexpr void setToHaveParamAndSource(int32_t p, PatchSource s) {
		data = p | (util::to_underlying(s) << 8) | 0xFFFF0000;
	}

	constexpr void setToHaveParamAndTwoSources(int32_t p, PatchSource s, PatchSource sLowestLevel) {
		data = p | (util::to_underlying(s) << 8) | (util::to_underlying(sLowestLevel) << 16) | 0xFF000000;
	}

	[[nodiscard]] constexpr bool isSetToParamWithNoSource(int32_t p) const { return (data == (p | 0xFFFFFF00)); }

	[[nodiscard]] constexpr inline bool isSetToParamAndSource(int32_t p, PatchSource s) const {
		return (data == (p | (util::to_underlying(s) << 8) | 0xFFFF0000));
	}

	[[nodiscard]] constexpr bool isJustAParam() const { return (data & 0x0000FF00) == 0x0000FF00; }

	[[nodiscard]] constexpr int32_t getJustTheParam() const { return data & 0xFF; }

	constexpr void changeParam(int32_t newParam) { data = (data & 0xFFFFFF00) | newParam; }

	[[nodiscard]] constexpr PatchSource getBottomLevelSource() const { // As in, the one furthest away from the param.
		if ((data & 0x00FF0000) == 0x00FF0000) {
			return static_cast<PatchSource>((data >> 8) & 0xFF);
		}

		return static_cast<PatchSource>((data >> 16) & 0xFF);
	}

	constexpr void addSource(PatchSource newSource) {
		uint8_t newSourceUnderlying = util::to_underlying(newSource);
		if ((data & 0x0000FF00) == 0x0000FF00) {
			data = (data & 0xFFFF00FF) | (newSourceUnderlying << 8);
		}
		else if ((data & 0x00FF0000) == 0x00FF0000) {
			data = (data & 0xFF00FFFF) | (newSourceUnderlying << 16);
		}
		else {
			data = (data & 0x00FFFFFF) | (newSourceUnderlying << 24);
		}
	}

	[[nodiscard]] constexpr ParamDescriptor getDestination() const {
		ParamDescriptor newParamDescriptor{};
		if ((data & 0x00FF0000) == 0x00FF0000) {
			newParamDescriptor.data = data | 0x0000FF00;
		}
		else {
			newParamDescriptor.data = data | 0x00FF0000;
		}
		return newParamDescriptor;
	}

	[[nodiscard]] constexpr bool hasJustOneSource() const {
		return ((data & 0xFFFF0000) == 0xFFFF0000) && ((data & 0x0000FF00) != 0x0000FF00);
	}

	[[nodiscard]] constexpr PatchSource getTopLevelSource() const { // As in, the one, nearest the param.
		return static_cast<PatchSource>((data & 0x0000FF00) >> 8);
	}

	[[nodiscard]] constexpr PatchSource getSecondSourceFromTop() const {
		return static_cast<PatchSource>((data & 0x00FF0000) >> 16);
	}

	[[nodiscard]] constexpr bool hasSecondSource() const { return ((data & 0x00FF0000) != 0x00FF0000); }

	constexpr void setToNull() { data = 0xFFFFFFFF; }

	[[nodiscard]] constexpr bool isNull() const { return (data == 0xFFFFFFFF); }

	uint32_t data{0};
};

constexpr bool operator==(const ParamDescriptor& lhs, const ParamDescriptor& rhs) {
	return (lhs.data == rhs.data);
}
constexpr bool operator!=(const ParamDescriptor& lhs, const ParamDescriptor& rhs) {
	return !(lhs == rhs);
}
