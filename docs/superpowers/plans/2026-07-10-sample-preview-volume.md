# Sample-Preview Volume Matching (#1591) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the sample-browser preview sound at the volume the sample will have once loaded into the target synth or kit row, replacing the branch's floating-point dB approach with exact integer math.

**Architecture:** A volume param's final linear gain is a square law of its patcher "pos" value (`pos = (param >> 2) + 2^29`, `gain ∝ pos²`). Applying one volume param's gain to another therefore reduces to a single integer multiply, `posNew = (posA * posB) >> 29` — no `log10f`, no `shiftVolumeByDB`, no floating point, and no zero-guard. The kit's gain is folded into `LOCAL_VOLUME` first (it sits at neutral by default, so it has exactly the 4× of headroom the kit's gain can demand), with any saturated remainder spilling into `GLOBAL_VOLUME_POST_FX`.

**Tech Stack:** C++20, CppUTest, CMake + Ninja, `dbt` build wrapper.

## Global Constraints

- Firmware builds with `./dbt build Debug` (enables beta asserts).
- Unit tests configure with `cmake -B build-tests -S tests -G Ninja`, build with `cmake --build build-tests --target UnitTests`, run with `./build-tests/unit/UnitTests`.
- Baseline before any change: **110 tests, 0 failures**.
- `util/functions.cpp` is NOT in the unit-test link set (it pulls in `fatfs`, `display`, `sound.h`). Anything that needs a unit test must be header-only.
- Repo has no CLAUDE.md; follow surrounding style. `clang-format` runs as a pre-commit hook.
- Spec: `docs/superpowers/specs/2026-07-10-sample-preview-volume-design.md`. Defect numbers below refer to it.

---

## File Structure

| File | Responsibility |
|---|---|
| `src/deluge/util/volume_param_gain.h` | **New.** Header-only, pure: the square-law gain math. One responsibility, no deps beyond `<cstdint>`/`<algorithm>`/`<limits>`. |
| `tests/unit/volume_param_gain_tests.cpp` | **New.** Unit tests for the above. |
| `tests/unit/CMakeLists.txt` | Register the new test file. |
| `src/deluge/processing/sound/sound.h` | Shared default-volume constants (defect 5). |
| `src/deluge/processing/sound/sound.cpp` | Use the constants in `initParams` / `setupAsSample`. |
| `src/deluge/processing/engines/audio_engine.h` | `PreviewVolume` struct + new `previewSample` signature (defect 8). |
| `src/deluge/processing/engines/audio_engine.cpp` | Apply `PreviewVolume` to the preview drum. |
| `src/deluge/gui/ui/browser/sample_browser.cpp` | Build `PreviewVolume` from the target (defects 1, 2, 6, 7). |
| `src/deluge/util/db_shift.h` | **New, Task 3.** `shiftVolumeByDB` moved out of `functions.cpp` so it can be tested, then fixed (defect 3). |
| `tests/unit/db_shift_tests.cpp` | **New, Task 3.** |

---

## Task 1: The gain math

Pure, header-only, unit-tested. Nothing else depends on it yet.

**Files:**
- Create: `src/deluge/util/volume_param_gain.h`
- Create: `tests/unit/volume_param_gain_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt` (add to `add_executable(UnitTests ...)` list)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `constexpr int32_t kVolumeParamUnityPos = 536870912;`
  - `constexpr int32_t kVolumeParamMaxPos = 1073741823;`
  - `struct VolumeParamGain { int32_t param; int32_t remainingGainPos; };`
  - `constexpr VolumeParamGain applyGainToVolumeParam(int32_t volumeParam, int32_t gainPos);`
  - `constexpr int32_t volumeParamToGainPos(int32_t volumeParam);`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/volume_param_gain_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "util/volume_param_gain.h"

