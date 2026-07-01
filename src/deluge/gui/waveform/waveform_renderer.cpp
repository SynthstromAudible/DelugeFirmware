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

WaveformRenderer waveformRenderer{};

WaveformRenderer::WaveformRenderer() {
}

namespace {

// Slides the cached per-column peak data when the waveform scrolls horizontally by a whole number of
// columns (fix A, issue #4460). After the shift, new column `i` holds what old column `i + shift` held,
// so only the columns scrolled freshly into view need re-investigating (marked un-investigated here).
void shiftRenderCache(WaveformRenderData& data, int32_t shift) {
	if (shift == 0) {
		return;
	}
	if (std::abs(shift) >= kDisplayWidth) {
		std::ranges::fill(data.colStatus, COL_STATUS_NOT_INVESTIGATED);
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
		std::fill_n(std::end(data.colStatus) - shift, shift, uint8_t{COL_STATUS_NOT_INVESTIGATED});
	}
	else {
		std::fill_n(std::begin(data.colStatus), -shift, uint8_t{COL_STATUS_NOT_INVESTIGATED});
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

	bool completeSuccess = findPeaksPerCol(sample, xScroll, xZoom, data, recorder, xStartSource, xEndSource);
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

	return true;
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

	if (data->colStatus[xDisplayData] != COL_STATUS_INVESTIGATED) {
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
		if (data->colStatus[xDisplayDataNow] != COL_STATUS_INVESTIGATED) {
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
                                       WaveformRenderData* data, SampleRecorder* recorder, int32_t xStart,
                                       int32_t xEnd) {

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

	if (xZoomSamples != data->xZoom || static_cast<int64_t>(numValidSamples) != data->validLengthSamples) {
		// Zoom changed, or the waveform length changed (e.g. sample replaced/shrunk): nothing is reusable.
		std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
	}
	else if (xScrollSamples != data->xScroll) {
		// Scroll changed but zoom and length didn't. If we moved by a whole number of columns that fits the
		// display width, the overlapping columns' peaks are still valid - slide them across so only the
		// newly-exposed columns get re-investigated (fix A, issue #4460). Otherwise fall back to a full
		// invalidate (this also covers a scroll delta so large the column count wouldn't fit in int32).
		int64_t deltaSamples = xScrollSamples - data->xScroll;
		int64_t shiftCols = (deltaSamples % static_cast<int64_t>(xZoomSamples) == 0)
		                        ? deltaSamples / static_cast<int64_t>(xZoomSamples)
		                        : 0;
		if (shiftCols != 0 && shiftCols >= -kDisplayWidth && shiftCols <= kDisplayWidth) {
			shiftRenderCache(*data, static_cast<int32_t>(shiftCols));
		}
		else {
			std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
		}
	}

	data->xScroll = xScrollSamples;
	data->xZoom = xZoomSamples;
	data->validLengthSamples = static_cast<int64_t>(numValidSamples);

	bool hadAnyTroubleLoading = false;

	for (int32_t col = xStart; col < xEnd; col++) {

		if (data->colStatus[col] == COL_STATUS_INVESTIGATED) {
			continue;
		}

		data->colStatus[col] = COL_STATUS_INVESTIGATED; // Default, which we may override below

		int32_t colStartSample = xScrollSamples + col * xZoomSamples;
		if (colStartSample >= numValidSamples) {
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
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
				data->colStatus[col] = 0;
				continue;
			}
			colEndSample = numValidSamples;
		}
		else if (colEndSample < 0) {
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}

		// 64-bit so samples past 2 GB (colStartSample * frameSize) don't overflow (#4460).
		int64_t colStartByte = static_cast<int64_t>(colStartSample) * sample->numChannels * sample->byteDepth
		                       + sample->audioDataStartPosBytes;
		int64_t colEndByte = static_cast<int64_t>(colEndSample) * sample->numChannels * sample->byteDepth
		                     + sample->audioDataStartPosBytes;

		int32_t colStartCluster = static_cast<int32_t>(colStartByte >> Cluster::size_magnitude);
		int32_t colEndCluster = static_cast<int32_t>(colEndByte >> Cluster::size_magnitude);

		int32_t clusterIndexToDo;
		int32_t startByteWithinCluster;
		int32_t endByteWithinCluster;

		int32_t numClustersSpan = colEndCluster - colStartCluster;

		bool investigatingAWholeCluster = false;

		// If both same cluster...
		if (numClustersSpan == 0) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			endByteWithinCluster = static_cast<int32_t>(colEndByte & (Cluster::size - 1));
		}

		// Special case to make sure we get initial transient (we know there's more than 1 cluster)
		else if (colStartSample == 0 && colStartByte < (Cluster::size >> 1)) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 3 or more clusters, take 2nd one. TODO: have it take any one which has previously been fully investigated?
		else if (numClustersSpan >= 2) {
			clusterIndexToDo = colStartCluster + 1;

			int32_t startByteWithinFirstCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));

			int32_t unusedBytesAtEndOfPrevCluster =
			    (Cluster::size - startByteWithinFirstCluster) % (sample->numChannels * sample->byteDepth);
			if (unusedBytesAtEndOfPrevCluster == 0) {
				startByteWithinCluster = 0;
			}
			else {
				startByteWithinCluster = (sample->numChannels * sample->byteDepth) - unusedBytesAtEndOfPrevCluster;
			}

			endByteWithinCluster = Cluster::size;
			investigatingAWholeCluster = true;
		}

		// If 2 cluster..
		else if (numClustersSpan == 1) {

			int32_t startByteWithinFirstCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			int32_t bytesInFirstCluster = Cluster::size - startByteWithinFirstCluster;

			int32_t bytesInSecondCluster = static_cast<int32_t>(colEndByte & (Cluster::size - 1));

			// If more in first cluster...
			if (bytesInFirstCluster >= bytesInSecondCluster) {
				clusterIndexToDo = colStartCluster;
				startByteWithinCluster = startByteWithinFirstCluster;
				endByteWithinCluster = Cluster::size;
			}

			// Or if more in second cluster...
			else {
				clusterIndexToDo = colEndCluster;

				int32_t unusedBytesAtEndOfPrevCluster =
				    (Cluster::size - startByteWithinFirstCluster) % (sample->numChannels * sample->byteDepth);
				if (unusedBytesAtEndOfPrevCluster == 0) {
					startByteWithinCluster = 0;
				}
				else {
					startByteWithinCluster = (sample->numChannels * sample->byteDepth) - unusedBytesAtEndOfPrevCluster;
				}

				endByteWithinCluster = bytesInSecondCluster;
			}
		}

		if (clusterIndexToDo >= endClusters) { // Could this actually happen?
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}

		else if (clusterIndexToDo == endClusters - 1) {

			int32_t limit = lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}

		SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndexToDo);

