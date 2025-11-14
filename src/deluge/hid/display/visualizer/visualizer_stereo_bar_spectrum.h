/*
 * Copyright (c) 2025 Bruce Zawadzki (Tone Coder)
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "visualizer_common.h"

namespace deluge::hid::display {

// Render stereo 8-band equalizer visualization
void renderVisualizerStereoBarSpectrum(oled_canvas::Canvas& canvas);

// Calculate frequency band range for stereo equalizer bar
void calculateStereoEQFrequencyBandRange(int32_t band, float& lower_freq, float& upper_freq);

// Update peak tracking and draw peak indicator for stereo equalizer bar
void updateAndDrawStereoEQPeak(oled_canvas::Canvas& canvas, int32_t band, float normalizedHeight, int32_t centerX,
                               int32_t maxBarHalfWidth, int32_t bandTopY, int32_t bandBottomY, int32_t kGraphMinX,
                               int32_t kGraphMaxX, int32_t kGraphMinY, int32_t kGraphMaxY, int32_t kGraphHeight,
                               uint32_t visualizer_mode);

} // namespace deluge::hid::display
