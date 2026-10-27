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
#include "model/clip/clip.h"
#include "model/sample/sample_controls.h"
#include "model/sample/sample_holder_for_clip.h"
#include "model/sample/sample_playback_guide.h"
#include "model/sample/waveform_render_data.h"
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
	void loadSample(bool mayActuallyReadFile, deluge::storage::AudioLoadContext* context = nullptr);
	/// @brief Report the clip's sample to @p visitor, with naming the clip after it as the after-bind step. Loads
	///        nothing.
	/// @param visitor Receives the demand.
	/// @return Walk::Continue.
	deluge::storage::Walk walkSampleDemands(deluge::storage::SampleLoadVisitor& visitor);
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

	/// @brief Seed the engine runtime cache from the authoritative DocAudioClip.
	///
	/// Called at construction (model defaults) and after the DOM-first load parse. Seeds the sampleHolder +
	/// SampleControls caches (the DOM-owned cold scalars attack/voice_priority have no cache; they read the record
	/// directly). Does nothing when docAudioClip_ is null.
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
	/// @brief Attack, DOM-owned (`docAudioClip_->attack`).
	///
	/// A cold field, read only at note-on/per-render-block envelope setup (AudioOutput::resetEnvelope +
	/// renderGlobalEffectableForClip) and save, so the accessor reads the record directly rather than keeping a
	/// derived cache.
	/// @return The attack value held in the record.
	int32_t attack() const;
	/// @brief Set the attack in the record.
	/// @param value New attack value.
	void setAttack(int32_t value);

	/// @brief Voice priority, DOM-owned (`docAudioClip_->voice_priority`).
	///
	/// A cold field, read only at voice-cull time (getCullImmunity) and save, so the accessor reads the record
	/// directly rather than keeping a derived cache. Written by the menu via the priority.h by-value seam.
	/// @return The voice priority held in the record.
	VoicePriority voicePriority() const;
	/// @brief Set the voice priority in the record.
	/// @param value New voice priority.
	void setVoicePriority(VoicePriority value);

	/// @name SampleControls descriptor setters
	/// The descriptor fields (interpolation / pitch-speed-independence / reversed) are DOM-owned
	/// (`docAudioClip_->{interpolation_mode,pitch_and_speed_independent,reversed}`). Unlike the cold scalars these
	/// are HOT (read per render sample via `sampleControls`), so `sampleControls` is a derived cache: the record is
	/// authoritative (save reads it, load seeds the cache in deriveEngineFromDoc), and every write goes through these
	/// setters, which update the record AND re-seed the cache. `sampleControls.invertReversed` is pure runtime
	/// playback state and has no record slot. Menu writes funnel here via the sample/utils by-value seam; render/read
	/// sites keep reading `sampleControls` directly.
	/// @{
	/// @brief Set the interpolation mode.
	/// @param mode New interpolation mode.
	void setInterpolationMode(InterpolationMode mode);
	/// @brief Set whether pitch and speed are independent.
	/// @param independent True for independent pitch and speed.
	void setPitchAndSpeedIndependent(bool independent);
	/// @brief Set whether playback is reversed.
	/// @param reversed True for reversed playback.
	void setReversed(bool reversed);
	/// @}

	/// @name sampleHolder / overdubs model setters
	/// Each writes `docAudioClip_->*` AND the engine cache (sampleHolder / overdubsShouldCloneOutput) but does NOT
	/// perform a reload: callers that need a reload (e.g. re-pointing the sample) do that themselves.
	/// @{
	/// @brief Set the sample start position.
	/// @param value Start position, in samples.
	void setSampleStartPos(uint64_t value);
	/// @brief Set the sample end position.
	/// @param value End position, in samples.
	void setSampleEndPos(uint64_t value);
	/// @brief Set the sample file path.
	/// @param value Null-terminated path.
	void setSampleFilePath(const char* value);
	/// @brief Set the sample transpose.
	/// @param value Transpose, in semitones.
	void setSampleTranspose(int32_t value);
	/// @brief Set the sample fine tune.
	/// @param value Fine tune, in cents.
	void setSampleCents(int32_t value);
	/// @brief Set whether overdubbing clones the output.
	/// @param value True to clone the output on overdub.
	void setOverdubsShouldCloneOutput(bool value);
	/// @}

	/// @brief Reflect the resolved sample window (file_path/start_pos/end_pos) from the engine cache into the record.
	///
	/// Call at the completion of a sample-load gesture (browser claim, linear-recording finish). It is the targeted
	/// inverse of `deriveEngineFromDoc` for the load-derived window: `loadFile`/`setAudioFile` set those three on the
	/// `sampleHolder` cache but never touch `docAudioClip_`, so without this the record would drift and save (which
	/// reads the record) would persist a stale/empty sample. Does nothing when `docAudioClip_` is null.
	void syncSampleRecordFromEngine();

	/// @brief This AudioClip's DOM-owned descriptor record, held in the project's `audio_clip_structure` keyed by id().
	///
	/// Bound in the AudioClip ctor under the clip's durable id (as the Clip base ctor binds `docClip_`); erased in
	/// ~AudioClip. The load path parses into it and derives the engine cache (deriveEngineFromDoc); all persistent
	/// fields read it directly, and the save path (writeDataToFile) reads it straight.
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
	void clearSample(Action* action);
	/// Priority for loading our Clusters: ahead of every Voice of the same VoicePriority (lower = more urgent).
	[[nodiscard]] int32_t getClusterPriority() const;
};
