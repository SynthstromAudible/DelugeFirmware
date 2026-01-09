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

// Render equalizer visualization with 16 frequency bands
void renderVisualizerBarSpectrum(oled_canvas::Canvas& canvas);

// Calculate frequency band range for equalizer bar
void calculateFrequencyBandRange(int32_t bar, float& lower_freq, float& upper_freq);

// Update peak tracking and draw peak indicator for equalizer bar
void updateAndDrawPeak(oled_canvas::Canvas& canvas, int32_t bar, float normalized_height, int32_t bar_left_x,
                       int32_t bar_right_x, int32_t k_graph_min_y, int32_t k_graph_max_y, int32_t k_graph_height,
                       uint32_t visualizer_mode);

} // namespace deluge::hid::display
