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

#include "gui/waveform/waveform_renderer.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "io/debug/log.h"
#include "model/sample/overview_cache_entry.h"
#include "model/sample/sample.h"
#include "model/sample/sample_reader_bridge.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_peak_math.h"
#include "model/sample/waveform_render_data.h"
#include "model/sample/waveform_scan.h"
#include "model/voice/voice_sample.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"
#include "sync/sd_access.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string.h>

WaveformRenderer waveformRenderer{};

WaveformRenderer::WaveformRenderer() {
}

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool WaveformRenderer::renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
                                        RGB thisImage[][kDisplayWidth + kSideBarWidth], WaveformRenderData* data,
                                        SampleRecorder* recorder, std::optional<RGB> rgb, bool reversed, int32_t xEnd) {

	bool completeSuccess = deluge::sample::find_peaks_per_col(sample, xScroll, xZoom, data, recorder);
	if (!completeSuccess) {
		return false;
	}

	// Clear display
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		memset(thisImage[y], 0, kDisplayWidth * 3);
	}

	for (int32_t xDisplay = 0; xDisplay < xEnd; xDisplay++) {
		renderOneCol(sample, xDisplay, thisImage, data, reversed, rgb);
	}

	return true;
}

// Returns false if had trouble loading some (will often not be all) Clusters
bool WaveformRenderer::renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, RGB* thisImage,
                                         WaveformRenderData* data, SampleRecorder* recorder, RGB rgb, bool reversed,
                                         int32_t xStart, int32_t xEnd) {

	int32_t xStartSource = xStart;
	int32_t xEndSource = xEnd;
	if (reversed) {
		xStartSource = kDisplayWidth - 1 - xEnd;
		xEndSource = kDisplayWidth - 1 - xStart;
	}

	bool completeSuccess =
	    deluge::sample::find_peaks_per_col(sample, xScroll, xZoom, data, recorder, xStartSource, xEndSource);
	if (!completeSuccess) {
		return false;
	}

	int32_t maxPeakFromZero = sample->getMaxPeakFromZero();

	for (int32_t xDisplayOutput = xStart; xDisplayOutput < xEnd; xDisplayOutput++) {

		int32_t xDisplaySource = xDisplayOutput;
		if (reversed) {
			xDisplaySource = kDisplayWidth - 1 - xDisplaySource;
		}

		// If no data here (e.g. if Sample not recorded this far yet...)
		if (data->colStatus[xDisplaySource] != COL_STATUS_INVESTIGATED) {
			thisImage[xDisplayOutput] = deluge::gui::colours::black;
			continue;
		}

		int32_t colourValue = deluge::sample::col_brightness_for_single_row(xDisplaySource, maxPeakFromZero, data);
		colourValue = (colourValue * colourValue); // >> 8;
		// if (colourValue > 255) colourValue = 255; // May sometimes go juuust over

		thisImage[xDisplayOutput] = rgb.transform([colourValue](auto channel) {
			int32_t valueHere = (colourValue * channel) >> 16;
			// Limit the heck out of the bit depth, to avoid problem with PIC firmware where too many different colour
			// shades cause big problems. The 6 is quite arbitrary, but I think it looks good
			return std::clamp<int32_t>((valueHere + 6) & ~15, 0, RGB::channel_max);
		});
	}

	return true;
}

void WaveformRenderer::renderOneColForCollapseAnimation(int32_t xDisplayWaveform, int32_t xDisplayOutput,
                                                        int32_t maxPeakFromZero, int32_t progress,
                                                        RGB thisImage[][kDisplayWidth + kSideBarWidth],
                                                        WaveformRenderData* data, std::optional<RGB> rgb, bool reversed,
                                                        int32_t valueCentrePoint, int32_t valueSpan) {

	int32_t xDisplayData = xDisplayWaveform;
	if (reversed) {
		xDisplayData = kDisplayWidth - 1 - xDisplayData;
	}

	if (data->colStatus[xDisplayData] != COL_STATUS_INVESTIGATED) {
		return;
	}

	int32_t min24, max24;
	deluge::sample::col_bar_positions(xDisplayData, data, &min24, &max24, valueCentrePoint, valueSpan);

	int32_t singleSquareBrightness = deluge::sample::col_brightness_for_single_row(xDisplayData, maxPeakFromZero, data);

	renderOneColForCollapseAnimationInterpolation(xDisplayOutput, min24, max24, singleSquareBrightness, progress,
	                                              thisImage, rgb);
}