#include <cstdint>
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
```

Register it in `tests/unit/CMakeLists.txt` — add `volume_param_gain_tests.cpp` to the `add_executable(UnitTests ...)` source list, after `function_tests.cpp`:

```cmake
add_executable(UnitTests
        RunAllTests.cpp
        scheduler_tests.cpp
        lfo_tests.cpp
        scale_tests.cpp
        value_scaling_tests.cpp
        clip_iterator_tests.cpp
        function_tests.cpp
        volume_param_gain_tests.cpp
        sync_tests.cpp
        chord_tests.cpp
        time_tests.cpp
        clock_output_scheduler_tests.cpp
)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake -B build-tests -S tests -G Ninja && cmake --build build-tests --target UnitTests
```

Expected: **compile error**, `fatal error: util/volume_param_gain.h: No such file or directory`.

- [ ] **Step 3: Write minimal implementation**

Create `src/deluge/util/volume_param_gain.h`:

```cpp
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
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build-tests --target UnitTests && ./build-tests/unit/UnitTests
```

Expected: `OK (118 tests, 118 ran, ...)` — 110 baseline plus 8 new, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add src/deluge/util/volume_param_gain.h tests/unit/volume_param_gain_tests.cpp tests/unit/CMakeLists.txt
git commit -m "Add exact integer gain math for volume params"
```

---

## Task 2: The #1591 fix

Replaces the branch's dB approach. Resolves defects 1, 2, 5, 6, 7, 8; documents 4.

**Files:**
- Modify: `src/deluge/processing/sound/sound.h` (add constants near top, after includes)
- Modify: `src/deluge/processing/sound/sound.cpp:141-145` (`initParams`), `:214-216` (`setupAsSample`)
- Modify: `src/deluge/processing/engines/audio_engine.h:145-149`
- Modify: `src/deluge/processing/engines/audio_engine.cpp:1318-1362` (`previewSample`)
- Modify: `src/deluge/gui/ui/browser/sample_browser.cpp:585-617` (`previewIfPossible`)

**Interfaces:**
- Consumes: `applyGainToVolumeParam`, `volumeParamToGainPos`, `kVolumeParamUnityPos`, `VolumeParamGain` from Task 1.
- Produces:
  - `constexpr int32_t kDefaultSoundVolumeUserValue = 40;` (in `sound.h`)
  - `constexpr int32_t kSampleDrumVelocityCableUserValue = 50;` (in `sound.h`)
  - `struct AudioEngine::PreviewVolume { int32_t volumePostFX; int32_t localVolume; int32_t velocityCableDepth; static PreviewVolume neutral(); };`
  - `void AudioEngine::previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound, PreviewVolume const& volume);`

- [ ] **Step 1: Add the shared default constants**

In `src/deluge/processing/sound/sound.h`, immediately before `class Sound`:

```cpp
/// The default volume every Sound gets from Sound::initParams(), as a user value (0-50).
constexpr int32_t kDefaultSoundVolumeUserValue = 40;
/// The depth of the VELOCITY -> LOCAL_VOLUME patch cable that Sound::setupAsSample() installs, as a user value.
constexpr int32_t kSampleDrumVelocityCableUserValue = 50;
```

In `src/deluge/processing/sound/sound.cpp`, `Sound::initParams` (currently lines 144-145), replace the literal `40`:

```cpp
	patchedParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(
	    getParamFromUserValue(params::GLOBAL_VOLUME_POST_FX, kDefaultSoundVolumeUserValue));
```

In `Sound::setupAsSample` (currently lines 214-216), replace the literal `50`:

```cpp
	paramManager->getPatchCableSet()->patchCables[0].setup(
	    PatchSource::VELOCITY, params::LOCAL_VOLUME,
	    getParamFromUserValue(params::PATCH_CABLE, kSampleDrumVelocityCableUserValue));
```

- [ ] **Step 2: Replace the previewSample signature**

In `src/deluge/processing/engines/audio_engine.h`, replace the current declaration (`void previewSample(String*, FilePointer*, bool, bool, int32_t, int32_t, float);`) with:

