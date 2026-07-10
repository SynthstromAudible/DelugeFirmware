# Sample-browser preview volume matching (issue #1591)

**Branch:** `1591-sample-preview-volume-not-linked-to-the-volume-of-the-clip-and-row-the-sample-will-be-loaded-into`
**Date:** 2026-07-10
**Status:** Approved, ready for implementation planning

## Problem

When you audition a sample in the sample browser, the preview plays through a
dedicated `SoundDrum` (`AudioEngine::sampleForPreview`) whose params are fixed at
`Sound::initParams` defaults. It renders straight to the master bus, so only the
song master volume is applied. The target synth's volume, the target kit-row
drum's volume, and the kit's own output volume are all ignored. The result: the
preview does not sound like the sample will once it is loaded.

A first attempt at a fix landed on this branch. A code review found eight
defects in it, three of them serious. This document specifies the replacement.

## Defects in the current branch implementation

Numbered as reported by the review, kept here so the plan can check each one off.

1. **Negative dB offset is undefined behavior.** The kit gain is converted to dB
   (`20 * log10f(kitLinear / neutralLinear)`) and applied via
   `ParamSet::shiftParamVolumeByDB`. For any kit quieter than neutral the offset
   is negative. `shiftVolumeByDB` (`util/functions.cpp:443`) has only ever been
   called with strictly positive offsets — both existing callers
   (`sound.cpp:3286`, `kit.cpp:1106`) guard with `compensationDB > 0.1`. On the
   negative path `newHowFarUpIntervalFloat` goes negative and line 468 converts
   a negative float to `uint32_t`, which is UB. On ARM the VCVT instruction
   saturates to 0, so the value snaps to its interval floor and the attenuation
   is silently discarded.
   *Observed:* a kit at −66 dB previews at full volume.

2. **A silenced kit previews at full volume.** `getFinalParameterValueVolume` of
   a minimum `UNPATCHED_VOLUME` returns exactly `0`. The `kitLinearVolume > 0`
   guard (added to dodge `log10f(0) == -inf`) then skips the shift entirely and
   leaves `targetExtraVolumeDB` at `0.f`. The guard fails open (loud) rather
   than closed (silent).

3. **`shiftVolumeByDB` quantizes every shift.** `int32_t howFarUpInterval` is
   divided by the `int` literal `268435456`, so `howFarUpIntervalFloat` is
   always exactly `0.0`. The intra-interval position of the old value is
   discarded. Pre-existing, but the branch promotes this helper from song-load
   compatibility fixups onto a live, user-facing audio path.

4. **The kit gain model is incomplete.** `GlobalEffectableForClip::renderOutput`
   (`global_effectable_for_clip.cpp:60-68`) computes
   `volumeAdjustment = getFinalParameterValueVolume(...) >> 1`, then
   `volumePostFX = volumeAdjustment + (volumeAdjustment >> 2)` (a ×1.25 kit
   baseline), then `setupFilterSetConfig` scales it again by the kit's
   resonance. The branch models only the ratio to neutral, so the baseline
   cancels out and resonance is ignored.

5. **Hardcoded defaults with a misleading comment.** The restore branch writes
   `getParamFromUserValue(GLOBAL_VOLUME_POST_FX, 40)` and `0`, attributing them
   to `setupAsSample()`. `setupAsSample()` (`sound.cpp:189`) sets neither param;
   they come from `Sound::initParams` (`sound.cpp:141`, `sound.cpp:144`).

6. **The velocity patch cable is not matched.** `setupAsSample` hardwires a
   `VELOCITY → LOCAL_VOLUME` cable at depth 50 on the preview drum. The branch
   copies the target's `LOCAL_VOLUME` param but not its cable depth, so the
   final per-voice volume still diverges for any drum with non-default velocity
   sensitivity.

7. **Recomputed constant and duplicated magic number.**
   `getFinalParameterValueVolume(134217728, cableToLinearParamShortcut(0))`
   always evaluates to `134217728`, recomputed on every encoder tick. The
   literal duplicates `global_effectable_for_clip.cpp:61` with no shared symbol.

