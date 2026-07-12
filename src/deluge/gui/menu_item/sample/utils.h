#pragma once

#include <cstdint>

class SampleControls;
class ModControllableAudio;
enum class InterpolationMode;

bool isSampleModeSample(ModControllableAudio* modControllable, int32_t whichThing);
SampleControls& getCurrentSampleControls(int32_t whichThing);

// M3 owner-4 by-value seam: the SampleControls descriptor fields are DOM-owned for an audio clip, so writes must route
// through AudioClip's setters (record + cache) rather than a mutable reference. For a sound source they still write the
// native cache directly (owner-5). getCurrentSampleControls() remains the read side (cache == record).
void setCurrentInterpolationMode(int32_t whichThing, InterpolationMode mode);
void setCurrentPitchAndSpeedIndependent(int32_t whichThing, bool independent);
