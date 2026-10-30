#pragma once

#include "plugin-api/types/interpolation_mode.h"

#include <cstdint>

class SampleControls;
class ModControllableAudio;
using deluge::plugin::InterpolationMode;

bool isSampleModeSample(ModControllableAudio* modControllable, int32_t whichThing);
SampleControls& getCurrentSampleControls(int32_t whichThing);

/// @brief Set the current sample source's interpolation mode.
///
/// An audio clip's SampleControls fields are owned by the DOM record, so writes route through AudioClip's setters
/// (record and cache) rather than a mutable reference; a sound source writes its native cache directly.
/// getCurrentSampleControls() is the read side.
/// @param whichThing Which sample source the menu is editing.
/// @param mode       The new interpolation mode.
void setCurrentInterpolationMode(int32_t whichThing, InterpolationMode mode);
/// @brief Set whether the current sample source's pitch and speed are independent (time-stretch); routes like
///        setCurrentInterpolationMode().
/// @param whichThing Which sample source the menu is editing.
/// @param independent True to make pitch and speed independent.
void setCurrentPitchAndSpeedIndependent(int32_t whichThing, bool independent);