8. **Fragile signature, split policy.** `previewSample` gained four positional
   defaulted parameters, two of them adjacent transposable `int32_t`s. Half the
   policy (kit dB math) lives in `sample_browser.cpp`, half (neutral defaults)
   in `audio_engine.cpp`.

## Key insight

For a volume param with no patch cables, the transform from param value to final
linear gain is a square law. Reading `Patcher::combineCablesLinear` and
`Patcher::cableToLinearParamWithoutRangeAdjustment` with
`paramRanges[GLOBAL_VOLUME_POST_FX] == 2^30` and
`paramNeutralValues[GLOBAL_VOLUME_POST_FX] == 2^27`:

```
pos(param) = (param >> 2) + 2^29          // affine in param, exact
final      = neutral * (pos / 2^29)^2     // square law
```

To multiply a param's final gain by the kit's gain `G = kitFinal / neutral`, scale
`pos` by `sqrt(G)`. And because the kit's gain is produced by the *same* square
law from the same neutral value:

```
G = (posKit / 2^29)^2    =>    sqrt(G) = posKit / 2^29    exactly
```

So the whole operation is one integer multiply:

```
posNew = (posTarget * posKit) >> 29
```

No `log10f`. No `shiftVolumeByDB`. No interval table. No floating point. And no
zero-guard: a kit at minimum volume gives `posKit == 0`, hence `posNew == 0`,
hence param minimum, hence true silence.

Defects 1, 2 and 7 disappear rather than being patched. Defect 3 leaves the
preview path entirely — the new code never calls `shiftVolumeByDB` — but the
bug remains in the helper itself for its two song-load callers, and is fixed
separately in commit 3.

Verified numerically against the real `getFinalParameterValueVolume` across the
full param range: exact wherever the result is representable.

## Design

### Component 1 — `applyGainToVolumeParam`

New pure function in `util/functions.{h,cpp}`:

```
int32_t applyGainToVolumeParam(int32_t volumeParam, int32_t gainPos);
```

`gainPos` is the gain expressed in the `pos` domain (`2^29` == unity gain).
Returns the new param value. The intermediate `posTarget * posKit >> 29` can
reach `2^31`, so it is computed in 64-bit and the resulting param saturated back
to `int32_t`. Being pure and total, it is unit-tested directly in
`tests/unit/function_tests.cpp`.

The kit's `gainPos` is `cableToLinearParamShortcut(kitVolumeParam) + 536870912`.

### Component 2 — `PreviewVolume`

```
struct PreviewVolume {
    int32_t volumePostFX;
    int32_t localVolume;
    int32_t velocityCableDepth;
    static PreviewVolume neutral();
};

void previewSample(String* path, FilePointer* filePointer, bool shouldActuallySound,
                   PreviewVolume const& volume);
```

Resolves defect 8: no transposable adjacent `int32_t`s, and `neutral()` gives the
restore-defaults branch exactly one home. `audio_engine.cpp` gains no dependency
on `soundEditor`.

### Component 3 — Gain application order

Apply the kit gain to `LOCAL_VOLUME` first, spilling any saturated remainder into
`GLOBAL_VOLUME_POST_FX`.

`LOCAL_VOLUME` sits at neutral (`0`) by default, giving it a full 4× of headroom —
which is exactly the kit's maximum gain — so this is exact for any drum whose
local volume is at or below neutral. Applying to `GLOBAL_VOLUME_POST_FX` first
would clip much earlier: a default drum already sits at 2.64× neutral, against a
4× param ceiling.

`GLOBAL_VOLUME_POST_FX` is a Sound-level param with no cables on the preview drum,
so it needs the existing `recalculatePatchingToParam` call. `LOCAL_VOLUME` is
per-voice and is picked up when the voice is patched during `noteOn`.

### Component 4 — Velocity cable

