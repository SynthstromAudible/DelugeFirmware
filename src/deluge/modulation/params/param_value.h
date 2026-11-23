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

#include "util/fixedpoint.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

// Advanced whenever any clip value or preset baseline changes. A curve may reuse
// its evaluated value while this is unchanged, without reading its baseline.
// Code which writes baseline storage directly must call parameterBaselinesChanged().
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline uint32_t parameter_baseline_epoch = 1;
// Advanced whenever any parameter value may have changed other than by the playback
// loop which advances it. Collections refresh their resolved render values when it moves.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
inline uint32_t parameter_value_epoch = 1;
#ifdef PARAMETER_RESOLVED_VERIFY
// Diagnostic counters for checking every resolved render read against the layers.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
extern "C" {
inline volatile uint32_t parameterVerifyReads = 0;
inline volatile uint32_t parameterVerifyMismatches = 0;
inline volatile uint32_t parameterVerifyLastMismatch = 0;
inline volatile uint32_t parameterVerifyBumps = 0;
inline volatile uint32_t parameterVerifyRefreshes = 0;
inline volatile uint32_t parameterVerifyRampTicks = 0;
inline volatile uint32_t parameterVerifyRampStores = 0;
}
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
#endif
inline void parameterValuesChanged() {
#ifdef PARAMETER_RESOLVED_VERIFY
	parameterVerifyBumps = parameterVerifyBumps + 1;
#endif
	// Zero marks resolved values as never filled, so skip it when wrapping.
	if (++parameter_value_epoch == 0) {
		parameter_value_epoch = 1;
	}
}
inline void parameterBaselinesChanged() {
	// Zero marks a curve value as unchecked, so skip it when wrapping.
	if (++parameter_baseline_epoch == 0) {
		parameter_baseline_epoch = 1;
	}
	parameterValuesChanged();
}

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
			parameterBaselinesChanged();
			return;
		}
		valueAtEdit_ = offset >= 0 ? INT32_MAX : INT32_MIN;
		presetAtEdit_ = static_cast<int32_t>(int64_t{valueAtEdit_} - offset);
		presetAndOffset_ = (presetAndOffset_ & ~uintptr_t{3}) | uintptr_t{offset != 0};
		parameterBaselinesChanged();
	}
	void setValue(int32_t value) {
		valueAtEdit_ = value;
		presetAtEdit_ = getPresetValue();
		presetAndOffset_ = (presetAndOffset_ & ~uintptr_t{3}) | uintptr_t{value != presetAtEdit_};
		parameterBaselinesChanged();
	}
	// Used when replacing a zero baseline slot with the permanent zero baseline.
	// Both baselines are equal, so retain even offsets hidden by saturation.
	void detachZeroPreset() {
		if (getPresetValue() == 0) {
			presetAndOffset_ &= uintptr_t{3};
			parameterBaselinesChanged();
		}
	}
	// Detach only from a zero baseline slot which is about to be reused for another route.
	void detachFromPreset(const int32_t* preset) {
		if (presetPointer() == preset) {
			detachZeroPreset();
		}
	}
	[[nodiscard]] bool isBoundTo(const int32_t* preset) const { return presetPointer() == preset; }
	void bindPreset(const int32_t* preset) {
		if (presetPointer() == preset) {
			return;
		}
		int64_t value = getUnclampedValue();
		// The aligned int32 pointer leaves a low bit for the zero-offset fast path.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		presetAndOffset_ = reinterpret_cast<uintptr_t>(preset);
		// Both branches below record the change.
		if (value == resolve(value)) {
			setValue(static_cast<int32_t>(value));
		}
		else {
			setUnclampedValue(value);
		}
	}

	// Adjust the offset before the shared baseline changes, retaining even the
	// portion of the scalar hidden by saturation. Other owners are untouched.
	void preserveBeforePresetChange(const int32_t* preset, int32_t newValue) {
		if (presetPointer() == preset) {
			setUnclampedValue(getUnclampedValue() + int64_t{*preset} - newValue);
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
	// Whether currentValue() is still valid: the curve was non-empty and evaluated
	// against its bound baseline, and no baseline anywhere has changed since.
	[[nodiscard]] bool isCurrent() const { return evaluatedEpoch_ == parameter_baseline_epoch; }
	[[nodiscard]] int32_t currentValue() const { return evaluatedValue_; }
	[[nodiscard]] int32_t currentBase() const { return evaluatedBase_; }
	// Only owners which have just evaluated their bound baseline may mark it.
	void markCurrent() const { evaluatedEpoch_ = parameter_baseline_epoch; }
	void invalidate() const { evaluatedEpoch_ = 0; }
	[[nodiscard]] int64_t getOffset() const { return offset_; }
	// Advance a current evaluation by an integer step. While the evaluated value is
	// not at a limit it equals base + offset exactly, so one saturating add gives
	// the same result as evaluating again.
	int32_t advanceCurrent(int32_t increment) {
		offset_ += increment;
		if (evaluatedValue_ != INT32_MIN && evaluatedValue_ != INT32_MAX) [[likely]] {
			evaluatedValue_ = add_saturate(evaluatedValue_, increment);
		}
		else {
			evaluate(evaluatedBase_);
		}
		return evaluatedValue_;
	}
	void setOffsetForBase(int64_t offset, int32_t base) {
		offset_ = offset;
		evaluate(base);
	}
	// Store a value an owner advanced to from currentValue() against the same base,
	// without reaching a limit, so the offset is exactly the difference.
	void commitAdvanced(int32_t value) {
		offset_ = int64_t{value} - evaluatedBase_;
		evaluatedValue_ = value;
	}

private:
	int64_t offset_ = 0;
	void evaluate(int32_t base) const {
		if (base != evaluatedBase_) {
			evaluatedEpoch_ = 0;
		}
		evaluatedBase_ = base;
		evaluatedValue_ = ClipParamValue::resolve(int64_t{base} + offset_);
	}
	mutable int32_t evaluatedBase_ = 0;
	mutable int32_t evaluatedValue_ = 0;
	mutable uint32_t evaluatedEpoch_ = 0;
};

// Avoid overflowing distance * elapsed for full-width curves and long clips.
inline int64_t interpolateAutomationOffset(int64_t left, int64_t right, int32_t elapsed, int32_t duration) {
	int64_t distance = right - left;
	return left + ((distance / duration) * elapsed) + ((distance % duration) * elapsed / duration);
}

// Multiplies a per-half-tick increment by a number of half ticks, clamping rather than wrapping. A steep
// increment (a short segment covering a big value change) times many skipped ticks easily overflows, and a
// wrapped increment would send the value in the wrong direction entirely.
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

// The increment for numSamples of interpolation at valueIncrementPerHalfTick, where inverse
// is the 32-bit fractional number of internal ticks per sample. Ordinary ramps and tempos
// take a single rounded 32-bit multiply; only an increment too large for 32 bits saturates.
// value * fraction / 2^32, rounded. A node offset can need far more than 32 bits, so the product can need more
// than 64; splitting value keeps every partial product within 64 bits.
inline int64_t multiplyByFractionRounded(int64_t value, uint32_t fraction) {
	int64_t high = value >> 32;
	uint64_t low = static_cast<uint32_t>(value);
	return (high * fraction) + static_cast<int64_t>(((low * fraction) + (uint64_t{1} << 31)) >> 32);
}

inline int64_t automationSampleIncrement(int64_t perHalfTick, uint32_t inverse, int32_t numSamples) {
	if (perHalfTick == static_cast<int32_t>(perHalfTick) && inverse <= INT32_MAX) [[likely]] {
		int32_t rate =
		    multiply_32x32_rshift32_rounded(static_cast<int32_t>(perHalfTick), static_cast<int32_t>(inverse));
		int32_t scaled_samples = 6 * numSamples;
		int64_t increment = int64_t{rate} * scaled_samples;
		if (increment == static_cast<int32_t>(increment)) [[likely]] {
			return increment;
		}
		return saturatingAutomationIncrement(rate, int64_t{6} * numSamples);
	}
	return saturatingAutomationIncrement(multiplyByFractionRounded(perHalfTick, inverse), int64_t{6} * numSamples);
}