```cpp
/// The volume context for a sample preview: the params the preview drum should adopt so that it sounds like the
/// instrument or kit-row the sample will be loaded into (issue #1591). Values are already fully resolved - any kit
/// output volume has been folded in by the caller.
struct PreviewVolume {
	int32_t volumePostFX;       ///< params::GLOBAL_VOLUME_POST_FX
	int32_t localVolume;        ///< params::LOCAL_VOLUME
	int32_t velocityCableDepth; ///< depth of the VELOCITY -> LOCAL_VOLUME patch cable

	/// The defaults the preview drum is built with, from Sound::initParams() and Sound::setupAsSample().
	static PreviewVolume neutral();
};

void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound, PreviewVolume const& volume);
```

No default argument: `previewSample` has exactly one caller, and a default would let a future caller silently get neutral volume.

- [ ] **Step 3: Apply PreviewVolume in the engine**

In `src/deluge/processing/engines/audio_engine.cpp`, add above `previewSample`:

```cpp
PreviewVolume PreviewVolume::neutral() {
	return {
	    .volumePostFX = getParamFromUserValue(params::GLOBAL_VOLUME_POST_FX, kDefaultSoundVolumeUserValue),
	    .localVolume = 0,
	    .velocityCableDepth = getParamFromUserValue(params::PATCH_CABLE, kSampleDrumVelocityCableUserValue),
	};
}
```

Replace the whole `if (shouldActuallySound) {` volume block (the code added by this branch, from the `PatchedParamSet* previewParams` line through the `recalculatePatchingToParam` line) with:

```cpp
	if (shouldActuallySound) {
		// Adopt the volume of the instrument / kit-row the sample will be loaded into, so the preview sounds the same
		// before and after selecting it (issue #1591). The song master volume is applied downstream in renderSongFX().
		PatchedParamSet* previewParams = paramManagerForSamplePreview->getPatchedParamSet();
		previewParams->params[params::GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(volume.volumePostFX);
		previewParams->params[params::LOCAL_VOLUME].setCurrentValueBasicForSetup(volume.localVolume);

		// setupAsSample() gives the preview drum exactly one patch cable: VELOCITY -> LOCAL_VOLUME.
		paramManagerForSamplePreview->getPatchCableSet()->patchCables[0].param.setCurrentValueBasicForSetup(
		    volume.velocityCableDepth);

		// GLOBAL_VOLUME_POST_FX is a Sound-level param with no patch cables on the preview drum, so its final value
		// won't update on its own - recompute it before we note-on. LOCAL_VOLUME is per-voice and is picked up when
		// the new voice is patched during noteOn().
		sampleForPreview->recalculatePatchingToParam(params::GLOBAL_VOLUME_POST_FX, paramManagerForSamplePreview);

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		// ... existing modelStack + noteOn + bypassCulling lines, unchanged ...
	}
```

Add `#include "util/volume_param_gain.h"` is **not** needed here — the engine no longer does gain math.

- [ ] **Step 4: Build the PreviewVolume at the call site**

In `src/deluge/gui/ui/browser/sample_browser.cpp`, add to the include block:

```cpp
#include "util/volume_param_gain.h"
```

Add a file-local helper above `SampleBrowser::previewIfPossible`:

```cpp
namespace {
/// The depth of the target Sound's VELOCITY -> LOCAL_VOLUME patch cable, or 0 if it has none. The preview drum always
/// notes on at velocity 64, so matching this depth makes its final per-voice volume match the target's.
int32_t targetVelocityToVolumeCableDepth(ParamManagerForTimeline* paramManager) {
	PatchCableSet* cables = paramManager->getPatchCableSet();
	for (int32_t c = 0; c < cables->numPatchCables; c++) {
		PatchCable& cable = cables->patchCables[c];
		if (cable.from == PatchSource::VELOCITY && cable.destinationParamDescriptor.isJustAParam()
		    && cable.destinationParamDescriptor.getJustTheParam() == params::LOCAL_VOLUME) {
			return cable.param.getCurrentValue();
		}
	}
	return 0;
}
} // namespace
```

Replace the whole volume-computation block this branch added (from `bool matchTargetVolume = false;` through the `AudioEngine::previewSample(...)` call) with:

```cpp
		// Work out the volume the sample will play at once it's loaded into the current instrument / kit-row, so the
		// preview can match it (issue #1591). The song master volume is applied to the preview downstream, so we only
		// need the target Sound's own volume, plus - for a kit - the kit's output volume.
		//
		// Not modelled: the kit's x1.25 postFX baseline and setupFilterSetConfig()'s resonance scaling, both applied
		// in GlobalEffectableForClip::renderOutput(). A resonant kit will still preview slightly off. Modelling them
		// would couple the preview path to the kit render chain.
		AudioEngine::PreviewVolume previewVolume = AudioEngine::PreviewVolume::neutral();

		if (shouldActuallySound && soundEditor.currentSound != nullptr && soundEditor.currentParamManager != nullptr
		    && soundEditor.currentParamManager->containsAnyMainParamCollections()) {
			PatchedParamSet* targetParams = soundEditor.currentParamManager->getPatchedParamSet();
			previewVolume.volumePostFX = targetParams->getValue(params::GLOBAL_VOLUME_POST_FX);
			previewVolume.localVolume = targetParams->getValue(params::LOCAL_VOLUME);
			previewVolume.velocityCableDepth = targetVelocityToVolumeCableDepth(soundEditor.currentParamManager);

			if (getCurrentOutputType() == OutputType::KIT) {
				ParamManagerForTimeline& kitParamManager = getCurrentInstrumentClip()->paramManager;
				if (kitParamManager.containsAnyParamCollectionsIncludingExpression()) {
					int32_t kitVolumeParam = kitParamManager.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME);
					int32_t kitGainPos = volumeParamToGainPos(kitVolumeParam);

					// Fold the kit's output volume into LOCAL_VOLUME first: it sits at neutral by default, so it has
					// exactly the 4x of headroom the kit's maximum gain demands. Anything that doesn't fit - only
					// possible when the drum's own local volume is already boosted - spills into GLOBAL_VOLUME_POST_FX.
					VolumeParamGain local = applyGainToVolumeParam(previewVolume.localVolume, kitGainPos);
					previewVolume.localVolume = local.param;
					previewVolume.volumePostFX =
					    applyGainToVolumeParam(previewVolume.volumePostFX, local.remainingGainPos).param;
				}
			}
		}

		AudioEngine::previewSample(&filePath, &currentFileItem->filePointer, shouldActuallySound, previewVolume);
```

Note the `containsAnyParamCollectionsIncludingExpression()` guard is retained deliberately: it is exactly the predicate `getUnpatchedParamSet()` asserts on internally (`E410`, `param_manager.h:97-103`).

- [ ] **Step 5: Verify the firmware builds**

```bash
./dbt build Debug
```

Expected: builds clean, no warnings about the touched files.

- [ ] **Step 6: Verify the unit tests still pass**

```bash
cmake --build build-tests --target UnitTests && ./build-tests/unit/UnitTests
```

Expected: `OK (118 tests, 118 ran, ...)`.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/gui/ui/browser/sample_browser.cpp src/deluge/processing/engines/audio_engine.cpp \
        src/deluge/processing/engines/audio_engine.h src/deluge/processing/sound/sound.cpp \
        src/deluge/processing/sound/sound.h
