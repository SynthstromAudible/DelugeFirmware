#include "model/fx/output_stage_gain.h"
#include "util/volume_param_gain.h"

#include <cstdint>
#include <limits>

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace {
constexpr int32_t kMinParam = std::numeric_limits<int32_t>::min();
constexpr int32_t kMaxParam = std::numeric_limits<int32_t>::max();

// The default Sound volume, plugin::preset_word(40).
constexpr int32_t kDefaultVolumeParam = 0x50000000;

// Mirror of dsp/fixed/curves/param_curve.h curve_square() with paramNeutralValue == 2^27, the neutral value
// for GLOBAL_VOLUME_POST_FX and LOCAL_VOLUME. Used to check the *delivered gain*, not just the param value.
int64_t finalGain(int32_t volumeParam) {
	int64_t pos = static_cast<int64_t>(volumeParam >> 2) + 536870912;
	int64_t squared = (pos >> 16) * (pos >> 15);
	int64_t result = ((squared * 134217728) >> 32) << 5;
	return result > 2147483647 ? 2147483647 : result;
}
} // namespace

// clang-format off
describe volume_param_gain_group("volume param gain", ${
	it("unity gain is identity", _ {
		// pos() truncates the low 2 bits, exactly as the patcher does.
		VolumeParamGain result = applyGainToVolumeParam(kDefaultVolumeParam, kVolumeParamUnityPos);
		expect(result.param).to_equal(kDefaultVolumeParam & ~3);
		expect(result.remainingGainPos).to_equal(kVolumeParamUnityPos);
	});

	it("zero gain yields silence", _ {
		// A kit whose volume is at minimum has gainPos == 0, which silences the target.
		VolumeParamGain result = applyGainToVolumeParam(kDefaultVolumeParam, 0);
		expect(result.param).to_equal(kMinParam);
		expect(finalGain(result.param)).to_equal(0);
	});

	it("attenuation is exact", _ {
		// A quiet kit: UNPATCHED_VOLUME == -2100000000 is roughly -66 dB, all of which must be applied.
		int32_t gainPos = volumeParamToGainPos(-2100000000);
		VolumeParamGain result = applyGainToVolumeParam(0, gainPos);
		// Delivered gain must equal the kit's own gain, since the target param was neutral.
		expect(finalGain(result.param)).to_equal(finalGain(-2100000000));
		expect(result.remainingGainPos).to_equal(kVolumeParamUnityPos);
	});

	it("maximum gain saturates without overflow", _ {
		VolumeParamGain result = applyGainToVolumeParam(kMaxParam, kVolumeParamMaxPos);
		expect(result.param).to_equal(kMaxParam);
		// Saturated, so some gain could not be applied.
		expect(result.remainingGainPos).to_be_greater_than(kVolumeParamUnityPos);
	});

	it("gain from neutral param is unity", _ {
		expect(volumeParamToGainPos(0)).to_equal(kVolumeParamUnityPos);
	});

	it("gain from minimum param is zero", _ {
		expect(volumeParamToGainPos(kMinParam)).to_equal(0);
	});

	it("spill carries remainder when local volume saturates", _ {
		// Drum with LOCAL_VOLUME already boosted, kit at maximum: LOCAL_VOLUME saturates and the remaining gain must
		// be carried out rather than silently dropped.
		int32_t kitGainPos = volumeParamToGainPos(kMaxParam);
		VolumeParamGain local = applyGainToVolumeParam(kDefaultVolumeParam, kitGainPos);
		expect(local.param).to_equal(kMaxParam);
		expect(local.remainingGainPos).to_be_greater_than(kVolumeParamUnityPos);

		// The carried remainder still boosts the second param.
		VolumeParamGain postFX = applyGainToVolumeParam(0, local.remainingGainPos);
		expect(finalGain(postFX.param)).to_be_greater_than(finalGain(0));
	});

	it("neutral local volume absorbs full kit range", _ {
		// LOCAL_VOLUME at neutral has exactly the 4x headroom the kit's maximum gain demands, so nothing ever spills.
		int32_t kitGainPos = volumeParamToGainPos(kMaxParam);
		VolumeParamGain local = applyGainToVolumeParam(0, kitGainPos);
		expect(local.remainingGainPos).to_equal(kVolumeParamUnityPos);
		expect(finalGain(local.param)).to_equal(finalGain(kMaxParam));
	});

	it("kit output stage gain matches render chain", _ {
		// Mirror a kit's track chain at neutral volume with its filters off: halve the volume gain, add a quarter back,
		// then the filter set's gain adjustment. 2^27 is unity.
		int64_t volumeAdjustment = int64_t{134217728} >> 1;
		int64_t volumePostFX = volumeAdjustment + (volumeAdjustment >> 2);
		volumePostFX = ((volumePostFX * 1720000000) >> 32) << 1;

		// Applied to a neutral LOCAL_VOLUME, the stage gain must deliver the same final gain as the kit chain.
		VolumeParamGain result = applyGainToVolumeParam(0, kitOutputStageGainPos());
		expect(result.remainingGainPos).to_equal(kVolumeParamUnityPos);
		expect(finalGain(result.param)).to_be_within(volumePostFX / 10000).of(volumePostFX);
	});

	it("kit output stage is about minus six dB", _ {
		// The kit stage the preview must apply (issue #1591): about half the gain, i.e. 1/sqrt(2) in the pos domain.
		int32_t pos = kitOutputStageGainPos();
		expect(pos).to_be_greater_than(379000000);
		expect(pos).to_be_less_than(381000000);
	});

	it("song output stage halves gain", _ {
		// Song::renderAudio() halves the song volume's gain. Applied to a neutral LOCAL_VOLUME, that must deliver half the
		// neutral final gain.
		VolumeParamGain result = applyGainToVolumeParam(0, songOutputStageGainPos());
		expect(finalGain(result.param)).to_be_within(finalGain(0) / 10000).of(finalGain(0) >> 1);
	});

	it("multiply gain pos multiplies linear gains", _ {
		expect(multiplyGainPos(kVolumeParamUnityPos, kVolumeParamUnityPos)).to_equal(kVolumeParamUnityPos);
		expect(multiplyGainPos(0, kVolumeParamMaxPos)).to_equal(0);

		// Song and kit stages at neutral: about a quarter of the gain, -12 dB, which the preview must apply.
		int32_t combined = multiplyGainPos(songOutputStageGainPos(), kitOutputStageGainPos());
		VolumeParamGain result = applyGainToVolumeParam(0, combined);
		int64_t expected = finalGain(0) / 4;
		expect(finalGain(result.param)).to_be_within(expected / 500).of(expected);
	});

	it("multiply gain pos saturates", _ {
		expect(multiplyGainPos(kMaxParam, kMaxParam)).to_equal(std::numeric_limits<int32_t>::max());
	});
});

DELUGE_SPEC(volume_param_gain_spec, volume_param_gain_group)
