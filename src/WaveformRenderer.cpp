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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <Cluster.h>
#include <InstrumentClipView.h>
#include <sound.h>
#include "definitions.h"
#include "matrixdriver.h"
#include <string.h>
#include <WaveformRenderer.h>
#include "Sample.h"
#include "uart.h"
#include "soundeditor.h"
#include "MultisampleRange.h"
#include "View.h"
#include "instrument.h"
#include "VoiceSample.h"
#include "KeyboardScreen.h"
#include "SampleRecorder.h"
#include "WaveformRenderData.h"

extern "C" {
extern uint8_t currentlyAccessingCard;
}

const uint8_t redColour[] = {255, 0, 0};
const uint8_t greenColour[] = {0, 255, 0};

WaveformRenderer waveformRenderer;

WaveformRenderer::WaveformRenderer() {
}

#define SAMPLES_TO_READ_PER_COL_MAGNITUDE 9

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool WaveformRenderer::renderFullScreen(Sample* sample, uint64_t xScroll, uint64_t xZoom,
                                        uint8_t thisImage[][displayWidth + sideBarWidth][3], WaveformRenderData* data,
                                        SampleRecorder* recorder, uint8_t rgb[], bool reversed, int xEnd) {

	bool completeSuccess = findPeaksPerCol(sample, xScroll, xZoom, data, recorder);
	if (!completeSuccess) return false;

	// Clear display
	for (int y = 0; y < displayHeight; y++) {
		memset(thisImage[y], 0, displayWidth * 3);
	}

	for (int xDisplay = 0; xDisplay < xEnd; xDisplay++) {
		renderOneCol(sample, xDisplay, thisImage, data, reversed, rgb);
	}

	return true;
}

// Returns false if had trouble loading some (will often not be all) Clusters
bool WaveformRenderer::renderAsSingleRow(Sample* sample, int64_t xScroll, uint64_t xZoom, uint8_t* thisImage,
                                         WaveformRenderData* data, SampleRecorder* recorder, uint8_t rgb[],
                                         bool reversed, int xStart, int xEnd) {

	int xStartSource = xStart;
	int xEndSource = xEnd;
	if (reversed) {
		xStartSource = displayWidth - 1 - xEnd;
		xEndSource = displayWidth - 1 - xStart;
	}

	bool completeSuccess = findPeaksPerCol(sample, xScroll, xZoom, data, recorder, xStartSource, xEndSource);
	if (!completeSuccess) return false;

	int32_t maxPeakFromZero = sample->getMaxPeakFromZero();

	for (int xDisplayOutput = xStart; xDisplayOutput < xEnd; xDisplayOutput++) {

		int xDisplaySource = xDisplayOutput;
		if (reversed) xDisplaySource = displayWidth - 1 - xDisplaySource;

		// If no data here (e.g. if Sample not recorded this far yet...)
		if (data->colStatus[xDisplaySource] != COL_STATUS_INVESTIGATED) {
			memset(&thisImage[xDisplayOutput * 3], 0, 3);
			continue;
		}

		int colourValue = getColBrightnessForSingleRow(xDisplaySource, maxPeakFromZero, data);
		colourValue = (colourValue * colourValue); // >> 8;
		//if (colourValue > 255) colourValue = 255; // May sometimes go juuust over

		for (int c = 0; c < 3; c++) {
			int valueHere = (colourValue * rgb[c]) >> 16;
			valueHere =
			    (valueHere + 6)
			    & ~15; // Limit the heck out of the bit depth, to avoid problem with PIC firmware where too many different colour shades cause big problems.
			           // The 6 is quite arbitrary, but I think it looks good
			if (valueHere > 255) valueHere = 255;
			thisImage[xDisplayOutput * 3 + c] = valueHere;
		}
	}

	return true;
}

