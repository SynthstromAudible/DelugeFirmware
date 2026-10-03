#pragma once

// The scale theory lives in deluge_music_theory; this keeps the app's existing includes working.
#include "music-theory/preset_scales.h"

using deluge::music::ensureNotAllPresetScalesDisabled;
using deluge::music::flashStorageCodeToScale;
using deluge::music::getScale;
using deluge::music::getScaleName;
using deluge::music::isUserScale;
using deluge::music::presetScaleNotes;
using deluge::music::Scale;
using deluge::music::scalelikeNames;
using deluge::music::scaleToFlashStorageCode;
using enum deluge::music::Scale;
