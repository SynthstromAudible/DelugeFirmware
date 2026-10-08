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
#include "gui/waveform/waveform_peak_math.h"
#include "gui/waveform/waveform_render_data.h"
#include "io/debug/log.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/voice/voice_sample.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"
#include <algorithm>
#include <iterator>
#include <limits>
#include <optional>
#include <string.h>

extern "C" {
extern uint8_t currentlyAccessingCard;
}

using namespace deluge::gui::waveform;

WaveformRenderer waveformRenderer{};

WaveformRenderer::WaveformRenderer() {
}

namespace {

/// @brief Slides cached per-column peaks across after the view scrolled by a whole number of columns.
///
/// Afterwards, column `i` holds what column `i + shift` held. The columns scrolled into view are marked
/// un-investigated so they get worked out.
///
/// @param data The cache to slide.
/// @param shift Columns scrolled, positive meaning rightwards. A whole screen or more clears everything.
void shiftColumns(WaveformRenderData& data, int32_t shift) {
	if (shift == 0) {
		return;
	}
	if (std::abs(shift) >= kDisplayWidth) {
		std::ranges::fill(data.colStatus, ColumnStatus::NOT_INVESTIGATED);
		return;
	}

	auto slide = [shift](auto& array) {
		if (shift > 0) {
			std::shift_left(std::begin(array), std::end(array), shift);
		}
		else {
			std::shift_right(std::begin(array), std::end(array), -shift);
		}
	};
	slide(data.colStatus);
	slide(data.minPerCol);
	slide(data.maxPerCol);

	// Mark the freshly-exposed columns (left behind by the shift) as needing investigation.
	if (shift > 0) {
		std::fill_n(std::end(data.colStatus) - shift, shift, ColumnStatus::NOT_INVESTIGATED);
	}
	else {
		std::fill_n(std::begin(data.colStatus), -shift, ColumnStatus::NOT_INVESTIGATED);
	}
}

} // namespace

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool WaveformRenderer::renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
                                        RGB thisImage[][kDisplayWidth + kSideBarWidth], WaveformRenderData* data,
                                        SampleRecorder* recorder, std::optional<RGB> rgb, bool reversed, int32_t xEnd) {

	bool completeSuccess = findPeaksPerCol(sample, xScroll, xZoom, data, recorder);
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

	// Don't block on the card here: song row view renders many of these at once, so loading every missing Cluster
	// synchronously made scrolling lag badly. Columns still waiting on a Cluster show their placeholder estimate
	// from before the zoom or scroll if they have one, or black otherwise, until it arrives and our false return
	// gets the row re-rendered. Not while recording, though: see findPeaksPerCol().
	bool wait_for_card = (recorder != nullptr);
	bool completeSuccess =
	    findPeaksPerCol(sample, xScroll, xZoom, data, recorder, xStartSource, xEndSource, wait_for_card);

	int32_t maxPeakFromZero = sample->getMaxPeakFromZero();

	for (int32_t xDisplayOutput = xStart; xDisplayOutput < xEnd; xDisplayOutput++) {

		int32_t xDisplaySource = xDisplayOutput;
		if (reversed) {
			xDisplaySource = kDisplayWidth - 1 - xDisplaySource;
		}

		// If no data here (e.g. if Sample not recorded this far yet...). Placeholders are drawn while their column
		// waits on the card, rather than leaving it black.
		ColumnStatus status = data->colStatus[xDisplaySource];
		if (status != ColumnStatus::INVESTIGATED && status != ColumnStatus::PLACEHOLDER) {
			thisImage[xDisplayOutput] = deluge::gui::colours::black;
			continue;
		}

		int32_t colourValue = getColBrightnessForSingleRow(xDisplaySource, maxPeakFromZero, data);
		colourValue = (colourValue * colourValue); // >> 8;
		// if (colourValue > 255) colourValue = 255; // May sometimes go juuust over

		thisImage[xDisplayOutput] = rgb.transform([colourValue](auto channel) {
			int32_t valueHere = (colourValue * channel) >> 16;
			// Limit the heck out of the bit depth, to avoid problem with PIC firmware where too many different colour
			// shades cause big problems. The 6 is quite arbitrary, but I think it looks good
			return std::clamp<int32_t>((valueHere + 6) & ~15, 0, RGB::channel_max);
		});
	}

	return completeSuccess;
}

