#include "CppUTest/TestHarness.h"
#include "model/global_effectable/output_stage_gain.h"
#include "util/volume_param_gain.h"

#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {
constexpr int32_t kMinParam = std::numeric_limits<int32_t>::min();
constexpr int32_t kMaxParam = std::numeric_limits<int32_t>::max();

// The default Sound volume, getParamFromUserValue(GLOBAL_VOLUME_POST_FX, 40).
constexpr int32_t kDefaultVolumeParam = 0x50000000;

// Mirror of util/functions.cpp getFinalParameterValueVolume() with paramNeutralValue == 2^27, the neutral value
// for GLOBAL_VOLUME_POST_FX and LOCAL_VOLUME. Used to check the *delivered gain*, not just the param value.
int64_t finalGain(int32_t volumeParam) {
	int64_t pos = static_cast<int64_t>(volumeParam >> 2) + 536870912;
	int64_t squared = (pos >> 16) * (pos >> 15);
	int64_t result = ((squared * 134217728) >> 32) << 5;
	return result > 2147483647 ? 2147483647 : result;
}
} // namespace

TEST_GROUP(VolumeParamGainTests){};

TEST(VolumeParamGainTests, unityGainIsIdentity) {
	// pos() truncates the low 2 bits, exactly as the patcher does.
	VolumeParamGain result = applyGainToVolumeParam(kDefaultVolumeParam, kVolumeParamUnityPos);
	CHECK_EQUAL(kDefaultVolumeParam & ~3, result.param);
	CHECK_EQUAL(kVolumeParamUnityPos, result.remainingGainPos);
}

TEST(VolumeParamGainTests, zeroGainYieldsSilence) {
	// A kit whose volume is at minimum has gainPos == 0. This is the case the old code got backwards.
	VolumeParamGain result = applyGainToVolumeParam(kDefaultVolumeParam, 0);
	CHECK_EQUAL(kMinParam, result.param);
	CHECK_EQUAL(0, finalGain(result.param));
}

TEST(VolumeParamGainTests, attenuationIsExact) {
	// A quiet kit: UNPATCHED_VOLUME == -2100000000 is roughly -66 dB. The old code applied no attenuation at all.
	int32_t gainPos = volumeParamToGainPos(-2100000000);
	VolumeParamGain result = applyGainToVolumeParam(0, gainPos);
	// Delivered gain must equal the kit's own gain, since the target param was neutral.
	CHECK_EQUAL(finalGain(-2100000000), finalGain(result.param));
	CHECK_EQUAL(kVolumeParamUnityPos, result.remainingGainPos);
}

TEST(VolumeParamGainTests, maximumGainSaturatesWithoutOverflow) {
	VolumeParamGain result = applyGainToVolumeParam(kMaxParam, kVolumeParamMaxPos);
	CHECK_EQUAL(kMaxParam, result.param);
	// Saturated, so some gain could not be applied.
	CHECK(result.remainingGainPos > kVolumeParamUnityPos);
}

TEST(VolumeParamGainTests, gainFromNeutralParamIsUnity) {
	CHECK_EQUAL(kVolumeParamUnityPos, volumeParamToGainPos(0));
}

TEST(VolumeParamGainTests, gainFromMinimumParamIsZero) {
	CHECK_EQUAL(0, volumeParamToGainPos(kMinParam));
}

TEST(VolumeParamGainTests, spillCarriesRemainderWhenLocalVolumeSaturates) {
	// Drum with LOCAL_VOLUME already boosted, kit at maximum: LOCAL_VOLUME saturates and the remaining gain must
	// be carried out rather than silently dropped.
	int32_t kitGainPos = volumeParamToGainPos(kMaxParam);
	VolumeParamGain local = applyGainToVolumeParam(kDefaultVolumeParam, kitGainPos);
	CHECK_EQUAL(kMaxParam, local.param);
	CHECK(local.remainingGainPos > kVolumeParamUnityPos);

	// The carried remainder still boosts the second param.
	VolumeParamGain postFX = applyGainToVolumeParam(0, local.remainingGainPos);
	CHECK(finalGain(postFX.param) > finalGain(0));
}

TEST(VolumeParamGainTests, neutralLocalVolumeAbsorbsFullKitRange) {
	// LOCAL_VOLUME at neutral has exactly the 4x headroom the kit's maximum gain demands, so nothing ever spills.
	int32_t kitGainPos = volumeParamToGainPos(kMaxParam);
	VolumeParamGain local = applyGainToVolumeParam(0, kitGainPos);
	CHECK_EQUAL(kVolumeParamUnityPos, local.remainingGainPos);
	CHECK_EQUAL(finalGain(kMaxParam), finalGain(local.param));
}

TEST(VolumeParamGainTests, kitOutputStageGainMatchesRenderChain) {
	// Mirror GlobalEffectableForClip::renderOutput() for a kit at neutral volume with its filters off: halve the
	// volume gain, add a quarter back, then FilterSet::setConfig()'s gain adjustment. 2^27 is unity.
	int64_t volumeAdjustment = int64_t{134217728} >> 1;
	int64_t volumePostFX = volumeAdjustment + (volumeAdjustment >> 2);
	volumePostFX = ((volumePostFX * 1720000000) >> 32) << 1;

	// Applied to a neutral LOCAL_VOLUME, the stage gain must deliver the same final gain as the kit chain.
	VolumeParamGain result = applyGainToVolumeParam(0, kitOutputStageGainPos());
	CHECK_EQUAL(kVolumeParamUnityPos, result.remainingGainPos);
	CHECK(std::abs(finalGain(result.param) - volumePostFX) <= volumePostFX / 10000);
}

TEST(VolumeParamGainTests, kitOutputStageIsAboutMinusSixDB) {
	// The gap the preview used to miss (issue #1591): about half the gain, i.e. 1/sqrt(2) in the pos domain.
	int32_t pos = kitOutputStageGainPos();
	CHECK(pos > 379000000);
	CHECK(pos < 381000000);
}

TEST(VolumeParamGainTests, songOutputStageHalvesGain) {
	// Song::renderAudio() halves the song volume's gain. Applied to a neutral LOCAL_VOLUME, that must deliver half the
	// neutral final gain.
	VolumeParamGain result = applyGainToVolumeParam(0, songOutputStageGainPos());
	CHECK(std::abs(finalGain(result.param) - (finalGain(0) >> 1)) <= finalGain(0) / 10000);
}

TEST(VolumeParamGainTests, multiplyGainPosMultipliesLinearGains) {
	CHECK_EQUAL(kVolumeParamUnityPos, multiplyGainPos(kVolumeParamUnityPos, kVolumeParamUnityPos));
	CHECK_EQUAL(0, multiplyGainPos(0, kVolumeParamMaxPos));

	// Song and kit stages at neutral: about a quarter of the gain, -12 dB, which is what the preview used to skip.
	int32_t combined = multiplyGainPos(songOutputStageGainPos(), kitOutputStageGainPos());
	VolumeParamGain result = applyGainToVolumeParam(0, combined);
	int64_t expected = finalGain(0) / 4;
	CHECK(std::abs(finalGain(result.param) - expected) <= expected / 500);
}

TEST(VolumeParamGainTests, multiplyGainPosSaturates) {
	CHECK_EQUAL(std::numeric_limits<int32_t>::max(), multiplyGainPos(kMaxParam, kMaxParam));
}
