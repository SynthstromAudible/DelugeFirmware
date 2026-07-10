/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "deluge-project/doc_clip.h"
#include "gui/colour/colour.h"
#include "gui/views/audio_clip_view.h"
#include "gui/waveform/waveform_render_data.h"
#include "io/midi/learned_midi.h"
#include "model/clip/clip.h"
#include "model/sample/sample_controls.h"
#include "model/sample/sample_holder_for_clip.h"
#include "model/sample/sample_playback_guide.h"
#include "model/timeline_counter.h"
#include "modulation/params/param.h"
#include "util/c_string.h"
#include <cstdint>
#include <optional>

class Song;
class ParamManagerForTimeline;
class Output;
class AudioClip;
class InstrumentClip;
class Action;
class TimelineView;
class ParamManagerForTimeline;
class ModelStackWithTimelineCounter;
class Serializer;
class Deserializer;

namespace deluge::storage {
class Value;
}

class Clip : public TimelineCounter {

public:
	Clip(ClipType newType, Song& song, uint32_t loadedId = 0);
	~Clip() override;
	bool cancelAnyArming();
	int32_t getMaxZoom();
	virtual int32_t getMaxLength();
	virtual Error clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing = false) const = 0;
	void cloneFrom(Clip const* other);
	void beginInstance(Song* song, int32_t arrangementRecordPos);
	void endInstance(int32_t arrangementRecordPos, bool evenIfOtherClip = false);
	virtual void setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos,
	                    bool useActualPosForParamManagers = true);
	virtual void setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos = true);
	virtual void expectNoFurtherTicks(Song* song, bool actuallySoundChange = true) = 0;
	virtual void resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound = true) = 0;
	virtual void reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack);
	virtual void processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast);
	void prepareForDestruction(ModelStackWithTimelineCounter* modelStack,
	                           InstrumentRemoval instrumentRemovalInstruction);
	uint32_t getActualCurrentPosAsIfPlayingInForwardDirection();
	int32_t getCurrentPosAsIfPlayingInForwardDirection();
	Clip* getClipBeingRecordedFrom();
	Clip* getClipToRecordTo();
	[[nodiscard]] bool isArrangementOnlyClip() const;
	bool isActiveOnOutput();
	virtual bool deleteSoundsWhichWontSound(Song* song);
	virtual Error appendClip(ModelStackWithTimelineCounter* thisModelStack,
	                         ModelStackWithTimelineCounter* otherModelStack);
	Error resumeOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                      ModelStackWithTimelineCounter* modelStackClone);
	virtual Error transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
	                                                        ModelStackWithTimelineCounter* modelStackClone) {
		return Error::NONE;
	}
	virtual void increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
	                                       IndependentNoteRowLengthIncrease independentNoteRowInstruction,
	                                       bool completelyRenderOutIterationDependence = false,
	                                       Action* action = nullptr) {
	} // This is not implemented for AudioClips - because in the cases where we call this, we don't want it to happen
	  // for AudioClips
	virtual void lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action = nullptr);
	virtual void getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager, Sound* sound);
	virtual ParamManagerForTimeline* getCurrentParamManager() { return nullptr; }

	// You're likely to want to call pickAnActiveClipIfPossible() after this
	virtual void detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumName,
	                              bool shouldDeleteEmptyNoteRowsAtEndOfList = false,
	                              bool shouldRetainLinksToSounds = false, bool keepNoteRowsWithMIDIInput = true,
	                              bool shouldGrabMidiCommands = false, bool shouldBackUpExpressionParamsToo = true) = 0;

	virtual Error undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack);
	virtual bool renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen,
	                               int32_t xScroll, uint32_t xZoom, RGB* image, uint8_t occupancyMask[],
	                               bool addUndefinedArea = true, int32_t noteRowIndexStart = 0,
	                               int32_t noteRowIndexEnd = 2147483647, int32_t xStart = 0,
	                               int32_t xEnd = kDisplayWidth, bool allowBlur = true, bool drawRepeats = false);

	// To be called after Song loaded, to link to the relevant Output object
	virtual Error claimOutput(ModelStackWithTimelineCounter* modelStack) = 0;
	virtual void finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop = nullptr,
	                                   int32_t buttonLatencyForTempolessRecord = 0) = 0;
	virtual Error beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) = 0;
	void drawUndefinedArea(int32_t localScroll, uint32_t, int32_t lengthToDisplay, RGB* image, uint8_t[],
	                       int32_t imageWidth, TimelineView* editorScreen, bool tripletsOnHere);
	bool opportunityToBeginSessionLinearRecording(ModelStackWithTimelineCounter* modelStack, bool* newOutputCreated,
	                                              int32_t buttonPressLatency);
	virtual Clip* cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature) = 0;

	/// returns whether the clip needs to be cloned for overdubs (old style) or will actually overdub its audio like a
	/// normal looper
	virtual bool shouldCloneForOverdubs() {
		return true; // instrument version of in place overdub doesn't quite work so never do it for now
	};
	void setupOverdubInPlace(OverDubType type);
	virtual bool getCurrentlyRecordingLinearly() = 0;
	virtual bool currentlyScrollableAndZoomable() = 0;
	virtual void clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
	                   bool clearSequenceAndMPE);

	void writeToFile(Serializer& writer, Song* song);
	virtual void writeDataToFile(Serializer& writer, Song* song);
	void writeMidiCommandsToFile(Serializer& writer, Song* song);
	virtual char const* getXMLTag() = 0;
	virtual Error readFromFile(Deserializer& reader, Song* song) = 0;
	// Read this clip type's pure view-state from its Value node (durable-id readFromTree slice 4); called by
	// Song::readClipsFromFile right after construction. Base reads the fields shared by both clip types.
	virtual void readShellFromTree(const deluge::storage::Value& clipNode);

	virtual void copyBasicsFrom(Clip const* otherClip);
	void setupForRecordingAsAutoOverdub(Clip* existingClip, Song* song, OverDubType newOverdubNature);
	void outputChanged(ModelStackWithTimelineCounter* modelStack, Output* newOutput);
	virtual bool isAbandonedOverdub() = 0;
	virtual bool wantsToBeginLinearRecording(Song* song);
	virtual void quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack, int32_t lengthSoFar,
	                                                   uint32_t timeRemainder, int32_t suggestedLength,
	                                                   int32_t alternativeLongerLength) = 0;
	virtual void abortRecording() = 0;
	virtual void stopAllNotesPlaying(Song* song, bool actuallySoundChange = true) {}
	virtual bool willCloneOutputForOverdub() { return false; }
	void setSequenceDirectionMode(ModelStackWithTimelineCounter* modelStack, SequenceDirection newSequenceDirection);
	virtual void incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks);
	/// Return true if successfully shifted
	virtual bool shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount, bool shiftAutomation,
	                               bool shiftSequenceAndMPE) = 0;

	// ----- TimelineCounter implementation -------
	[[nodiscard]] uint32_t getLivePos() const override;
	[[nodiscard]] int32_t getLoopLength() const override;
	[[nodiscard]] bool isPlayingAutomationNow() const override;
	[[nodiscard]] bool backtrackingCouldLoopBackToEnd() const override;
	[[nodiscard]] int32_t getPosAtWhichPlaybackWillCut(ModelStackWithTimelineCounter const* modelStack) const override;
	[[nodiscard]] int32_t getLastProcessedPos() const override;
	bool possiblyCloneForArrangementRecording(ModelStackWithTimelineCounter* modelStack) override;
	TimelineCounter* getTimelineCounterToRecordTo() override;
	void getActiveModControllable(ModelStackWithTimelineCounter* modelStack) override;
	void expectEvent() override;

	Output* output;

	/// M3 owner-1: clip colour offset lives in the DOM DocClip (see loopLength()).
	[[nodiscard]] int16_t colourOffset() const { return treeDoc_->colour_offset; }
	void setColourOffset(int32_t newOffset) { treeDoc_->colour_offset = static_cast<int16_t>(newOffset); }

	const ClipType type;
	/// M3 owner-1: clip section lives in the DOM DocClip (see loopLength()). 255 = arrangement-only.
	[[nodiscard]] uint8_t section() const { return treeDoc_->section; }
	void setSection(uint8_t newSection) { treeDoc_->section = newSection; }
	bool soloingInSessionMode;
	ArmState armState;
	bool activeIfNoSolo;
	bool activeIfNoSoloBeforeStemExport; // Used by stem export to restore previous state
	bool exportStem;                     // Used by stem export to flag if this note row should be exported
	bool wasActiveBefore;                // A temporary thing used by Song::doLaunch()
	bool gotInstanceYet{};               // For use only while loading song

	bool isPendingOverdub;
	bool isUnfinishedAutoOverdub;
	bool wasWantingToDoLinearRecordingBeforeCountIn{}; // Only valid during a count-in
	OverDubType overdubNature;

	LearnedMIDI muteMIDICommand;

	/// M3 owner-1: clip structure lives in a DOM `DocClip` (`Song::clip_structure`), never an engine field. Durable-id
	/// Step 3: `treeDoc_` is bound in the Clip ctor under the clip's durable id and is authoritative for the clip's
	/// whole life -- always non-null, so there is no fallback and no bind-at-creation step. It points into a std::map
	/// (clip_structure), so it is address-stable across other clips' inserts; `~Clip` removes the entry.
	[[nodiscard]] int32_t loopLength() const { return treeDoc_->loop_length; }
	void setLoopLength(int32_t newLength) { treeDoc_->loop_length = newLength; }
	/// Repoint this clip's structure at a specific tree record (used by edit gestures that dual-write the store).
	void bindDomClip(deluge::project::DocClip* d) { treeDoc_ = d; }
	/// Whether this clip's structure is bound to a tree record. Always true post-ctor (kept for legacy call sites).
	[[nodiscard]] bool isDomBound() const { return treeDoc_ != nullptr; }
	deluge::project::DocClip* treeDoc_{nullptr};

	// Before linear recording of this Clip began, and this Clip started getting extended to multiples of this
	int32_t originalLength;

	int32_t lastProcessedPos{};

	Clip* beingRecordedFromClip;

	int32_t repeatCount{};

	uint32_t indexForSaving{}; // For use only while saving song

	/// Stable, persisted, per-song serial identity (the DOM reference foundation, distinct from the
	/// transient positional indexForSaving). nullopt until assigned lazily on first save from the
	/// song's counter (or restored on load); resolve via Song::clipForId.
	[[nodiscard]] uint32_t id() const { return id_; }

	/// M3 owner-1: clip launch style lives in the DOM DocClip (see loopLength()).
	[[nodiscard]] LaunchStyle launchStyle() const { return static_cast<LaunchStyle>(treeDoc_->launch_style); }
	void setLaunchStyle(LaunchStyle s) { treeDoc_->launch_style = static_cast<uint8_t>(s); }
	int64_t fillEventAtTickCount;
	bool overdubsShouldCloneOutput{};

	// START ~ new Automation Clip View Variables
	bool onAutomationClipView; // new to save the view that you are currently in
	                           //(e.g. if you leave clip and want to come back where you left off)

	/// last selected Parameter to be edited in Automation Instrument Clip View
	int32_t lastSelectedParamID;
	deluge::modulation::params::Kind lastSelectedParamKind;
	int32_t lastSelectedParamShortcutX;
	int32_t lastSelectedParamShortcutY;
	int32_t lastSelectedParamArrayPosition;
	OutputType lastSelectedOutputType;
	PatchSource lastSelectedPatchSource;
	// END ~ new Automation Clip View Variables

	virtual bool isEmpty(bool displayPopup = true) = 0;

	virtual bool renderSidebar(uint32_t whichRows = 0, RGB image[][kDisplayWidth + kSideBarWidth] = nullptr,
	                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth] = nullptr) = 0;
	// Setup the name per clip on session/song/grid view and to be selectable on the arranger
	std::string name{};

protected:
	virtual void posReachedEnd(ModelStackWithTimelineCounter* modelStack); // May change the TimelineCounter in the
	                                                                       // modelStack if new Clip got created
	virtual bool
	cloneOutput(ModelStackWithTimelineCounter* modelStack) = 0; // Returns whether a new Output was in fact created
	Error solicitParamManager(Song* song, ParamManager* newParamManager = nullptr,
	                          Clip* favourClipForCloningParamManager = nullptr);
	virtual void pingpongOccurred(ModelStackWithTimelineCounter* modelStack) {}

private:
	/// Stable per-song serial identity; see id(). Minted in the ctor.
	uint32_t id_{0};
};