		if (sampleCluster->cluster && sampleCluster->cluster->numReasonsToBeLoaded < 0) {
			FREEZE_WITH_ERROR("E449"); // Trying to catch errer before i028, which users have gotten.
		}

		// If we're wanting to investigate the whole length of one Cluster, and that's already actually been done
		// previously, we can just reuse those findings!
		if (investigatingAWholeCluster && sampleCluster->investigatedWholeLength) {
			data->minPerCol[col] = (int32_t)sampleCluster->minValue << 24;
			data->maxPerCol[col] = (int32_t)sampleCluster->maxValue << 24;
		}

		// Otherwise, do our normal investigation
		else {
			char const* errorCode;
			if (sampleCluster->cluster) {
				if (sampleCluster->cluster->loaded) {
					errorCode = "E343";
				}
				else {
					errorCode = "E344";
				}
			}
			else {
				errorCode = "E341"; // Qui got this, around V3.1.3! And Steven G, 3.1.5. And Brawny, V4.0.1-RC! And then
				                    // Malte P.
			}

			Cluster* cluster = sampleCluster->getCluster(sample, clusterIndexToDo, CLUSTER_LOAD_IMMEDIATELY);
			if (!cluster) {
cantReadData:
				D_PRINTLN("cant read");
				data->colStatus[col] = 0;
				hadAnyTroubleLoading = true;
				continue;
			}

			if (cluster->numReasonsToBeLoaded <= 0) {
				// Branko V got this. Trying to catch E340 below, which Ron R got while recording
				FREEZE_WITH_ERROR(errorCode);
			}

			uint32_t numBytesToRead = endByteWithinCluster - startByteWithinCluster;

			// Make the end-byte earlier, so we won't read past the end of the Cluster boundary
			int32_t overshoot = numBytesToRead % (sample->numChannels * sample->byteDepth);
			endByteWithinCluster -= overshoot;

			// However, if that's reduced us to 0 bytes to read, we know we're gonna have to load in the next Cluster to
			// get its sample that's on the boundary
			Cluster* nextCluster = nullptr;
			if (endByteWithinCluster <= startByteWithinCluster && clusterIndexToDo < endClusters - 1) {
				endByteWithinCluster += overshoot;
				SampleCluster* nextSampleCluster = sample->clusters.getElement(clusterIndexToDo + 1);
				if ((nextSampleCluster->cluster != nullptr) && nextSampleCluster->cluster->numReasonsToBeLoaded < 0) {
					FREEZE_WITH_ERROR("E450"); // Trying to catch errer before i028, which users have gotten.
				}
				nextCluster = nextSampleCluster->getCluster(sample, clusterIndexToDo, CLUSTER_LOAD_IMMEDIATELY);

				if (cluster->numReasonsToBeLoaded <= 0) {
					FREEZE_WITH_ERROR("E342"); // Trying to catch E340 below, which Ron R got while recording
				}

				if (nextCluster == nullptr) {
					audioFileManager.removeReasonFromCluster(*cluster, "po8w");
					goto cantReadData;
				}
			}

			numBytesToRead = endByteWithinCluster - startByteWithinCluster;

			// NOTE: from here on, we read *both* channels (if there are two), counting each one as a "sample"
			WaveformPeak peak = scanClusterPeak(cluster->data, startByteWithinCluster, endByteWithinCluster,
			                                    sample->byteDepth, sample->numChannels);
			int32_t minThisCol = peak.min;
			int32_t maxThisCol = peak.max;

			// If we just looked at the length of one entire cluster...
			if (investigatingAWholeCluster) {

				// See if we want to include any previously captured maximums and minimums, which might have looked at
				// slightly different values
				int32_t prevMin = (int32_t)sampleCluster->minValue << 24;
				int32_t prevMax = (int32_t)sampleCluster->maxValue << 24;

				if (prevMin < minThisCol) {
					minThisCol = prevMin;
				}
				if (prevMax > maxThisCol) {
					maxThisCol = prevMax;
				}

				// And mark the SampleCluster as fully investigated (rounding toward 0)
				sampleCluster->minValue = toCoarsePeak(minThisCol);
				sampleCluster->maxValue = toCoarsePeak(maxThisCol);
				sampleCluster->investigatedWholeLength = true;
			}

			// Or, if we only looked at a smaller part of a cluster...
			else {

				// Then just contribute to the running record of max and min found
				int8_t smallMin = toCoarsePeak(minThisCol);
				int8_t smallMax = toCoarsePeak(maxThisCol);

				if (smallMin < sampleCluster->minValue) {
					sampleCluster->minValue = smallMin;
				}
				if (smallMax > sampleCluster->maxValue) {
					sampleCluster->maxValue = smallMax;
				}
			}

			data->maxPerCol[col] = maxThisCol;
			data->minPerCol[col] = minThisCol;

			audioFileManager.removeReasonFromCluster(*cluster, "E340"); // Ron R got this, when error was "iiuh"
			if (nextCluster != nullptr) {
				audioFileManager.removeReasonFromCluster(*nextCluster, "9700");
			}
			AudioEngine::routineWithClusterLoading();
		}
	}

	if (recorder != nullptr) {
		sample->maxValueFound = recorder->recordMax;
		sample->minValueFound = recorder->recordMin;
	}

	// Keep a running best for the max and min found for the whole Sample
	else {
		for (int32_t col = xStart; col < xEnd; col++) {
			if (data->colStatus[col] == COL_STATUS_INVESTIGATED) {
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

// Background "waveform overview" pre-scan (issue #4460). Investigates one whole cluster the same way
// findPeaksPerCol's whole-cluster branch does, but self-contained so it can run off the render path.
// Caches the result in the SampleCluster (int8 min/max + investigatedWholeLength) so later zoomed-out
// renders get a cache hit and never load this cluster synchronously while the user is scrolling.
bool WaveformRenderer::investigateWholeCluster(Sample* sample, int32_t clusterIndex) {

	const int32_t endClusters = sample->getFirstClusterIndexWithNoAudioData();
	if (clusterIndex < 0 || clusterIndex >= endClusters) {
		return true; // Nothing to do
	}

	SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndex);
	if (sampleCluster->investigatedWholeLength) {
		return true; // Already cached (perhaps by a previous render)
	}

	// Don't cache a cluster a recorder is still writing into - its data isn't final yet. We leave the
	// scan cursor parked here; once recording moves on / finishes, the reason is released and we proceed.
	if (sampleCluster->cluster != nullptr && sampleCluster->cluster->numReasonsHeldBySampleRecorder > 0) {
		return false;
	}

	const int32_t frameSize = sample->numChannels * sample->byteDepth;
	const uint64_t numValidBytes = sample->lengthInSamples * sample->byteDepth * sample->numChannels;

	// Work out the first frame-aligned byte of audio data within this cluster. 64-bit so multi-GB samples
	// (cluster index << size_magnitude) don't overflow (#4460).
	const int64_t clusterStartByteAbs = clusterStartByte(clusterIndex, Cluster::size_magnitude);
	int32_t startByteWithinCluster;
	if (clusterStartByteAbs <= static_cast<int64_t>(sample->audioDataStartPosBytes)) {
		// This cluster still contains the file header; audio data begins at audioDataStartPosBytes.
		startByteWithinCluster = static_cast<int32_t>(sample->audioDataStartPosBytes - clusterStartByteAbs);
	}
	else {
		const int32_t rem = static_cast<int32_t>((clusterStartByteAbs - sample->audioDataStartPosBytes) % frameSize);
		startByteWithinCluster = (rem == 0) ? 0 : (frameSize - rem);
	}

	int32_t endByteWithinCluster = Cluster::size;
	if (clusterIndex == endClusters - 1) {
		endByteWithinCluster =
		    std::min(endByteWithinCluster,
		             lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size));
	}

	// Trim the end back to a whole frame so we never read past the cluster boundary.
	endByteWithinCluster -= (endByteWithinCluster - startByteWithinCluster) % frameSize;

	if (endByteWithinCluster <= startByteWithinCluster) {
		// No whole frames to scan here (e.g. a sub-frame tail). Leave it un-investigated rather than
		// stamping an inverted sentinel; advanceOverviewScan advances the cursor past it regardless.
		return true;
	}

	Cluster* cluster = sampleCluster->getCluster(sample, clusterIndex, CLUSTER_LOAD_IMMEDIATELY);
	if (cluster == nullptr) {
		return false; // Card busy / couldn't read - caller will retry later
	}

	WaveformPeak peak = scanClusterPeak(cluster->data, startByteWithinCluster, endByteWithinCluster, sample->byteDepth,
	                                    sample->numChannels);

	// Fold in anything previously captured for this cluster (same as findPeaksPerCol's whole-cluster path).
	peak.min = std::min(peak.min, static_cast<int32_t>(sampleCluster->minValue) << 24);
	peak.max = std::max(peak.max, static_cast<int32_t>(sampleCluster->maxValue) << 24);

	// Store the coarse (int8) overview, rounding towards 0.
	sampleCluster->minValue = toCoarsePeak(peak.min);
	sampleCluster->maxValue = toCoarsePeak(peak.max);
	sampleCluster->investigatedWholeLength = true;

	// Keep the sample's running peak up to date too, so brightness normalisation is pre-warmed.
	sample->maxValueFound = std::max(sample->maxValueFound, peak.max);
	sample->minValueFound = std::min(sample->minValueFound, peak.min);

	audioFileManager.removeReasonFromCluster(*cluster, "4460");
	AudioEngine::routineWithClusterLoading();

	return true;
}

// Advances the sample's background overview pre-scan by up to maxClusters previously-uninvestigated
// clusters. Cheaply skips clusters already investigated (e.g. by a render). Returns true while work
// remains, so the idle driver knows to come back.
bool WaveformRenderer::advanceOverviewScan(Sample* sample, int32_t maxClusters) {

	const int32_t endClusters = sample->getFirstClusterIndexWithNoAudioData();

	// If the valid range shrank (e.g. after invalidation), clamp the cursor back into range.
	sample->overviewScanNextCluster = std::min(sample->overviewScanNextCluster, endClusters);

	for (int32_t budget = maxClusters; budget > 0 && sample->overviewScanNextCluster < endClusters;) {
		const int32_t clusterIndex = sample->overviewScanNextCluster;

		if (sample->clusters.getElement(clusterIndex)->investigatedWholeLength) {
			sample->overviewScanNextCluster++; // Free; don't spend budget on cache hits
			continue;
		}

		// Bail if the card or audio routine became busy since entry, rather than issuing another
		// synchronous load this tick (#4460). `currentlyAccessingCard` is the file-scope extern declared at
		// the top of this file; `clusterBeingLoaded` is a public AudioFileManager member.
		if (currentlyAccessingCard || (audioFileManager.clusterBeingLoaded != nullptr)
		    || AudioEngine::audioRoutineLocked) {
			return true;
		}

		if (!investigateWholeCluster(sample, clusterIndex)) {
			return true; // Card busy or still recording - retry this cluster next time without advancing
		}

		sample->overviewScanNextCluster++;
		budget--;
	}

	return sample->overviewScanNextCluster < endClusters;
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

	if (data->colStatus[xDisplaySource] == COL_STATUS_INVESTIGATED) {

		getColBarPositions(xDisplaySource, data, &min24, &max24, sample->getFoundValueCentrePoint(),
		                   sample->getValueSpan());

		drawColBar(xDisplay, min24, max24, thisImage, brightness, rgb);
	}
}
