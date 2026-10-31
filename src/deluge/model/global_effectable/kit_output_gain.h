#pragma once
#include <cstdint>

/// The fixed gain stages a kit's output passes through in GlobalEffectableForClip::renderOutput(), on top of the
/// kit's own UNPATCHED_VOLUME. They're shared with the sample browser so a preview can match a sample loaded into a
/// kit row (issue #1591).

/// renderOutput() halves the gain from UNPATCHED_VOLUME, because the preset default already doubles it.
constexpr int32_t kClipVolumeAdjustmentShift = 1;
/// renderOutput() then raises a kit's post-FX volume by volumeAdjustment >> this, so default filter resonance doesn't
/// reduce volume overall.
constexpr int32_t kKitPostFXBaselineShift = 2;
/// FilterSet::setConfig() scales every filter set's gain by this, applied as multiply_32x32_rshift32() << 1 (~0.801),
/// whether or not any filter is on.
constexpr int32_t kFilterSetGainAdjust = 1720000000;

namespace kit_output_gain_detail {
constexpr uint64_t isqrt(uint64_t n) {
	uint64_t x = n;
	uint64_t y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return x;
}
} // namespace kit_output_gain_detail

/// The gain a kit's output stage applies at neutral UNPATCHED_VOLUME with its filters off - about 0.5, or -6 dB - in
/// the volume-param pos domain of util/volume_param_gain.h, where 2^29 is unity.
constexpr int32_t kitOutputStageGainPos() {
	// Linear gain where 2^27 is unity, the neutral value processReverbSendAndVolume() treats as unity.
	int64_t gain = int64_t{134217728} >> kClipVolumeAdjustmentShift;
	gain += gain >> kKitPostFXBaselineShift;
	gain = ((gain * kFilterSetGainAdjust) >> 32) << 1;
	// pos = sqrt(gain / 2^27) * 2^29 = sqrt(gain * 2^31)
	return static_cast<int32_t>(kit_output_gain_detail::isqrt(static_cast<uint64_t>(gain) << 31));
}