// Value out of 255
int WaveformRenderer::getColBrightnessForSingleRow(int xDisplay, int32_t maxPeakFromZero, WaveformRenderData* data) {

	int32_t peak1 = std::abs(data->minPerCol[xDisplay]);
	int32_t peak2 = std::abs(data->maxPerCol[xDisplay]);

	int32_t peakHere = getMax(peak1, peak2);

	if (false && peakHere >= maxPeakFromZero) {
		Uart::print("peak: ");
		Uart::print(peakHere);
		Uart::print(" but max: ");
		Uart::println(maxPeakFromZero);
	}

	uint32_t peak16 = ((int64_t)peakHere << 16) / maxPeakFromZero;

	return getMin(peak16 >> 8, 256); // Max 256 - for now. Looks great and bright.
	    // Must manually limit this, cos if we've ended up with values higher than our maxPeakFromZero,
	    // there'd be trouble otherwise
}

void WaveformRenderer::renderOneColForCollapseAnimation(int xDisplayWaveform, int xDisplayOutput,
                                                        int32_t maxPeakFromZero, int progress,
                                                        uint8_t thisImage[][displayWidth + sideBarWidth][3],
                                                        WaveformRenderData* data, uint8_t rgb[], bool reversed,
                                                        int32_t valueCentrePoint, int32_t valueSpan) {

	int xDisplayData = xDisplayWaveform;
	if (reversed) xDisplayData = displayWidth - 1 - xDisplayData;

	if (data->colStatus[xDisplayData] != COL_STATUS_INVESTIGATED) return;

	int32_t min24, max24;
	getColBarPositions(xDisplayData, data, &min24, &max24, valueCentrePoint, valueSpan);

	int singleSquareBrightness = getColBrightnessForSingleRow(xDisplayData, maxPeakFromZero, data);

	renderOneColForCollapseAnimationInterpolation(xDisplayOutput, min24, max24, singleSquareBrightness, progress,
	                                              thisImage, rgb);
}

// For the explode animation. Crams multiple cols of source material into one col of output material.
// Crudely grabs the max values from all cols in range, which looks totally fine.
void WaveformRenderer::renderOneColForCollapseAnimationZoomedOut(int xDisplayWaveformLeftEdge,
                                                                 int xDisplayWaveformRightEdge, int xDisplayOutput,
                                                                 int32_t maxPeakFromZero, int progress,
                                                                 uint8_t thisImage[][displayWidth + sideBarWidth][3],
                                                                 WaveformRenderData* data, uint8_t rgb[], bool reversed,
                                                                 int32_t valueCentrePoint, int32_t valueSpan) {

	int xDisplayDataLeftEdge = xDisplayWaveformLeftEdge;
	int xDisplayDataRightEdge = xDisplayWaveformRightEdge;
	if (reversed) {
		xDisplayDataLeftEdge = displayWidth - 1 - xDisplayWaveformRightEdge;
		xDisplayDataRightEdge = displayWidth - 1 - xDisplayWaveformLeftEdge;
	}

	int32_t min24Total = 2147483647;
	int32_t max24Total = -2147483648;

	int singleSquareBrightnessTotal = 0;

	for (int xDisplayDataNow = xDisplayDataLeftEdge; xDisplayDataNow <= xDisplayDataRightEdge; xDisplayDataNow++) {
		if (data->colStatus[xDisplayDataNow] != COL_STATUS_INVESTIGATED) return;

		int32_t min24, max24;
		getColBarPositions(xDisplayDataNow, data, &min24, &max24, valueCentrePoint, valueSpan);

		if (min24 < min24Total) min24Total = min24;
		if (max24 > max24Total) max24Total = max24;

		int singleSquareBrightness = getColBrightnessForSingleRow(xDisplayDataNow, maxPeakFromZero, data);

		if (singleSquareBrightness > singleSquareBrightnessTotal) singleSquareBrightnessTotal = singleSquareBrightness;
	}

	renderOneColForCollapseAnimationInterpolation(xDisplayOutput, min24Total, max24Total, singleSquareBrightnessTotal,
	                                              progress, thisImage, rgb);
}

