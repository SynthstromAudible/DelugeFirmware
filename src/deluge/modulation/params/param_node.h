/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "model/positionable.h"

/// A node's offset from its owner's clip value, packed into the seven bytes after
/// ParamNode::interpolated so a node stays 12 bytes. Differences between int32
/// values need 33 bits; the remaining range covers rebased and loaded curves.
/// Reads and writes convert to and from int64_t.
class [[gnu::packed]] ParamNodeOffset {
public:
	static constexpr int64_t kMax = (int64_t{1} << 55) - 1;
	static constexpr int64_t kMin = -(int64_t{1} << 55);

	ParamNodeOffset() = default;
	// Explicit, so mixed expressions such as conditionals convert to int64_t unambiguously.
	explicit ParamNodeOffset(int64_t value) { set(value); }
	ParamNodeOffset& operator=(int64_t value) {
		set(value);
		return *this;
	}
	ParamNodeOffset& operator+=(int64_t difference) {
		set(get() + difference);
		return *this;
	}
	// Implicit so existing int64_t arithmetic reads naturally.
	// NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
	operator int64_t() const { return get(); }
	[[nodiscard]] int64_t get() const {
		return static_cast<int64_t>((static_cast<uint64_t>(static_cast<int64_t>(high_)) << 48)
		                            | (static_cast<uint64_t>(middle_) << 32) | low_);
	}

private:
	void set(int64_t value) {
		// Valid offsets never approach the limits; saturate rather than wrap if they do.
		auto bits = static_cast<uint64_t>(std::clamp(value, kMin, kMax));
		low_ = static_cast<uint32_t>(bits);
		middle_ = static_cast<uint16_t>(bits >> 32);
		high_ = static_cast<int8_t>(bits >> 48);
	}
	uint32_t low_{0};
	uint16_t middle_{0};
	int8_t high_{0};
};
static_assert(sizeof(ParamNodeOffset) == 7);

class ParamNode : public Positionable {
public:
	ParamNode() = default;
	ParamNode(ParamNode const& other) = default;
	ParamNode(ParamNode&& other) = default;

	ParamNode& operator=(ParamNode const& rhs) = default;
	ParamNode& operator=(ParamNode&& rhs) = default;

	/// Whether the value should be interpolated from the previous node to this one.
	///
	/// When false, the value should change only when this node is actually reached.
	bool interpolated{false};
	/// Offset from the owning AutoParam's clip value. Standalone parameters and
	/// clipboard nodes use a zero reference (absolute values). Full int32
	/// differences require 33 signed bits.
	///
	/// For patch cables, values are in the range [-2^30, 2^30-1], while all other parameter types use the
	/// full range (even unipolar parameters!). This means when converting automation from patch cables to
	/// non-patch cables, lshiftAndSaturate<1>() must be used while conversion in the opposite direction
	/// (non-patch-cable to patch-cable) requires a right shift by 1.
	ParamNodeOffset offset;
};
static_assert(sizeof(ParamNode) == 12);

struct StolenParamNodes {
	int32_t num;
	ParamNode* nodes;
	int32_t base;
};
