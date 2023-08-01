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

#include <cstdint>
#include "definitions_cxx.hpp"
#include "util/misc.h"

class ParamDescriptor {
public:
	inline void setToHaveParamOnly(int32_t p) { data = p | 0xFFFFFF00; }

	inline void setToHaveParamAndSource(int32_t p, PatchSource s) { data = p | (util::to_underlying(s) << 8) | 0xFFFF0000; }

	inline void setToHaveParamAndTwoSources(int32_t p, PatchSource s, PatchSource sLowestLevel) {
		data = p | (util::to_underlying(s) << 8) | (util::to_underlying(sLowestLevel) << 16) | 0xFF000000;
	}

	inline bool isSetToParamWithNoSource(int32_t p) { return (data == (p | 0xFFFFFF00)); }

	inline bool isSetToParamAndSource(int32_t p, PatchSource s) {
		return (data == (p | (util::to_underlying(s) << 8) | 0xFFFF0000));
	}

	inline bool isJustAParam() { return (data & 0x0000FF00) == 0x0000FF00; }

	inline int32_t getJustTheParam() { return data & 0xFF; }

	inline void changeParam(int32_t newParam) { data = (data & 0xFFFFFF00) | newParam; }

	inline PatchSource getBottomLevelSource() { // As in, the one furthest away from the param.
		if ((data & 0x00FF0000) == 0x00FF0000) {
			return static_cast<PatchSource>((data >> 8) & 0xFF);
		}
		else {
			return static_cast<PatchSource>((data >> 16) & 0xFF);
		}
	}

	inline void addSource(PatchSource newSource) {
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

	inline ParamDescriptor getDestination() {
		ParamDescriptor newParamDescriptor;
		if ((data & 0x00FF0000) == 0x00FF0000) {
			newParamDescriptor.data = data | 0x0000FF00;
		}
		else {
			newParamDescriptor.data = data | 0x00FF0000;
		}
		return newParamDescriptor;
	}

	inline bool hasJustOneSource() {
		return ((data & 0xFFFF0000) == 0xFFFF0000) && ((data & 0x0000FF00) != 0x0000FF00);
	}

	inline PatchSource getTopLevelSource() { // As in, the one, nearest the param.
		return static_cast<PatchSource>((data & 0x0000FF00) >> 8);
	}

	inline PatchSource getSecondSourceFromTop() { return static_cast<PatchSource>((data & 0x00FF0000) >> 16); }

	inline bool hasSecondSource() { return ((data & 0x00FF0000) != 0x00FF0000); }

	inline void setToNull() { data = 0xFFFFFFFF; }

	inline bool isNull() { return (data == 0xFFFFFFFF); }

	uint32_t data;
};

inline bool operator==(const ParamDescriptor& lhs, const ParamDescriptor& rhs) {
	return (lhs.data == rhs.data);
}
inline bool operator!=(const ParamDescriptor& lhs, const ParamDescriptor& rhs) {
	return !(lhs == rhs);
}
