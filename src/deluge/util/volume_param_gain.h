#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

/// A volume param's final linear gain is a square law of the "pos" value the patcher builds in
/// Patcher::cableToLinearParamWithoutRangeAdjustment():
///
///     pos(param) = (param >> 2) + kVolumeParamUnityPos
///     finalGain  = paramNeutralValue * (pos / kVolumeParamUnityPos)^2
///
/// So multiplying a param's gain by G means scaling its pos by sqrt(G). And because a second volume param's gain is
/// produced by the same square law from the same neutral value, sqrt(G) is exactly that second param's own pos.
/// Applying one volume param's gain to another is therefore a single integer multiply - no dB conversion, no floating
/// point, and no special case for silence.

/// pos of a neutral param (value 0), i.e. unity gain. 2^29.
constexpr int32_t kVolumeParamUnityPos = 536870912;
/// pos of the largest representable param value, INT32_MAX.
constexpr int32_t kVolumeParamMaxPos = 1073741823;

struct VolumeParamGain {
	/// The new param value, saturated into int32_t.
	int32_t param;
	/// The portion of the requested gain that did not fit because `param` saturated, in the same pos domain as the
	/// input gain. Equal to kVolumeParamUnityPos when all of the requested gain was applied.
	int32_t remainingGainPos;
};

/// The gain a volume param contributes, expressed in the pos domain. A param at minimum yields 0 (silence).
constexpr int32_t volumeParamToGainPos(int32_t volumeParam) {
	return (volumeParam >> 2) + kVolumeParamUnityPos;
}

/// Scale `volumeParam`'s linear gain by `gainPos`, a gain in the pos domain where kVolumeParamUnityPos is unity.
/// A `gainPos` of 0 - a fully attenuated source - yields INT32_MIN, i.e. silence.
constexpr VolumeParamGain applyGainToVolumeParam(int32_t volumeParam, int32_t gainPos) {
	int64_t pos = static_cast<int64_t>(volumeParamToGainPos(volumeParam));
	int64_t scaledPos = (pos * static_cast<int64_t>(gainPos)) >> 29;

	if (scaledPos > kVolumeParamMaxPos) {
		// Saturated. Report how much gain we actually delivered so the caller can carry the rest elsewhere.
		int64_t remaining = (scaledPos << 29) / kVolumeParamMaxPos;
		return {std::numeric_limits<int32_t>::max(),
		        static_cast<int32_t>(std::min<int64_t>(remaining, std::numeric_limits<int32_t>::max()))};
	}

	int64_t param = (scaledPos - kVolumeParamUnityPos) << 2;
	param = std::clamp<int64_t>(param, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max());
	return {static_cast<int32_t>(param), kVolumeParamUnityPos};
}
