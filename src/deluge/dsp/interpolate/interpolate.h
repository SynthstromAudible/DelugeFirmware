#pragma once
#include "definitions_cxx.hpp"
#include "deluge/dsp/stereo_sample.h"
#include <array>
#include <cstdint>

extern const int16_t windowedSincKernel[][17][16];

namespace deluge::dsp {
struct [[gnu::hot]] Interpolator {

	Interpolator() = default;
	StereoSample interpolate(size_t channels, int32_t whichKernel, uint32_t oscPos);
	StereoSample interpolateLinear(size_t channels, uint32_t oscPos);

	/// The i-th most recent sample, where 0 is the newest
	[[nodiscard]] constexpr int16_t l(size_t i) const { return history_l_[head_ + i]; }
	[[nodiscard]] constexpr int16_t r(size_t i) const { return history_r_[head_ + i]; }

	constexpr void setL(size_t i, int16_t value) { set(history_l_, i, value); }
	constexpr void setR(size_t i, int16_t value) { set(history_r_, i, value); }

	/// Adds a newer sample, dropping the oldest. For mono, @p right is ignored.
	[[gnu::always_inline]] constexpr void push(int16_t left, int16_t right) {
		jumpForward(1);
		setL(0, left);
		setR(0, right);
	}

	/// Makes room for @p num_samples newer samples, dropping the oldest. The caller must then set all of them, from
	/// setL(num_samples - 1) for the oldest of the new ones down to setL(0) for the newest.
	[[gnu::always_inline]] constexpr void jumpForward(size_t num_samples) {
		head_ = (head_ - num_samples) & (kInterpolationMaxNumSamples - 1);
	}

private:
	static_assert((kInterpolationMaxNumSamples & (kInterpolationMaxNumSamples - 1)) == 0,
	              "set() relies on this being a power of two");

	using History = std::array<int16_t, 2 * kInterpolationMaxNumSamples>;

	[[gnu::always_inline]] constexpr void set(History& history, size_t i, int16_t value) const {
		size_t pos = head_ + i; // Below 2 * kInterpolationMaxNumSamples, so flipping this bit finds its other copy
		history[pos] = value;
		history[pos ^ kInterpolationMaxNumSamples] = value;
	}

	// These are ring buffers, holding each sample twice, kInterpolationMaxNumSamples apart. That way the latest
	// kInterpolationMaxNumSamples are always in order from head_ onwards, however far round head_ has got - so
	// interpolate() can load them straight into NEON registers, and adding a sample doesn't mean shuffling all the
	// others along.
	// Value-initialised: interpolate() reads the full kInterpolationMaxNumSamples-tap window via SIMD, so any tap not
	// yet set must be a defined 0 rather than whatever the allocation left behind (the live-input pitch-shifter feeds
	// these incrementally on warm-up).
	alignas(sizeof(int32_t) * 4) History history_l_{};
	alignas(sizeof(int32_t) * 4) History history_r_{};
	size_t head_ = 0;
};
} // namespace deluge::dsp
