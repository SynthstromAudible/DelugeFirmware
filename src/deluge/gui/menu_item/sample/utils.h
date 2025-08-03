#pragma once

#include <cstdint>

class SampleControls;
class ModControllableAudio;

bool isSampleModeSample(ModControllableAudio* modControllable, int32_t whichThing);
SampleControls& getCurrentSampleControls(int32_t whichThing);
