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
#include "gui/views/audio_clip_view.h"
#include "gui/waveform/waveform_render_data.h"
#include "model/clip/clip.h"
#include "model/note/note_vector.h"
#include "model/sample/sample_controls.h"
#include "model/sample/sample_holder_for_clip.h"
#include "model/sample/sample_playback_guide.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "util/d_string.h"

class Note;
class VoiceSample;
class SampleRecorder;
class ModelStackWithTimelineCounter;

class AudioClip final : public Clip {
public:
	AudioClip();
	~AudioClip() override;
	void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast) override;
	void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true) override;
	Error clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false) const override;
	void render(ModelStackWithTimelineCounter* modelStack, std::span<q31_t> output, int32_t amplitude,
	            int32_t amplitudeIncrement, int32_t pitchAdjust);
	void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                      bool shouldDeleteEmptyNoteRowsAtEndOfList = false, bool shouldRetainLinksToSounds = false,
	                      bool keepNoteRowsWithMIDIInput = true, bool shouldGrabMidiCommands = false,
	                      bool shouldBackUpExpressionParamsToo = true) override;
	bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen, int32_t xScroll,
	                       uint32_t xZoom, RGB* image, uint8_t occupancyMask[], bool addUndefinedArea,
	                       int32_t noteRowIndexStart = 0, int32_t noteRowIndexEnd = 2147483647, int32_t xStart = 0,
	                       int32_t xEnd = kDisplayWidth, bool allowBlur = true, bool drawRepeats = false) override;
	Error claimOutput(ModelStackWithTimelineCounter* modelStack) override;
	void loadSample(bool mayActuallyReadFile);
	bool wantsToBeginLinearRecording(Song* song) override;
	bool isAbandonedOverdub() override;
	void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
	                           int32_t buttonLatencyForTempolessRecord) override;
	Error beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) override;
	void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                           uint32_t timeRemainder, int32_t suggestedLength,
	                                           int32_t alternativeLongerLength) override;

	// we can only do in place overdubs if input monitoring is on right now, recording input seperately might come later
	// if we're in rows mode then use the old cloning method instead
	bool shouldCloneForOverdubs() override { return ((AudioOutput*)output)->mode != AudioOutputMode::looper; };
	Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature) override;
	int64_t getSamplesFromTicks(int32_t ticks);
	void unassignVoiceSample(bool wontBeUsedAgain);
	void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true) override;
	Error changeOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput);
	Error setOutput(ModelStackWithTimelineCounter* modelStack, Output* newOutput,
	                AudioClip* favourClipForCloningParamManager = nullptr);
	RGB getColour();
	bool currentlyScrollableAndZoomable() override;
	void getScrollAndZoomInSamples(int32_t xScroll, int32_t xZoom, int64_t* xScrollSamples, int64_t* xZoomSamples);
	void clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
	           bool clearSequenceAndMPE) override;
	bool getCurrentlyRecordingLinearly() override;
	void abortRecording() override;
	void setupPlaybackBounds();
	uint64_t getCullImmunity();
	void posReachedEnd(ModelStackWithTimelineCounter* modelStack) override;
	void copyBasicsFrom(Clip const* otherClip) override;
	bool willCloneOutputForOverdub() override { return overdubsShouldCloneOutput; }
	void sampleZoneChanged(ModelStackWithTimelineCounter const* modelStack);
	int64_t getNumSamplesTilLoop(ModelStackWithTimelineCounter* modelStack);
	void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos, bool useActualPosForParamManagers) override;
	/// Return true if successfully shifted, as clip cannot be shifted past beginning
	bool shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount, bool shiftAutomation,
	                       bool shiftSequenceAndMPE) override;

	Error readFromFile(Deserializer& reader, Song* song) override;
	void writeDataToFile(Serializer& writer, Song* song) override;
	char const* getXMLTag() override { return "audioClip"; }
	int32_t nextSampleRestartPos;
	SampleControls sampleControls;

	SampleHolderForClip sampleHolder;

	VoiceSample* voiceSample;

	SamplePlaybackGuide guide;

	String outputNameWhileLoading; // Only valid while loading

	WaveformRenderData renderData;
	// TODO: For looping without monitoring we'll need a second recorder plus maybe a second sample player?
	SampleRecorder* recorder; // Will be set to NULL right at the end of the loop's recording, even though the
	                          // SampleRecorder itself will usually persist slightly longer
	int32_t attack;

	VoicePriority voicePriority;

	bool doingLateStart;
	bool maySetupCache;

	// Gate view data — per-step muting for audio clips (analogous to NoteRow::gateCloses)
	NoteVector gateCloses;
	int32_t gateAmplitude{0x7FFFFFFF};
	bool gateOpen{true};
	uint8_t currentGateAttack{0};
	uint8_t currentGateRelease{0};

	bool isGateOpenAtPos(int32_t pos, int32_t effectiveLength);
	Note* getPrecedingGateClose(int32_t pos, int32_t effectiveLength);
	int32_t ticksTilNextGateEvent(int32_t pos, int32_t effectiveLength);

	bool renderSidebar(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return audioClipView.renderSidebar(whichRows, image, occupancyMask);
	};

	ParamManagerForTimeline* getCurrentParamManager() override;

	bool isEmpty(bool displayPopup = true) override;

protected:
	bool cloneOutput(ModelStackWithTimelineCounter* modelStack) override;

private:
	AudioClip* nextClipInSection;
	void removeClipFromSection(AudioClip* clip);
	void detachAudioClipFromOutput(Song* song, bool shouldRetainLinksToOutput, bool shouldTakeParamManagerWith = false);
	LoopType getLoopingType(ModelStackWithTimelineCounter const* modelStack);
};