// For the explode animation. Crams multiple cols of source material into one col of output material.
// Crudely grabs the max values from all cols in range, which looks totally fine.
void WaveformRenderer::renderOneColForCollapseAnimationZoomedOut(
    int32_t xDisplayWaveformLeftEdge, int32_t xDisplayWaveformRightEdge, int32_t xDisplayOutput,
    int32_t maxPeakFromZero, int32_t progress, RGB thisImage[][kDisplayWidth + kSideBarWidth], WaveformRenderData* data,
    std::optional<RGB> rgb, bool reversed, int32_t valueCentrePoint, int32_t valueSpan) {

	int32_t xDisplayDataLeftEdge = xDisplayWaveformLeftEdge;
	int32_t xDisplayDataRightEdge = xDisplayWaveformRightEdge;
	if (reversed) {
		xDisplayDataLeftEdge = kDisplayWidth - 1 - xDisplayWaveformRightEdge;
		xDisplayDataRightEdge = kDisplayWidth - 1 - xDisplayWaveformLeftEdge;
	}

	int32_t min24Total = 2147483647;
	int32_t max24Total = -2147483648;

	int32_t singleSquareBrightnessTotal = 0;

	for (int32_t xDisplayDataNow = xDisplayDataLeftEdge; xDisplayDataNow <= xDisplayDataRightEdge; xDisplayDataNow++) {
		if (data->colStatus[xDisplayDataNow] != COL_STATUS_INVESTIGATED) {
			return;
		}

		int32_t min24, max24;
		deluge::sample::col_bar_positions(xDisplayDataNow, data, &min24, &max24, valueCentrePoint, valueSpan);

		if (min24 < min24Total) {
			min24Total = min24;
		}
		if (max24 > max24Total) {
			max24Total = max24;
		}

		int32_t singleSquareBrightness =
		    deluge::sample::col_brightness_for_single_row(xDisplayDataNow, maxPeakFromZero, data);

		if (singleSquareBrightness > singleSquareBrightnessTotal) {
			singleSquareBrightnessTotal = singleSquareBrightness;
		}
	}

	renderOneColForCollapseAnimationInterpolation(xDisplayOutput, min24Total, max24Total, singleSquareBrightnessTotal,
	                                              progress, thisImage, rgb);
}

// Once we've derived the appropriate data from the waveform for one final col of pads,
// this does the vertical animation according to our current amount of expandedness
void WaveformRenderer::renderOneColForCollapseAnimationInterpolation(int32_t xDisplayOutput, int32_t min24,
                                                                     int32_t max24, int32_t singleSquareBrightness,
                                                                     int32_t progress,
                                                                     RGB thisImage[][kDisplayWidth + kSideBarWidth],
                                                                     std::optional<RGB> rgb) {

	int32_t minStart = ((int32_t)collapseAnimationToWhichRow - (kDisplayHeight >> 1)) << 24;
	int32_t maxStart = ((int32_t)collapseAnimationToWhichRow - (kDisplayHeight >> 1) + 1) << 24;

	int32_t minDistance = min24 - minStart;
	int32_t maxDistance = max24 - maxStart;
	int32_t brightnessDistance = 256 - singleSquareBrightness;

	int32_t minCurrent = minStart + (((int64_t)minDistance * progress) >> 16);
	int32_t maxCurrent = maxStart + (((int64_t)maxDistance * progress) >> 16);
	int32_t brightnessCurrent = singleSquareBrightness + ((brightnessDistance * progress) >> 16);

	drawColBar(xDisplayOutput, minCurrent, maxCurrent, thisImage, brightnessCurrent, rgb);
}

void WaveformRenderer::drawColBar(int32_t xDisplay, int32_t min24, int32_t max24,
                                  RGB thisImage[][kDisplayWidth + kSideBarWidth], int32_t brightness,
                                  std::optional<RGB> rgb) {
	int32_t yStart = std::max((int32_t)(min24 >> 24), -(kDisplayHeight >> 1));
	int32_t yStop = std::min((int32_t)(max24 >> 24) + 1, kDisplayHeight >> 1);

	for (int32_t y = yStart; y < yStop; y++) {

		int32_t colourAmount; // Out of 256

		if (y == (min24 >> 24)) {
			int32_t howMuchThisSquare = (min24 - (y << 24)) >> 16; // Comes out as 8-bit
			colourAmount = brightness - ((howMuchThisSquare * brightness) >> 8);
		}

		else if (y < (max24 >> 24)) {
			colourAmount = brightness;
		}

		else {
			int32_t howMuchThisSquare = (max24 - (y << 24)) >> 16; // Comes out as 8-bit
			colourAmount = ((howMuchThisSquare * brightness) >> 8);
		}

		int32_t valueHere = (colourAmount * colourAmount) >> 8;
		RGB color = rgb.has_value()
		                ? rgb.value().transform([valueHere](auto channel) { return (valueHere * channel) >> 8; })
		                : RGB::monochrome(valueHere);

		thisImage[y + (kDisplayHeight >> 1)][xDisplay] = color;
	}
}

void WaveformRenderer::renderOneCol(Sample* sample, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth],
                                    WaveformRenderData* data, bool reversed, std::optional<RGB> rgb) {
	int32_t min24, max24;
	int32_t brightness = rgb ? 256 : 128;

	int32_t xDisplaySource = reversed ? (kDisplayWidth - 1 - xDisplay) : xDisplay;

	if (data->colStatus[xDisplaySource] == COL_STATUS_INVESTIGATED) {

		deluge::sample::col_bar_positions(xDisplaySource, data, &min24, &max24, sample->getFoundValueCentrePoint(),
		                                  sample->getValueSpan());

		drawColBar(xDisplay, min24, max24, thisImage, brightness, rgb);
	}
}