git commit -m "Match sample-browser preview volume to the target instrument (#1591)"
```

---

## Task 3: Fix `shiftVolumeByDB` (independent)

**Not required by the #1591 fix** — the new code never calls this helper. This is a standalone correctness fix to a shared helper whose only remaining callers are the two song-load compatibility paths (`sound.cpp:3286`, `kit.cpp:1106`). It changes the resonance compensation applied to pre-1.2.0 songs on load, altering the audio of existing user songs. Kept in its own commit so it can be split off or reverted independently.

To make it testable it must leave `functions.cpp` (which cannot link into the unit tests). Moving it to a header is behaviour-preserving: `functions.h` includes the new header, so no caller changes.

**Files:**
- Create: `src/deluge/util/db_shift.h`
- Modify: `src/deluge/util/functions.cpp` (remove `dbIntervals` and `shiftVolumeByDB`, lines 438-490)
- Modify: `src/deluge/util/functions.h` (remove the `shiftVolumeByDB` declaration at line 235, add `#include "util/db_shift.h"`)
- Create: `tests/unit/db_shift_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `inline int32_t shiftVolumeByDB(int32_t oldValue, float offset);` in `util/db_shift.h`.

- [ ] **Step 1: Move the function verbatim, no behaviour change**

Create `src/deluge/util/db_shift.h` containing the *current, unmodified* `dbIntervals` table and `shiftVolumeByDB` body from `functions.cpp:438-490`, with `shiftVolumeByDB` marked `inline` and `dbIntervals` marked `constexpr`. Delete both from `functions.cpp`. Delete the declaration from `functions.h:235` and add `#include "util/db_shift.h"` there instead.

Verify with `./dbt build Debug` — must build clean. Commit this move separately so the behaviour change in Step 3 is reviewable on its own:

```bash
git add src/deluge/util/db_shift.h src/deluge/util/functions.h src/deluge/util/functions.cpp
git commit -m "Move shiftVolumeByDB into a header so it can be unit tested"
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/db_shift_tests.cpp`:

```cpp
#include "CppUTest/TestHarness.h"
#include "util/db_shift.h"

#include <cstdint>
#include <limits>

TEST_GROUP(DbShiftTests){};

TEST(DbShiftTests, positiveOffsetIncreasesValue) {
	int32_t oldValue = 0;
	CHECK(shiftVolumeByDB(oldValue, 3.0f) > oldValue);
}

TEST(DbShiftTests, negativeOffsetDecreasesValue) {
	// The bug: negative offsets converted a negative float to uint32_t (UB) and the attenuation was discarded.
	int32_t oldValue = 0;
	CHECK(shiftVolumeByDB(oldValue, -3.0f) < oldValue);
}

TEST(DbShiftTests, zeroOffsetIsIdentity) {
	int32_t oldValue = 0x50000000;
	CHECK_EQUAL(oldValue, shiftVolumeByDB(oldValue, 0.0f));
}

TEST(DbShiftTests, intraIntervalPositionIsPreserved) {
	// The bug: howFarUpIntervalFloat used integer division and was always 0.0, so any value partway up an interval
	// was snapped down to the interval floor. Two values in the same interval must stay ordered after a shift.
	int32_t low = 0x50000000;
	int32_t high = 0x5F000000;
	CHECK(low < high);
	CHECK(shiftVolumeByDB(low, 1.0f) < shiftVolumeByDB(high, 1.0f));
}

TEST(DbShiftTests, largeNegativeOffsetSaturatesToSilence) {
	CHECK_EQUAL(std::numeric_limits<int32_t>::min(), shiftVolumeByDB(0, -500.0f));
}

TEST(DbShiftTests, largePositiveOffsetSaturatesToMaximum) {
	CHECK_EQUAL(std::numeric_limits<int32_t>::max(), shiftVolumeByDB(0, 500.0f));
}

TEST(DbShiftTests, roundTripApproximatelyRestores) {
	int32_t oldValue = 0x50000000;
	int32_t shifted = shiftVolumeByDB(oldValue, 6.0f);
	int32_t restored = shiftVolumeByDB(shifted, -6.0f);
	// Allow for fixed-point rounding across two conversions.
	CHECK(std::abs(static_cast<int64_t>(restored) - oldValue) < (1 << 20));
}
```

Register `db_shift_tests.cpp` in `tests/unit/CMakeLists.txt`'s `add_executable(UnitTests ...)` list, after `volume_param_gain_tests.cpp`.

- [ ] **Step 3: Run tests to verify they fail**

```bash
cmake -B build-tests -S tests -G Ninja && cmake --build build-tests --target UnitTests && ./build-tests/unit/UnitTests
```

