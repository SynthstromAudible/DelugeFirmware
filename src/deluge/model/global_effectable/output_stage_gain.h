#pragma once
#include <cstdint>

/// The fixed gain stages a Sound's output passes through after Sound::render() but before the master FX in
/// AudioEngine::renderSongFX(): a kit's in GlobalEffectableForClip::renderOutput(), and the song's in
/// Song::renderAudio(). The sample preview renders outside the song and skips both, so these are shared with the
/// sample browser to let a preview match a sample loaded into the current instrument (issue #1591).

/// Song::renderAudio() halves the gain from the song's UNPATCHED_VOLUME before applying it to every output.
constexpr int32_t kSongVolumeAdjustmentShift = 1;
/// GlobalEffectableForClip::renderOutput() halves the gain from UNPATCHED_VOLUME, because the preset default already
/// doubles it.
constexpr int32_t kClipVolumeAdjustmentShift = 1;
/// renderOutput() then raises a kit's post-FX volume by volumeAdjustment >> this, so default filter resonance doesn't
/// reduce volume overall.
constexpr int32_t kKitPostFXBaselineShift = 2;
/// FilterSet::setConfig() scales every filter set's gain by this, applied as multiply_32x32_rshift32() << 1 (~0.801),
/// whether or not any filter is on.
constexpr int32_t kFilterSetGainAdjust = 1720000000;

namespace output_stage_gain_detail {
constexpr uint64_t isqrt(uint64_t n) {
	uint64_t x = n;
	uint64_t y = (x + 1) / 2;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2;
	}
	return x;
}

/// Convert a linear gain where 2^27 is unity - the neutral post-FX volume processReverbSendAndVolume() treats as
/// unity - to the volume-param pos domain of util/volume_param_gain.h, where 2^29 is unity.
constexpr int32_t linearGainToPos(int64_t gain) {
	// pos = sqrt(gain / 2^27) * 2^29 = sqrt(gain * 2^31)
	return static_cast<int32_t>(isqrt(static_cast<uint64_t>(gain) << 31));
}
} // namespace output_stage_gain_detail

/// The gain the song's volume stage applies at neutral song UNPATCHED_VOLUME - 0.5, or -6 dB - in the pos domain.
constexpr int32_t songOutputStageGainPos() {
	return output_stage_gain_detail::linearGainToPos(int64_t{134217728} >> kSongVolumeAdjustmentShift);
}

/// The gain a kit's output stage applies at neutral kit UNPATCHED_VOLUME with its filters off - about 0.5, or -6 dB -
/// in the pos domain.
constexpr int32_t kitOutputStageGainPos() {
	int64_t gain = int64_t{134217728} >> kClipVolumeAdjustmentShift;
	gain += gain >> kKitPostFXBaselineShift;
	gain = ((gain * kFilterSetGainAdjust) >> 32) << 1;
	return output_stage_gain_detail::linearGainToPos(gain);
}
