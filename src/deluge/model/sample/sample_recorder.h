/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "dsp/envelope_follower/absolute_value.h"
#include "dsp/stereo_sample.h"
#include "fatfs/fatfs.hpp"
#include <cstddef>
#include <optional>

enum class MonitoringAction {
	NONE = 0,
	REMOVE_RIGHT_CHANNEL = 1,
	SUBTRACT_RIGHT_CHANNEL = 2,
};

enum class RecorderStatus {
	CAPTURING_DATA = 0,
	CAPTURING_DATA_WAITING_TO_STOP = 1,
	FINISHED_CAPTURING_BUT_STILL_WRITING = 2,
	COMPLETE = 3,

	// Means RAM error only. SD errors are noted separately and won't affect operation, as long as RAM lasts
	ABORTED = 4,
	AWAITING_DELETION = 5,
};

class Sample;
class Cluster;
class AudioClip;
class Output;
class SampleRecorder {
public:
	SampleRecorder() = default;
	~SampleRecorder();
	Error setup(int32_t newNumChannels, AudioInputChannel newMode, bool newKeepingReasons,
	            bool shouldRecordExtraMargins, AudioRecordingFolder newFolderID, int32_t buttonPressLatency,
	            Output* outputRecordingFrom);
	void setRecordingThreshold();
	void feedAudio(int32_t* inputAddress, int32_t numSamples, bool applyGain = false, uint8_t gainToApply = 5);
	Error cardRoutine();
	void endSyncedRecording(int32_t buttonLatencyForTempolessRecording);
	bool inputLooksDifferential();
	bool inputHasNoRightChannel();
	void removeFromOutput() {
		if (status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			abort();
		}
		outputRecordingFrom = nullptr;
	};
	void abort();

	SampleRecorder* next;

	gsl::owner<Sample*> sample;

	int32_t numSamplesToRunBeforeBeginningCapturing;
	uint32_t numSamplesBeenRunning;
	uint32_t numSamplesCaptured;

	uint32_t numSamplesExtraToCaptureAtEndSyncingWise;

	int32_t firstUnwrittenClusterIndex = 0;

	// Put things in valid state so if we get destructed before any recording, it's all ok
	int32_t currentRecordClusterIndex = -1;

	// Note! If this is NULL, that means that currentRecordClusterIndex refers to a cluster that never got created (cos
	// some error or max file size reached)
	Cluster* currentRecordCluster = nullptr;

	uint32_t audioFileNumber;
	AudioRecordingFolder folderID;

	char* writePos;
	char* clusterEndPos;

	// When this gets set, we add the Sample to the master list. This is stored here in addition to in the Sample,
	// so we can delete an aborted file even after the Sample has been detached / destructed.
	// This will be the temp file path if there is one.
	String filePathCreated;

	RecorderStatus status = RecorderStatus::CAPTURING_DATA;
	AudioInputChannel mode;
	Output* outputRecordingFrom; // for when recording from a specific output

	// Need to keep track of this, so we know whether to remove it. Well I guess we could just look and see if it's
	// there... but this is nice.
	bool haveAddedSampleToArray = false;

	bool allowFileAlterationAfter = false;
	bool allowNormalization = true;
	bool autoDeleteWhenDone = false;
	bool keepingReasonsForFirstClusters;
	uint8_t recordingNumChannels;
	bool hadCardError = false;
	bool reachedMaxFileSize = false;
	bool recordingExtraMargins = false;
	bool pointerHeldElsewhere = false;
	bool capturedTooMuch = false;
	bool thresholdRecording = false;

	// Most of these are not captured in the case of BALANCED input for AudioClips
	bool recordingClippedRecently;
	int32_t recordPeakL;
	int32_t recordPeakR;
	int32_t recordPeakLMinusR;
	uint64_t recordSumL;
	uint64_t recordSumR;
	uint64_t recordSumLPlusR;  // L and R are halved before these two are calculated
	uint64_t recordSumLMinusR; // --------

	int32_t recordMax;
	int32_t recordMin;

	uint32_t audioDataLengthBytesAsWrittenToFile;
	uint32_t loopEndSampleAsWrittenToFile;

	float startValueThreshold;

	int32_t* sourcePos;

	std::optional<FatFS::File> file;

private:
	void setExtraBytesOnPreviousCluster(Cluster* currentCluster, int32_t currentClusterIndex);
	Error writeCluster(int32_t clusterIndex, size_t numBytes);
	Error alterFile(MonitoringAction action, int32_t lshiftAmount, uint32_t idealFileSizeBeforeAction,

	                uint64_t dataLengthAfterAction);
	Error finalizeRecordedFile();
	Error createNextCluster();
	Error writeAnyCompletedClusters();
	void finishCapturing();
	void updateDataLengthInFirstCluster(Cluster* cluster);
	void totalSampleLengthNowKnown(uint32_t totalLength, uint32_t loopEndPointSamples = 0);
	void detachSample();
	Error truncateFileDownToSize(uint32_t newFileSize);
	Error writeOneCompletedCluster();
	AbsValueFollower envelopeFollower{};
};