// Value out of 255
int32_t WaveformRenderer::getColBrightnessForSingleRow(int32_t xDisplay, int32_t maxPeakFromZero,
                                                       WaveformRenderData* data) {

	int32_t peak1 = std::abs(data->minPerCol[xDisplay]);
	int32_t peak2 = std::abs(data->maxPerCol[xDisplay]);

	int32_t peakHere = std::max(peak1, peak2);

	if (false && peakHere >= maxPeakFromZero) {
		D_PRINTLN("peak:  %d  but max:  %d", peakHere, maxPeakFromZero);
	}

	uint32_t peak16 = ((int64_t)peakHere << 16) / maxPeakFromZero;

	return std::min<int32_t>(peak16 >> 8, 256); // Max 256 - for now. Looks great and bright.
	                                            // Must manually limit this, cos if we've ended up with values higher
	                                            // than our maxPeakFromZero, there'd be trouble otherwise
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

	if (data->colStatus[xDisplayData] != ColumnStatus::INVESTIGATED) {
		return;
	}

	int32_t min24, max24;
	getColBarPositions(xDisplayData, data, &min24, &max24, valueCentrePoint, valueSpan);

	int32_t singleSquareBrightness = getColBrightnessForSingleRow(xDisplayData, maxPeakFromZero, data);

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
		if (data->colStatus[xDisplayDataNow] != ColumnStatus::INVESTIGATED) {
			return;
		}

		int32_t min24, max24;
		getColBarPositions(xDisplayDataNow, data, &min24, &max24, valueCentrePoint, valueSpan);

		if (min24 < min24Total) {
			min24Total = min24;
		}
		if (max24 > max24Total) {
			max24Total = max24;
		}

		int32_t singleSquareBrightness = getColBrightnessForSingleRow(xDisplayDataNow, maxPeakFromZero, data);

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

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool WaveformRenderer::findPeaksPerCol(Sample* sample, int64_t xScrollSamples, uint64_t xZoomSamples,
                                       WaveformRenderData* data, SampleRecorder* recorder, int32_t xStart, int32_t xEnd,
                                       bool wait_for_card) {

	releaseFinishedLoads();
	int32_t load_instruction = wait_for_card ? CLUSTER_LOAD_IMMEDIATELY : CLUSTER_ENQUEUE;

	uint64_t numValidSamples;
	int32_t endClusters;
	if (recorder) {
		numValidSamples = recorder->numSamplesCaptured;
		endClusters = sample->clusters.getNumElements();
	}
	else {
		numValidSamples = sample->lengthInSamples;
		endClusters = sample->getFirstClusterIndexWithNoAudioData();
	}

	uint64_t numValidBytes = numValidSamples * sample->byteDepth * sample->numChannels;

	bool length_changed = static_cast<int64_t>(numValidSamples) != data->num_samples;
	if (recorder != nullptr) {
		// During recording the waveform grows every frame. A monotonic append leaves already-investigated
		// columns valid (the growing edge is re-checked below because only ColumnStatus::INVESTIGATED columns are
		// skipped), so don't nuke the whole cache each frame just because the length ticked up.
		length_changed = false;
	}
	// Callers set xScroll to -1 to force a re-render (e.g. sample replaced), which must never look like a scroll
	if (length_changed || data->xScroll == -1) {
		// Off the record path the waveform length changed (e.g. sample replaced/shrunk), or a re-render was forced:
		// nothing is reusable.
		std::ranges::fill(data->colStatus, ColumnStatus::NOT_INVESTIGATED);
	}
	else if (xZoomSamples != data->xZoom || xScrollSamples != data->xScroll) {
		// If we only scrolled, by a whole number of columns, the overlapping columns' peaks are still valid - slide
		// them across so only the newly-exposed columns get re-investigated. Otherwise carry over whatever of the
		// old peaks still fits.
		std::optional<int32_t> shift;
		if (xZoomSamples == data->xZoom) {
			shift = columnShift(xScrollSamples - data->xScroll, static_cast<int64_t>(xZoomSamples), kDisplayWidth);
		}
		if (shift.has_value()) {
			shiftColumns(*data, *shift);
		}
		else {
			remapColumns(*data, xScrollSamples, static_cast<int64_t>(xZoomSamples));
		}
	}

	data->xScroll = xScrollSamples;
	data->xZoom = xZoomSamples;
	data->num_samples = static_cast<int64_t>(numValidSamples);

	bool hadAnyTroubleLoading = false;

	bool was_placeholder = false;

	// Leaves a column for a later render to try again. A placeholder keeps its estimate to draw in the meantime.
	auto retry_later = [&](int32_t col) {
		data->colStatus[col] = was_placeholder ? ColumnStatus::PLACEHOLDER : ColumnStatus::NOT_INVESTIGATED;
		hadAnyTroubleLoading = true;
	};

	for (int32_t col = xStart; col < xEnd; col++) {

		if (data->colStatus[col] == ColumnStatus::INVESTIGATED) {
			continue;
		}
		was_placeholder = (data->colStatus[col] == ColumnStatus::PLACEHOLDER);

		data->colStatus[col] = ColumnStatus::INVESTIGATED; // Default, which we may override below

		int32_t colStartSample = xScrollSamples + col * xZoomSamples;
		if (colStartSample >= numValidSamples) {
			data->colStatus[col] = ColumnStatus::BEYOND_WAVEFORM;
			continue;
		}
		else if (colStartSample < 0) {
			colStartSample = 0;
		}

		int32_t colEndSample = xScrollSamples + (col + 1) * xZoomSamples;

		// If this column extends further right than the end of the waveform...
		if (colEndSample >= numValidSamples) {

			// If we're still recording, we'll just want to come back and render this one when the waveform has grown to
			// cover this whole column
			if (recorder) {
				data->colStatus[col] = ColumnStatus::NOT_INVESTIGATED;
				continue;
			}
			colEndSample = numValidSamples;
		}
		else if (colEndSample < 0) {
			data->colStatus[col] = ColumnStatus::BEYOND_WAVEFORM;
			continue;
		}

		// 64-bit so samples past 2 GB (colStartSample * frameSize) don't overflow.
		int64_t colStartByte = static_cast<int64_t>(colStartSample) * sample->numChannels * sample->byteDepth
		                       + sample->audioDataStartPosBytes;
		int64_t colEndByte = static_cast<int64_t>(colEndSample) * sample->numChannels * sample->byteDepth
		                     + sample->audioDataStartPosBytes;

		int32_t frameSize = sample->numChannels * sample->byteDepth;
		int64_t audioEndByte = sample->audioDataStartPosBytes + static_cast<int64_t>(numValidBytes);
		int32_t firstCluster = static_cast<int32_t>(colStartByte >> Cluster::size_magnitude);
		int32_t lastCluster = static_cast<int32_t>((colEndByte - 1) >> Cluster::size_magnitude);
		lastCluster = std::min(lastCluster, endClusters - 1);
		if (firstCluster > lastCluster) {
			data->colStatus[col] = ColumnStatus::BEYOND_WAVEFORM;
			continue;
		}

		int32_t minThisCol = std::numeric_limits<int32_t>::max();
		int32_t maxThisCol = std::numeric_limits<int32_t>::min();
		bool hadSamples = false;
		bool loadFailed = false;

		for (int32_t clusterIndex = firstCluster; clusterIndex <= lastCluster; clusterIndex++) {
			int64_t thisClusterStart = clusterStart(clusterIndex, Cluster::size_magnitude);
			int64_t audioStart = thisClusterStart +
			                     firstFrameOffset(thisClusterStart, sample->audioDataStartPosBytes, frameSize);
			int64_t audioEnd = std::min(thisClusterStart + Cluster::size, audioEndByte);
			audioEnd -= std::max<int64_t>(audioEnd - audioStart, 0) % frameSize;

			int64_t readStart = std::max<int64_t>(colStartByte, audioStart);
			int64_t readEnd = std::min<int64_t>(colEndByte, audioEnd);
			readEnd -= std::max<int64_t>(readEnd - readStart, 0) % frameSize;
			if (readEnd <= readStart) {
				continue;
			}

			bool stillRecording = recorder != nullptr && thisClusterStart + Cluster::size > audioEndByte;
			bool wholeCluster = readStart == audioStart && readEnd == audioEnd && !stillRecording;
			SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndex);
			if (sampleCluster->cluster != nullptr && sampleCluster->cluster->numReasonsToBeLoaded < 0) {
				FREEZE_WITH_ERROR("E449"); // Trying to catch errer before i028, which users have gotten.
			}

			Peak peak;
			if (wholeCluster && sampleCluster->investigatedWholeLength) {
				peak = Peak{static_cast<int32_t>(sampleCluster->minValue) << 24,
				            static_cast<int32_t>(sampleCluster->maxValue) << 24};
			}
			else {
				char const* errorCode = "E341";
				if (sampleCluster->cluster != nullptr) {
					errorCode = sampleCluster->cluster->loaded ? "E343" : "E344";
				}

				Cluster* cluster = sampleCluster->getCluster(sample, clusterIndex, load_instruction);
				if (cluster == nullptr) {
					D_PRINTLN("cant read");
					retry_later(col);
					loadFailed = true;
					break;
				}
				if (!cluster->loaded) {
					holdUntilLoaded(sample, cluster);
					retry_later(col);
					loadFailed = true;
					break;
				}
				if (cluster->numReasonsToBeLoaded <= 0) {
					FREEZE_WITH_ERROR(errorCode);
				}

				peak = scanPeak(cluster->data, static_cast<int32_t>(readStart - thisClusterStart),
				                static_cast<int32_t>(readEnd - thisClusterStart), sample->byteDepth);
				if (wholeCluster) {
					int32_t prevMin = static_cast<int32_t>(sampleCluster->minValue) << 24;
					int32_t prevMax = static_cast<int32_t>(sampleCluster->maxValue) << 24;
					peak.min = std::min(peak.min, prevMin);
					peak.max = std::max(peak.max, prevMax);
					sampleCluster->minValue = coarsePeak(peak.min);
					sampleCluster->maxValue = coarsePeak(peak.max);
					sampleCluster->investigatedWholeLength = true;
				}
				else {
					sampleCluster->minValue = std::min(sampleCluster->minValue, coarsePeak(peak.min));
					sampleCluster->maxValue = std::max(sampleCluster->maxValue, coarsePeak(peak.max));
				}

				audioFileManager.removeReasonFromCluster(*cluster, "E340"); // Ron R got this, when error was "iiuh"
				AudioEngine::routineWithClusterLoading();
			}

			if (peak.min <= peak.max) {
				minThisCol = std::min(minThisCol, peak.min);
				maxThisCol = std::max(maxThisCol, peak.max);
				hadSamples = true;
			}
		}

		if (loadFailed) {
			continue;
		}
		if (!hadSamples) {
			data->colStatus[col] = ColumnStatus::BEYOND_WAVEFORM;
			continue;
		}
		data->maxPerCol[col] = maxThisCol;
		data->minPerCol[col] = minThisCol;
	}

	if (recorder != nullptr) {
		sample->maxValueFound = recorder->recordMax;
		sample->minValueFound = recorder->recordMin;
	}

	// Keep a running best for the max and min found for the whole Sample
	else {
		for (int32_t col = xStart; col < xEnd; col++) {
			if (data->colStatus[col] == ColumnStatus::INVESTIGATED) {
				if (data->maxPerCol[col] > sample->maxValueFound) {
					sample->maxValueFound = data->maxPerCol[col];
				}
				if (data->minPerCol[col] < sample->minValueFound) {
					sample->minValueFound = data->minPerCol[col];
				}
			}
		}
	}

	return !hadAnyTroubleLoading;
}