Expected: `negativeOffsetDecreasesValue`, `intraIntervalPositionIsPreserved`, `largeNegativeOffsetSaturatesToSilence`, and `roundTripApproximatelyRestores` FAIL. `zeroOffsetIsIdentity` may also fail (the interval-floor snap).

- [ ] **Step 4: Fix the implementation**

Replace `shiftVolumeByDB` in `src/deluge/util/db_shift.h` with:

```cpp
/// Shift a volume param value by `offset` dB. Positive offsets raise it, negative offsets lower it; both saturate.
/// The value's position is linear in dB within each of the 16 intervals of `dbIntervals`.
inline int32_t shiftVolumeByDB(int32_t oldValue, float offset) {
	uint32_t oldValuePositive = static_cast<uint32_t>(oldValue) + 2147483648u;
	uint32_t currentInterval = oldValuePositive >> 28;

	// Interval 0 is not a real dB interval - nothing to shift within it.
	if (currentInterval < 1) {
		return oldValue;
	}

	float howFarUpInterval = static_cast<float>(oldValuePositive & 268435455u) / 268435456.0f;

	while (true) {
		float dbThisInterval = dbIntervals[currentInterval];

		if (offset >= 0.0f) {
			float dbLeftThisInterval = (1.0f - howFarUpInterval) * dbThisInterval;
			if (dbLeftThisInterval > offset) {
				howFarUpInterval += offset / dbThisInterval;
				break;
			}
			if (currentInterval == 15) {
				return std::numeric_limits<int32_t>::max();
			}
			offset -= dbLeftThisInterval;
			currentInterval++;
			howFarUpInterval = 0.0f;
		}
		else {
			float dbLeftThisInterval = howFarUpInterval * dbThisInterval;
			if (dbLeftThisInterval > -offset) {
				howFarUpInterval += offset / dbThisInterval;
				break;
			}
			if (currentInterval == 1) {
				return std::numeric_limits<int32_t>::min();
			}
			offset += dbLeftThisInterval;
			currentInterval--;
			howFarUpInterval = 1.0f;
		}
	}

	howFarUpInterval = std::clamp(howFarUpInterval, 0.0f, 0.99999994f);
	uint32_t newValuePositive =
	    (currentInterval << 28) + static_cast<uint32_t>(howFarUpInterval * 268435456.0f);
	return static_cast<int32_t>(newValuePositive - 2147483648u);
}
```

Add `#include <algorithm>` and `#include <limits>` to `db_shift.h`.

Two bugs are fixed here. First, `howFarUpInterval` now uses float division, so a value partway up an interval keeps its position. Second, the dB-within-interval fraction is `offset / dbThisInterval`, not `offset / dbLeftThisInterval`; the two coincide only when `howFarUpInterval` is 0, which is why the original looked correct while its divisor was silently always the full interval.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cmake --build build-tests --target UnitTests && ./build-tests/unit/UnitTests
```

Expected: `OK (125 tests, 125 ran, ...)` — 118 plus 7 new, 0 failures.

- [ ] **Step 6: Verify the firmware still builds**

```bash
./dbt build Debug
```

- [ ] **Step 7: Add a changelog entry**

Add to the changelog under the current unreleased section, matching surrounding style:

> Fixed `shiftVolumeByDB` discarding the intra-interval position of its input and mishandling negative offsets. This corrects the resonance compensation applied to songs saved before firmware 1.2.0 when they are loaded; such songs may now sound slightly different.

- [ ] **Step 8: Commit**

```bash
git add src/deluge/util/db_shift.h tests/unit/db_shift_tests.cpp tests/unit/CMakeLists.txt
git commit -m "Fix shiftVolumeByDB interval position and negative offsets"
```

---

## On-device verification

None of the unit tests exercise the audio path. After Task 2, flash and check:

1. Synth with volume reduced → preview matches post-load level.
2. Kit row with drum volume reduced → preview matches.
3. Kit output volume reduced → preview matches (the case the old code got backwards).
4. Kit output volume at minimum → preview is **silent**.
5. Audio clip and slicer → preview unchanged from before the branch.
6. Drum with its velocity→volume cable removed → preview matches.
