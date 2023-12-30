/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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
#include "gui/waveform/waveform_render_data.h"
#include "model/clip/clip.h"
#include "model/sample/sample_controls.h"
#include "model/sample/sample_holder_for_clip.h"
#include "model/sample/sample_playback_guide.h"
#include "util/d_string.h"

class VoiceSample;
class SampleRecorder;
class ModelStackWithTimelineCounter;

class AudioClip final : public Clip {
public:
	AudioClip();
	~AudioClip();
	void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast);
	void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true);
	int32_t clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false);
	void render(ModelStackWithTimelineCounter* modelStack, int32_t* outputBuffer, int32_t numSamples, int32_t amplitude,
	            int32_t amplitudeIncrement, int32_t pitchAdjust);
	void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                      bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                      bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                      bool shouldBackUpExpressionParamsToo = true);
	bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
	                       uint32_t xZoom, uint8_t* image, uint8_t occupancyMask[], bool addUndefinedArea,
	                       int32_t noteRowIndexStart = 0, int32_t noteRowIndexEnd = 2147483647, int32_t xStart = 0,
	                       int32_t xEnd = kDisplayWidth, bool allowBlur = true, bool drawRepeats = false);
	int32_t claimOutput(ModelStackWithTimelineCounter* modelStack);
	void loadSample(bool mayActuallyReadFile);
	bool wantsToBeginLinearRecording(Song* song);
	bool isAbandonedOverdub();
	void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
	                           int32_t buttonLatencyForTempolessRecord);
	int32_t beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency);
	void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                           uint32_t timeRemainder, int32_t suggestedLength,
	                                           int32_t alternativeLongerLength);
	Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature);
	int64_t getSamplesFromTicks(int32_t ticks);
	void unassignVoiceSample();
	void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true);
	int32_t changeOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput);
	int32_t setOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput,
	                  AudioClip* favourClipForCloningParamManager = NULL);
	void getColour(uint8_t rgb[]);
	bool currentlyScrollableAndZoomable();
	void getScrollAndZoomInSamples(int32_t xScroll, int32_t xZoom, int64_t* xScrollSamples, int64_t* xZoomSamples);
	void clear(Action* action, ModelStackWithTimelineCounter* modelStack);
	bool getCurrentlyRecordingLinearly();
	void abortRecording();
	void setupPlaybackBounds();
	uint64_t getCullImmunity();
	void posReachedEnd(ModelStackWithTimelineCounter* modelStack);
	void copyBasicsFrom(Clip* otherClip);
	bool willCloneOutputForOverdub() { return overdubsShouldCloneOutput; }
	void sampleZoneChanged(ModelStackWithTimelineCounter const* modelStack);
	int64_t getNumSamplesTilLoop(ModelStackWithTimelineCounter* modelStack);
	void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers);
	/// Return true if successfully shifted, as clip cannot be shifted past beginning
	bool shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount);

	int32_t readFromFile(Song* song);
	void writeDataToFile(Song* song);
	char const* getXMLTag() { return "audioClip"; }

	SampleControls sampleControls;

	SampleHolderForClip sampleHolder;

	VoiceSample* voiceSample;

	SamplePlaybackGuide guide;

	String outputNameWhileLoading; // Only valid while loading

	WaveformRenderData renderData;

	SampleRecorder*
	    recorder; // Will be set to NULL right at the end of the loop's recording, even though the SampleRecorder itself will usually persist slightly longer

	int32_t attack;

	VoicePriority voicePriority;

	bool doingLateStart;
	bool maySetupCache;

	//START ~ new Automation Clip View Variables
	bool onAutomationAudioClipView; //new to save the view that you are currently in
	                                //(e.g. if you leave clip and want to come back where you left off)

	int32_t lastSelectedParamID;       //last selected Parameter to be edited in Automation Instrument Clip View
	Param::Kind lastSelectedParamKind; //0 = patched, 1 = unpatched, 2 = global effectable, 3 = none
	int32_t lastSelectedParamShortcutX;
	int32_t lastSelectedParamShortcutY;
	int32_t lastSelectedParamArrayPosition;
	InstrumentType lastSelectedInstrumentType;

	//END ~ new Automation Clip View Variables

protected:
	bool cloneOutput(ModelStackWithTimelineCounter* modelStack);

private:
	void detachAudioClipFromOutput(Song* song, bool shouldRetainLinksToOutput, bool shouldTakeParamManagerWith = false);
	LoopType getLoopingType(ModelStackWithTimelineCounter const* modelStack);
};
