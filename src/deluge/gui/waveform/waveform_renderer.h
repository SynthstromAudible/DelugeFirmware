/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include <cstdint>

class Sample;
class MultisampleRange;
class SampleRecorder;
struct WaveformRenderData;

struct MarkerColumn {
	int32_t pos; // Unscrolled
	int32_t colOnScreen;
};

// This class provides low-level rendering functions for the waveform only

class WaveformRenderer {
public:
	WaveformRenderer();

	bool renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
	                      uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3], WaveformRenderData* data,
	                      SampleRecorder* recorder = NULL, uint8_t rgb[] = NULL, bool reversed = false,
	                      int32_t xEnd = kDisplayWidth);
	bool renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, uint8_t* thisImage,
	                       WaveformRenderData* data, SampleRecorder* recorder, uint8_t rgb[], bool reversed,
	                       int32_t xStart, int32_t xEnd);
	void renderOneCol(Sample* sample, int32_t xDisplay, uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3],
	                  WaveformRenderData* data, bool reversed = false, uint8_t rgb[] = NULL);
	void renderOneColForCollapseAnimation(int32_t xDisplay, int32_t xDisplayOutput, int32_t maxPeakFromZero,
	                                      int32_t progress, uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3],
	                                      WaveformRenderData* data, uint8_t rgb[], bool reversed,
	                                      int32_t valueCentrePoint, int32_t valueSpan);
	void renderOneColForCollapseAnimationZoomedOut(int32_t xDisplayWaveformLeftEdge, int32_t xDisplayWaveformRightEdge,
	                                               int32_t xDisplayOutput, int32_t maxPeakFromZero, int32_t progress,
	                                               uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3],
	                                               WaveformRenderData* data, uint8_t rgb[], bool reversed,
	                                               int32_t valueCentrePoint, int32_t valueSpan);
	bool findPeaksPerCol(Sample* sample, int64_t xScroll, uint64_t xZoom, WaveformRenderData* data,
	                     SampleRecorder* recorder = NULL, int32_t xStart = 0, int32_t xEnd = kDisplayWidth);

	int8_t collapseAnimationToWhichRow;

private:
	int32_t getColBrightnessForSingleRow(int32_t xDisplay, int32_t maxPeakFromZero, WaveformRenderData* data);
	void getColBarPositions(int32_t xDisplay, WaveformRenderData* data, int32_t* min24, int32_t* max24,
	                        int32_t valueCentrePoint, int32_t valueSpan);
	void drawColBar(int32_t xDisplay, int32_t min24, int32_t max24,
	                uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3], int32_t brightness = 128,
	                uint8_t rgb[] = NULL);
	void renderOneColForCollapseAnimationInterpolation(int32_t xDisplayOutput, int32_t min24, int32_t max24,
	                                                   int32_t singleSquareBrightness, int32_t progress,
	                                                   uint8_t thisImage[][kDisplayWidth + kSideBarWidth][3],
	                                                   uint8_t rgb[]);
};

extern WaveformRenderer waveformRenderer;