// Once we've derived the appropriate data from the waveform for one final col of pads,
// this does the vertical animation according to our current amount of expandedness
void WaveformRenderer::renderOneColForCollapseAnimationInterpolation(
    int xDisplayOutput, int32_t min24, int32_t max24, int singleSquareBrightness, int progress,
    uint8_t thisImage[][displayWidth + sideBarWidth][3], uint8_t rgb[]) {

	int32_t minStart = ((int32_t)collapseAnimationToWhichRow - (displayHeight >> 1)) << 24;
	int32_t maxStart = ((int32_t)collapseAnimationToWhichRow - (displayHeight >> 1) + 1) << 24;

	int32_t minDistance = min24 - minStart;
	int32_t maxDistance = max24 - maxStart;
	int brightnessDistance = 256 - singleSquareBrightness;

	int32_t minCurrent = minStart + (((int64_t)minDistance * progress) >> 16);
	int32_t maxCurrent = maxStart + (((int64_t)maxDistance * progress) >> 16);
	int brightnessCurrent = singleSquareBrightness + ((brightnessDistance * progress) >> 16);

	drawColBar(xDisplayOutput, minCurrent, maxCurrent, thisImage, brightnessCurrent, rgb);
}

// Returns false if had trouble loading some (will often not be all) Clusters, e.g. cos we're in the card routine
bool WaveformRenderer::findPeaksPerCol(Sample* sample, int64_t xScrollSamples, uint64_t xZoomSamples,
                                       WaveformRenderData* data, SampleRecorder* recorder, int xStart, int xEnd) {

	if (xScrollSamples != data->xScroll || xZoomSamples != data->xZoom) {
		memset(data->colStatus, 0, sizeof(data->colStatus));
	}

	data->xScroll = xScrollSamples;
	data->xZoom = xZoomSamples;

	uint64_t numValidSamples;
	int endClusters;
	if (recorder) {
		numValidSamples = recorder->numSamplesCaptured;
		endClusters = sample->clusters.getNumElements();
	}
	else {
		numValidSamples = sample->lengthInSamples;
		endClusters = sample->getFirstClusterIndexWithNoAudioData();
	}

	uint64_t numValidBytes = numValidSamples * sample->byteDepth * sample->numChannels;

	bool hadAnyTroubleLoading = false;

	for (int col = xStart; col < xEnd; col++) {

		if (data->colStatus[col] == COL_STATUS_INVESTIGATED) continue;

		data->colStatus[col] = COL_STATUS_INVESTIGATED; // Default, which we may override below

		int colStartSample = xScrollSamples + col * xZoomSamples;
		if (colStartSample >= numValidSamples) {
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}
		else if (colStartSample < 0) {
			colStartSample = 0;
		}

		int colEndSample = xScrollSamples + (col + 1) * xZoomSamples;

		// If this column extends further right than the end of the waveform...
		if (colEndSample >= numValidSamples) {

			// If we're still recording, we'll just want to come back and render this one when the waveform has grown to cover this whole column
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

		int colStartByte = colStartSample * sample->numChannels * sample->byteDepth + sample->audioDataStartPosBytes;
		int colEndByte = colEndSample * sample->numChannels * sample->byteDepth + sample->audioDataStartPosBytes;

		int colStartCluster = colStartByte >> audioFileManager.clusterSizeMagnitude;
		int colEndCluster = colEndByte >> audioFileManager.clusterSizeMagnitude;

		int clusterIndexToDo;
		int startByteWithinCluster;
		int endByteWithinCluster;

		int numClustersSpan = colEndCluster - colStartCluster;

		bool investigatingAWholeCluster = false;

		// If both same cluster...
		if (numClustersSpan == 0) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = colStartByte & (audioFileManager.clusterSize - 1);
			endByteWithinCluster = colEndByte & (audioFileManager.clusterSize - 1);
		}

		// Special case to make sure we get initial transient (we know there's more than 1 cluster)
		else if (colStartSample == 0 && colStartByte < (audioFileManager.clusterSize >> 1)) {
			clusterIndexToDo = colStartCluster;
			startByteWithinCluster = colStartByte & (audioFileManager.clusterSize - 1);
			endByteWithinCluster = audioFileManager.clusterSize;
			investigatingAWholeCluster = true;
		}

		// If 3 or more clusters, take 2nd one. TODO: have it take any one which has previously been fully investigated?
		else if (numClustersSpan >= 2) {
			clusterIndexToDo = colStartCluster + 1;

			int startByteWithinFirstCluster = colStartByte & (audioFileManager.clusterSize - 1);

			int unusedBytesAtEndOfPrevCluster = (audioFileManager.clusterSize - startByteWithinFirstCluster)
			                                    % (sample->numChannels * sample->byteDepth);
			if (unusedBytesAtEndOfPrevCluster == 0) startByteWithinCluster = 0;
			else startByteWithinCluster = (sample->numChannels * sample->byteDepth) - unusedBytesAtEndOfPrevCluster;

			endByteWithinCluster = audioFileManager.clusterSize;
			investigatingAWholeCluster = true;
		}

		// If 2 cluster..
		else if (numClustersSpan == 1) {

			int startByteWithinFirstCluster = colStartByte & (audioFileManager.clusterSize - 1);
			int bytesInFirstCluster = audioFileManager.clusterSize - startByteWithinFirstCluster;

			int bytesInSecondCluster = colEndByte & (audioFileManager.clusterSize - 1);

			// If more in first cluster...
			if (bytesInFirstCluster >= bytesInSecondCluster) {
				clusterIndexToDo = colStartCluster;
				startByteWithinCluster = startByteWithinFirstCluster;
				endByteWithinCluster = audioFileManager.clusterSize;
			}

			// Or if more in second cluster...
			else {
				clusterIndexToDo = colEndCluster;

				int unusedBytesAtEndOfPrevCluster = (audioFileManager.clusterSize - startByteWithinFirstCluster)
				                                    % (sample->numChannels * sample->byteDepth);
				if (unusedBytesAtEndOfPrevCluster == 0) startByteWithinCluster = 0;
				else startByteWithinCluster = (sample->numChannels * sample->byteDepth) - unusedBytesAtEndOfPrevCluster;

				endByteWithinCluster = bytesInSecondCluster;
			}
		}

		if (clusterIndexToDo >= endClusters) { // Could this actually happen?
			data->colStatus[col] = COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM;
			continue;
		}

		else if (clusterIndexToDo == endClusters - 1) {

			int limit = (numValidBytes + sample->audioDataStartPosBytes) & (audioFileManager.clusterSize - 1);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}

		SampleCluster* sampleCluster = sample->clusters.getElement(clusterIndexToDo);

		// If we're wanting to investigate the whole length of one Cluster, and that's already actually been done previously, we can just reuse those findings!
		if (investigatingAWholeCluster && sampleCluster->investigatedWholeLength) {
			data->minPerCol[col] = (int32_t)sampleCluster->minValue << 24;
			data->maxPerCol[col] = (int32_t)sampleCluster->maxValue << 24;
		}

		// Otherwise, do our normal investigation
		else {
			char const* errorCode;
			if (sampleCluster->cluster) {
				if (sampleCluster->cluster->loaded) errorCode = "E343";
				else errorCode = "E344";
			}
			else {
				errorCode =
				    "E341"; // Qui got this, around V3.1.3! And Steven G, 3.1.5. And Brawny, V4.0.1-RC! And then Malte P.
			}

			Cluster* cluster = sampleCluster->getCluster(sample, clusterIndexToDo, CLUSTER_LOAD_IMMEDIATELY);
			if (!cluster) {
cantReadData:
				Uart::println("cant read");
				data->colStatus[col] = 0;
				hadAnyTroubleLoading = true;
				continue;
			}

			if (cluster->numReasonsToBeLoaded <= 0)
				numericDriver.freezeWithError(
				    errorCode); // Branko V got this. Trying to catch E340 below, which Ron R got while recording

			uint32_t numBytesToRead = endByteWithinCluster - startByteWithinCluster;

			// Make the end-byte earlier, so we won't read past the end of the Cluster boundary
			int overshoot = numBytesToRead % (sample->numChannels * sample->byteDepth);
			endByteWithinCluster -= overshoot;

			// However, if that's reduced us to 0 bytes to read, we know we're gonna have to load in the next Cluster to get its sample that's on the boundary
			Cluster* nextCluster = NULL;
			if (endByteWithinCluster <= startByteWithinCluster && clusterIndexToDo < endClusters - 1) {
				endByteWithinCluster += overshoot;
				nextCluster = sample->clusters.getElement(clusterIndexToDo + 1)
				                  ->getCluster(sample, clusterIndexToDo, CLUSTER_LOAD_IMMEDIATELY);

				if (cluster->numReasonsToBeLoaded <= 0)
					numericDriver.freezeWithError(
					    "E342"); // Trying to catch E340 below, which Ron R got while recording

				if (!nextCluster) {
					audioFileManager.removeReasonFromCluster(cluster, "po8w");
					goto cantReadData;
				}
			}

			numBytesToRead = endByteWithinCluster - startByteWithinCluster;

			// NOTE: from here on, we read *both* channels (if there are two), counting each one as a "sample"

			int numSamplesToRead = numBytesToRead / sample->byteDepth;
			int byteIncrement = sample->byteDepth;

			// We don't want to read endless samples. If we were gonna read lost, skip some.
			int timesTooManySamples = ((numSamplesToRead - 1) >> SAMPLES_TO_READ_PER_COL_MAGNITUDE) + 1;
			if (timesTooManySamples > 1) {

				// If stereo sample, force an odd number here so we alternate between reading both channels
				if (sample->numChannels == 2) {
					if (!(timesTooManySamples & 1)) timesTooManySamples++;
				}

				byteIncrement *= timesTooManySamples;
			}

			// Misalign, to align with non-32-bit data
			startByteWithinCluster += sample->byteDepth - 4;
			endByteWithinCluster += sample->byteDepth - 4;

			int bytePos = startByteWithinCluster;

			int32_t minThisCol = 2147483647;
			int32_t maxThisCol = -2147483648;

			// Go through the actual waveform of this cluster
			while (bytePos < endByteWithinCluster) {

				int32_t individualSampleValue =
				    *(int32_t*)&cluster->data[bytePos]; // & sample->bitMask; // bitMask hardly matters here

				if (individualSampleValue > maxThisCol) maxThisCol = individualSampleValue;
				if (individualSampleValue < minThisCol) minThisCol = individualSampleValue;

				bytePos += byteIncrement;
			}

			// If we just looked at the length of one entire cluster...
			if (investigatingAWholeCluster) {

				// See if we want to include any previously captured maximums and minimums, which might have looked at slightly different values
				int32_t prevMin = (int32_t)sampleCluster->minValue << 24;
				int32_t prevMax = (int32_t)sampleCluster->maxValue << 24;

				if (prevMin < minThisCol) minThisCol = prevMin;
				if (prevMax > maxThisCol) maxThisCol = prevMax;

				// And mark the SampleCluster as fully investigated
				sampleCluster->minValue = minThisCol >> 24;
				sampleCluster->maxValue = maxThisCol >> 24;

				// Make rounding be towards 0
				if (sampleCluster->minValue < 0) sampleCluster->minValue++;
				if (sampleCluster->maxValue < 0) sampleCluster->maxValue++;

				sampleCluster->investigatedWholeLength = true;
			}

			// Or, if we only looked at a smaller part of a cluster...
			else {

				// Then just contribute to the running record of max and min found
				int8_t smallMin = minThisCol >> 24;
				int8_t smallMax = maxThisCol >> 24;

				// Make rounding be towards 0
				if (smallMin < 0) smallMin++;
				if (smallMax < 0) smallMax++;

				if (smallMin < sampleCluster->minValue) sampleCluster->minValue = smallMin;
				if (smallMax > sampleCluster->maxValue) sampleCluster->maxValue = smallMax;
			}

			data->maxPerCol[col] = maxThisCol;
			data->minPerCol[col] = minThisCol;

			audioFileManager.removeReasonFromCluster(cluster, "E340"); // Ron R got this, when error was "iiuh"
			if (nextCluster) audioFileManager.removeReasonFromCluster(nextCluster, "9700");

			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}
	}

	if (recorder) {
		sample->maxValueFound = recorder->recordMax;
		sample->minValueFound = recorder->recordMin;
	}

	// Keep a running best for the max and min found for the whole Sample
	else {
		for (int col = xStart; col < xEnd; col++) {
			if (data->colStatus[col] == COL_STATUS_INVESTIGATED) {
				if (data->maxPerCol[col] > sample->maxValueFound) sample->maxValueFound = data->maxPerCol[col];
				if (data->minPerCol[col] < sample->minValueFound) sample->minValueFound = data->minPerCol[col];
			}
		}
	}

	return !hadAnyTroubleLoading;
}

