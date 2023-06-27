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

#ifndef WAVEFORMRENDERER_H_
#define WAVEFORMRENDERER_H_

#include "r_typedefs.h"
#include "definitions.h"

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
	                      uint8_t thisImage[][displayWidth + sideBarWidth][3], WaveformRenderData* data,
	                      SampleRecorder* recorder = NULL, uint8_t rgb[] = NULL, bool reversed = false,
	                      int xEnd = displayWidth);
	bool renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, uint8_t* thisImage,
	                       WaveformRenderData* data, SampleRecorder* recorder, uint8_t rgb[], bool reversed, int xStart,
	                       int xEnd);
	void renderOneCol(Sample* sample, int xDisplay, uint8_t thisImage[][displayWidth + sideBarWidth][3],
	                  WaveformRenderData* data, bool reversed = false, uint8_t rgb[] = NULL);
	void renderOneColForCollapseAnimation(int xDisplay, int xDisplayOutput, int32_t maxPeakFromZero, int progress,
	                                      uint8_t thisImage[][displayWidth + sideBarWidth][3], WaveformRenderData* data,
	                                      uint8_t rgb[], bool reversed, int32_t valueCentrePoint, int32_t valueSpan);
	void renderOneColForCollapseAnimationZoomedOut(int xDisplayWaveformLeftEdge, int xDisplayWaveformRightEdge,
	                                               int xDisplayOutput, int32_t maxPeakFromZero, int progress,
	                                               uint8_t thisImage[][displayWidth + sideBarWidth][3],
	                                               WaveformRenderData* data, uint8_t rgb[], bool reversed,
	                                               int32_t valueCentrePoint, int32_t valueSpan);
	bool findPeaksPerCol(Sample* sample, int64_t xScroll, uint64_t xZoom, WaveformRenderData* data,
	                     SampleRecorder* recorder = NULL, int xStart = 0, int xEnd = displayWidth);

	int8_t collapseAnimationToWhichRow;

private:
	int getColBrightnessForSingleRow(int xDisplay, int32_t maxPeakFromZero, WaveformRenderData* data);
	void getColBarPositions(int xDisplay, WaveformRenderData* data, int32_t* min24, int32_t* max24,
	                        int32_t valueCentrePoint, int32_t valueSpan);
	void drawColBar(int xDisplay, int32_t min24, int32_t max24, uint8_t thisImage[][displayWidth + sideBarWidth][3],
	                int brightness = 128, uint8_t rgb[] = NULL);
	void renderOneColForCollapseAnimationInterpolation(int xDisplayOutput, int32_t min24, int32_t max24,
	                                                   int singleSquareBrightness, int progress,
	                                                   uint8_t thisImage[][displayWidth + sideBarWidth][3],
	                                                   uint8_t rgb[]);
};

extern WaveformRenderer waveformRenderer;

#endif /* WAVEFORMRENDERER_H_ */