void WaveformRenderer::holdUntilLoaded(Sample* sample, Cluster* cluster) {
	auto is_this_cluster = [cluster](const PendingLoad& load) { return load.cluster == cluster; };

	// One reason is enough. And if we're full, the column gets retried once earlier loads have finished.
	if (std::ranges::any_of(pending_loads_, is_this_cluster) || pending_loads_.full()) {
		audioFileManager.removeReasonFromCluster(*cluster, "4460");
		return;
	}

	sample->addReason();
	pending_loads_.push_back({cluster, sample});
}

void WaveformRenderer::release(const PendingLoad& load) {
	audioFileManager.removeReasonFromCluster(*load.cluster, "4460");
	load.sample->removeReason("4460");
}

void WaveformRenderer::releaseFinishedLoads() {
	auto finished = [](const PendingLoad& load) {
		if (load.cluster == audioFileManager.clusterBeingLoaded) {
			return false;
		}
		// Unloadable ones have been taken out of the loading queue, so would otherwise be held forever
		return load.cluster->loaded || load.cluster->unloadable || load.sample->unloadable;
	};

	for (auto it = pending_loads_.begin(); it != pending_loads_.end();) {
		if (finished(*it)) {
			release(*it);
			it = pending_loads_.erase(it);
		}
		else {
			++it;
		}
	}
}

void WaveformRenderer::releaseAllLoads() {
	for (const PendingLoad& load : pending_loads_) {
		release(load);
	}
	pending_loads_.clear();
}

void WaveformRenderer::getColBarPositions(int32_t xDisplay, WaveformRenderData* data, int32_t* min24, int32_t* max24,
                                          int32_t valueCentrePoint, int32_t valueSpan) {
	*min24 = ((int64_t)(data->minPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;
	*max24 = ((int64_t)(data->maxPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;

	// Ensure we're going to draw at least 1 pixel's width
	if (*max24 - *min24 < kMaxSampleValue) {
		int32_t midPoint = (*max24 >> 1) + (*min24 >> 1);
		*max24 = midPoint + 8388608;
		*min24 = midPoint - 8388608;
	}
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

	if (data->colStatus[xDisplaySource] == ColumnStatus::INVESTIGATED) {

		getColBarPositions(xDisplaySource, data, &min24, &max24, sample->getFoundValueCentrePoint(),
		                   sample->getValueSpan());

		drawColBar(xDisplay, min24, max24, thisImage, brightness, rgb);
	}
}
