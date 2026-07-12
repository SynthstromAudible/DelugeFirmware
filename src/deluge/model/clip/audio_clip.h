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
#include "model/sample/sample_controls.h"
#include "model/sample/sample_holder_for_clip.h"
#include "model/sample/sample_playback_guide.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "util/c_string.h"

class VoiceSample;
class SampleRecorder;
class ModelStackWithTimelineCounter;

namespace deluge::project {
struct DocAudioClip;
}

class AudioClip final : public Clip {
public:
	AudioClip(Song& song, uint32_t loadedId = 0);
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

	/// M3 owner-4: residual engine→record sync for the descriptor fields that are NOT yet DOM-authoritative — the
	/// sampleHolder group (file_path / start_pos / end_pos / transpose / cents) and overdubs_should_clone. Called at
	/// save (writeDataToFile start) so the record reflects those engine-owned values. The DOM-owned fields (attack,
	/// voice_priority, and the SampleControls group) are NOT touched here — they funnel through their setters. The
	/// sampleHolder group is deferred because its sample window is authored by SampleHolder::loadFile at load time and
	/// reverted raw by two undo Consequences (ConsequenceClipLength via a stored uint64_t*,
	/// ConsequenceAudioClipSetSample); inverting it needs the M4 edit→model+undo funnel, not an owner-4 setter.
	/// overdubs_should_clone is a polymorphic Clip-base field. Self-guards on a null docAudioClip_.
	void syncDocAudioClip();
	/// M3 owner-4 (inverse): seed the engine runtime cache from the authoritative DocAudioClip. Called at
	/// construction (model defaults) and after the DOM-first load parse. Seeds the sampleHolder + SampleControls caches
	/// (the DOM-owned cold scalars attack/voice_priority have no cache — they read the record directly). Self-guards on
	/// a null docAudioClip_.
	void deriveEngineFromDoc();
	char const* getXMLTag() override { return "audioClip"; }
	int32_t nextSampleRestartPos{};
	SampleControls sampleControls;

	SampleHolderForClip sampleHolder;

	VoiceSample* voiceSample;

	SamplePlaybackGuide guide;

	std::string outputNameWhileLoading{}; // Only valid while loading

	WaveformRenderData renderData;
	// TODO: For looping without monitoring we'll need a second recorder plus maybe a second sample player?
	SampleRecorder* recorder; // Will be set to NULL right at the end of the loop's recording, even though the
	                          // SampleRecorder itself will usually persist slightly longer
	/// M3 owner-4: attack is DOM-owned (`docAudioClip_->attack`); the native engine field is retired. A cold field —
	/// read only at note-on/per-render-block envelope setup (AudioOutput::resetEnvelope +
	/// renderGlobalEffectableForClip) and save — so the accessor reads the record directly rather than keeping a
	/// derived cache.
	int32_t attack() const;
	void setAttack(int32_t value);

	/// M3 owner-4: voice priority is DOM-owned (`docAudioClip_->voice_priority`); the native engine field is retired.
	/// A cold field — read only at voice-cull time (getCullImmunity) and save — so the accessor reads the record
	/// directly rather than keeping a derived cache. Written by the menu via the priority.h by-value seam.
	VoicePriority voicePriority() const;
	void setVoicePriority(VoicePriority value);

	/// M3 owner-4: the SampleControls descriptor fields (interpolation / pitch-speed-independence / reversed) are
	/// DOM-owned (`docAudioClip_->{interpolation_mode,pitch_and_speed_independent,reversed}`). Unlike the cold scalars
	/// these are HOT — read per render sample via `sampleControls` — so `sampleControls` is retained as a derived
	/// cache: the record is authoritative (save reads it, load seeds the cache in deriveEngineFromDoc), and every
	/// write goes through these setters which update the record AND re-seed the cache. `sampleControls.invertReversed`
	/// is pure runtime playback state and has no record slot. Menu writes funnel here via the sample/utils by-value
	/// seam; render/read sites keep reading `sampleControls` directly.
	void setInterpolationMode(InterpolationMode mode);
	void setPitchAndSpeedIndependent(bool independent);
	void setReversed(bool reversed);

	/// M3 owner-4 prep (task A1): sampleHolder/overdubs model setters. These write `docAudioClip_->*` AND the engine
	/// cache (sampleHolder / overdubsShouldCloneOutput) but do NOT perform a reload — callers that need a reload
	/// (e.g. re-pointing the sample) keep doing that themselves; this only adds the record write. Not yet called
	/// anywhere (behavior-neutral until a later task wires call sites).
	void setSampleStartPos(uint64_t value);
	void setSampleEndPos(uint64_t value);
	void setSampleFilePath(const char* value);
	void setOverdubsShouldCloneOutput(bool value);

	/// M3 owner-4: this AudioClip's DOM-owned descriptor record, held in the project's `audio_clip_structure` keyed
	/// by id(). Bound in the AudioClip ctor under the clip's durable id (mirroring how the Clip base ctor binds
	/// `docClip_`); erased in ~AudioClip. The load path parses into it and derives the engine cache
	/// (deriveEngineFromDoc); retired fields (e.g. voice_priority) read it directly; the save path still rebuilds the
	/// not-yet-retired fields from the engine via syncDocAudioClip().
	deluge::project::DocAudioClip* docAudioClip_{nullptr};
	void bindDocAudioClip(deluge::project::DocAudioClip* d) { docAudioClip_ = d; }

	bool doingLateStart{};
	bool maySetupCache{};

	bool renderSidebar(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) override {
		return audioClipView.renderSidebar(whichRows, image, occupancyMask);
	};

	ParamManagerForTimeline* getCurrentParamManager() override;

	bool isEmpty(bool displayPopup = true) override;

protected:
	bool cloneOutput(ModelStackWithTimelineCounter* modelStack) override;

private:
	AudioClip* nextClipInSection{};
	void removeClipFromSection(AudioClip* clip);
	void detachAudioClipFromOutput(Song* song, bool shouldRetainLinksToOutput, bool shouldTakeParamManagerWith = false);
	LoopType getLoopingType(ModelStackWithTimelineCounter const* modelStack);
};