`PreviewVolume::velocityCableDepth` carries the target's `VELOCITY → LOCAL_VOLUME`
cable depth, or `0` if the target has no such cable. `neutral()` restores the
`setupAsSample` value of `getParamFromUserValue(PATCH_CABLE, 50)`. The preview
always notes on at velocity 64 (the `Sound::noteOn` default argument), so
matching the depth makes the final per-voice volume match the target. Resolves
defect 6.

### Component 5 — Shared default-volume constant

Introduce a named constant for the default Sound volume user value (`40`), used
by both `Sound::initParams` and `PreviewVolume::neutral()`, so the two cannot
drift. Resolves defect 5.

### Guard retained as-is

`kitParamManager.containsAnyParamCollectionsIncludingExpression()` before
`getUnpatchedParamSet()` is kept. It is exactly the predicate that
`getUnpatchedParamSet()` asserts on internally (`E410`,
`param_manager.h:97-103`), so it matches house idiom.

## Accepted limitations

Both are documented in comments at the call site so they do not read as
oversights.

- **Kit baseline and resonance are not modeled** (defect 4). The ×1.25 postFX
  baseline cancels out under ratio-to-neutral, and `setupFilterSetConfig`'s
  resonance scaling is not replicated. A resonant kit still previews slightly
  off. Modeling the full chain would couple the preview path to the kit render
  chain; rejected as too much coupling for the accuracy gained.
- **Combined gain saturates** at roughly +24 dB over the drum's own level, when
  both the drum's local volume and its postFX volume are already cranked. It
  fails quiet, which is the safe direction.

## Error handling

- Kit volume at minimum → `posKit == 0` → param minimum → silence. No guard, no
  special case.
- No target context (audio clip, slicer) → `PreviewVolume::neutral()`.
- Saturation is handled inside `applyGainToVolumeParam`, which is total: every
  `(int32_t, int32_t)` input pair returns a valid param value.

## Testing

- **Unit** (`tests/unit/function_tests.cpp`): `applyGainToVolumeParam` at unity
  gain (identity), zero gain (param minimum), maximum gain (saturates, does not
  overflow or wrap), and a round-trip against `getFinalParameterValueVolume`
  asserting the delivered gain ratio matches the requested one wherever
  representable.
- **Unit**, spill path: a target whose `LOCAL_VOLUME` is already above neutral,
  with the kit at maximum gain, must saturate `LOCAL_VOLUME` and carry the
  remaining gain into `GLOBAL_VOLUME_POST_FX` rather than dropping it.
- **Unit**, commit 3: `shiftVolumeByDB` with a negative offset, and with an old
  value partway up an interval, to pin the two bugs the fix addresses.
- **On-device**, since none of the above exercises the audio path:
  1. Synth with volume reduced → preview matches post-load level.
  2. Kit row with drum volume reduced → preview matches.
  3. Kit output volume reduced → preview matches (this is the case the old code
     got backwards).
  4. Kit output volume at minimum → preview is silent.
  5. Audio clip / slicer → preview unchanged from before the branch.
  6. Drum with velocity→volume cable removed → preview matches.

## Commits

Three, so the risky change is independently revertible.

1. `applyGainToVolumeParam` and its unit tests.
2. The #1591 fix: `PreviewVolume`, exact gain math, velocity cable matching,
   shared default-volume constant. Resolves defects 1, 2, 5, 6, 7, 8, and
   documents 4.
3. **Independent:** the `shiftVolumeByDB` integer-division and negative-offset
   fix (defect 3), with a changelog entry.

### Note on commit 3

After this design, commit 3 is **not required by the #1591 fix** — the new code
never calls `shiftVolumeByDB`. It is a standalone correctness fix to a shared
helper whose only remaining callers are the two song-load compatibility paths.
Fixing it changes the resonance compensation applied to every pre-1.2.0 song on
load, altering the audio of existing user songs. It is therefore kept in its own
commit with its own changelog entry, so it can be split off or reverted without
touching the #1591 work. This was raised with the user and the split was
approved.
