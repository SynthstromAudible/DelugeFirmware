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
#include <bit>
#include <cstdint>
#include <limits>

// The full difference between two signed 32-bit parameter values needs 33 bits.
// Represent the clip offset as the difference of two int32 endpoints. This retains
// all 33 bits without an aligned int64 member, and makes an unchanged baseline cheap.
class ClipParamValue {
public:
	static int32_t resolve(int64_t value) {
		return static_cast<int32_t>(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX));
	}
	[[nodiscard]] int32_t getPresetValue() const {
		const int32_t* preset = presetPointer();
		return preset != nullptr ? *preset : 0;
	}
	[[nodiscard]] int64_t getOffset() const {
		if ((presetAndOffset_ & 2) != 0) [[unlikely]] {
			return (int64_t{presetAtEdit_} * 4294967296LL) + std::bit_cast<uint32_t>(valueAtEdit_);
		}
		return int64_t{valueAtEdit_} - presetAtEdit_;
	}
	[[nodiscard]] int32_t getValue() const {
		// An untouched parameter reads the output directly. The hot path does not
		// fetch per-clip adjustment fields or perform wide arithmetic.
		if ((presetAndOffset_ & 1) == 0) [[likely]] {
			// The clear tag means this is the original aligned preset pointer.
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
			const auto* preset = reinterpret_cast<const int32_t*>(presetAndOffset_);
			return preset != nullptr ? *preset : 0;
		}
		return getAdjustedValue();
	}
	[[nodiscard]] int64_t getUnclampedValue() const { return int64_t{getPresetValue()} + getOffset(); }
	// Undo must retain the part of a clip adjustment hidden by saturation.
	void setUnclampedValue(int64_t value) {
		int64_t offset = value - getPresetValue();
		// Detaching or changing baselines can require more than 33 bits even
		// though each user edit fits in int32. Use the same two words for those
		// rare values, with the second aligned-pointer tag selecting the encoding.
		if (offset < -int64_t{UINT32_MAX} || offset > int64_t{UINT32_MAX}) {
			auto bits = static_cast<uint64_t>(offset);
			valueAtEdit_ = std::bit_cast<int32_t>(static_cast<uint32_t>(bits));
			presetAtEdit_ = std::bit_cast<int32_t>(static_cast<uint32_t>(bits >> 32));
			presetAndOffset_ = (presetAndOffset_ & ~uintptr_t{3}) | 3;
			return;
		}
		valueAtEdit_ = offset >= 0 ? INT32_MAX : INT32_MIN;
		presetAtEdit_ = static_cast<int32_t>(int64_t{valueAtEdit_} - offset);
		presetAndOffset_ = (presetAndOffset_ & ~uintptr_t{3}) | uintptr_t{offset != 0};
	}
	void setValue(int32_t value) {
		valueAtEdit_ = value;
		presetAtEdit_ = getPresetValue();
		presetAndOffset_ = (presetAndOffset_ & ~uintptr_t{3}) | uintptr_t{value != presetAtEdit_};
	}
	// Used when replacing a zero baseline slot with the permanent zero baseline.
	// Both baselines are equal, so retain even offsets hidden by saturation.
	void detachZeroPreset() {
		if (getPresetValue() == 0) {
			presetAndOffset_ &= uintptr_t{3};
		}
	}
	void bindPreset(const int32_t* preset) {
		if (presetPointer() == preset) {
			return;
		}
		int64_t value = getUnclampedValue();
		// The aligned int32 pointer leaves a low bit for the zero-offset fast path.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		presetAndOffset_ = reinterpret_cast<uintptr_t>(preset);
		if (value == resolve(value)) {
			setValue(static_cast<int32_t>(value));
		}
		else {
			setUnclampedValue(value);
		}
	}

private:
	[[nodiscard]] const int32_t* presetPointer() const {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
		return reinterpret_cast<const int32_t*>(presetAndOffset_ & ~uintptr_t{3});
	}
	// Keep adjustment and saturation code out of each inlined audio-path read.
	[[nodiscard]] [[gnu::noinline]] int32_t getAdjustedValue() const;
	uintptr_t presetAndOffset_ = 0;
	int32_t valueAtEdit_ = 0;
	int32_t presetAtEdit_ = 0;
};

// Playback curves receive their base from the owner. No binding or standalone scalar is stored here.
class AutoParamValue {
public:
	// AutoParam supplies its binding explicitly so a relocated or cloned curve
	// never needs to write a back-pointer during a read.
	[[nodiscard]] int32_t getValueForBase(int32_t base) const {
		if (base != evaluatedBase_) {
			evaluate(base);
		}
		return evaluatedValue_;
	}
	[[nodiscard]] int64_t getOffset() const { return offset_; }
	void setOffsetForBase(int64_t offset, int32_t base) {
		offset_ = offset;
		evaluate(base);
	}

private:
	int64_t offset_ = 0;
	void evaluate(int32_t base) const {
		evaluatedBase_ = base;
		evaluatedValue_ = ClipParamValue::resolve(int64_t{base} + offset_);
	}
	mutable int32_t evaluatedBase_ = 0;
	mutable int32_t evaluatedValue_ = 0;
};

// Avoid overflowing distance * elapsed for full-width curves and long clips.
inline int64_t interpolateAutomationOffset(int64_t left, int64_t right, int32_t elapsed, int32_t duration) {
	int64_t distance = right - left;
	return left + ((distance / duration) * elapsed) + ((distance % duration) * elapsed / duration);
}

// Rendering normally multiplies two signed 32-bit quantities. Keep that path a
// single widening multiply instead of doing a 64-bit division for every sample block.
inline int64_t saturatingAutomationIncrement(int64_t increment, int64_t halfTicks) {
	constexpr int64_t limit = 2 * int64_t{UINT32_MAX};
	if (increment == static_cast<int32_t>(increment) && halfTicks == static_cast<int32_t>(halfTicks)) {
		return std::clamp(int64_t{static_cast<int32_t>(increment)} * static_cast<int32_t>(halfTicks), -limit, limit);
	}
	if (halfTicks == 0) {
		return 0;
	}
	if (increment > limit / halfTicks) {
		return limit;
	}
	if (increment < -limit / halfTicks) {
		return -limit;
	}
	return increment * halfTicks;
}