void WaveformRenderer::getColBarPositions(int xDisplay, WaveformRenderData* data, int32_t* min24, int32_t* max24,
                                          int32_t valueCentrePoint, int32_t valueSpan) {
	*min24 = ((int64_t)(data->minPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;
	*max24 = ((int64_t)(data->maxPerCol[xDisplay] - valueCentrePoint) << 24) / valueSpan;

	// Ensure we're going to draw at least 1 pixel's width
	if (*max24 - *min24 < 16777216) {
		int midPoint = (*max24 >> 1) + (*min24 >> 1);
		*max24 = midPoint + 8388608;
		*min24 = midPoint - 8388608;
	}
}

void WaveformRenderer::drawColBar(int xDisplay, int32_t min24, int32_t max24,
                                  uint8_t thisImage[][displayWidth + sideBarWidth][3], int brightness, uint8_t rgb[]) {
	int yStart = getMax((int)(min24 >> 24), -(displayHeight >> 1));
	int yStop = getMin((int)(max24 >> 24) + 1, displayHeight >> 1);

	for (int y = yStart; y < yStop; y++) {

		int colourAmount; // Out of 256

		if (y == (min24 >> 24)) {
			int howMuchThisSquare = (min24 - (y << 24)) >> 16; // Comes out as 8-bit
			colourAmount = brightness - ((howMuchThisSquare * brightness) >> 8);
		}

		else if (y < (max24 >> 24)) {
			colourAmount = brightness;
		}

		else {
			int howMuchThisSquare = (max24 - (y << 24)) >> 16; // Comes out as 8-bit
			colourAmount = ((howMuchThisSquare * brightness) >> 8);
		}

		for (int c = 0; c < 3; c++) {
			int valueHere = (colourAmount * colourAmount) >> 8;

			if (rgb) valueHere = (valueHere * rgb[c]) >> 8;

			thisImage[y + (displayHeight >> 1)][xDisplay][c] = valueHere;
		}
	}
}

void WaveformRenderer::renderOneCol(Sample* sample, int xDisplay, uint8_t thisImage[][displayWidth + sideBarWidth][3],
                                    WaveformRenderData* data, bool reversed, uint8_t rgb[]) {
	int32_t min24, max24;
	int brightness = rgb ? 256 : 128;

	int xDisplaySource = reversed ? (displayWidth - 1 - xDisplay) : xDisplay;

	if (data->colStatus[xDisplaySource] == COL_STATUS_INVESTIGATED) {

		getColBarPositions(xDisplaySource, data, &min24, &max24, sample->getFoundValueCentrePoint(),
		                   sample->getValueSpan());

		drawColBar(xDisplay, min24, max24, thisImage, brightness, rgb);
	}
}
