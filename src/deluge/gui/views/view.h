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
#include "gui/colour/colour.h"
#include "hid/button.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "storage/preset_acquisition.h"
#include <cstdint>

class InstrumentClip;
class NoteRow;
class UI;
class ModControllable;
class ParamManagerForTimeline;
class RootUI;
class ParamManagerForTimeline;
class MelodicInstrument;
class Drum;
class Instrument;
class TimelineCounter;
class Clip;
class Output;
class AudioClip;
class ModelStackWithThreeMainThings;
class ParamManagerForTimeline;
class LearnedMIDI;
class Kit;
class Song;

// A view is where the user can interact with the pads - song view, Clip view, automation view and keyboard view.
// (Is that still a good description? This class is a bit of a mishmash of poorly organised code, sorry.)

/// Assorted utilities for manging the user interface.
class View {
public:
	View();
	void focusRegained();
	ActionResult buttonAction(deluge::hid::Button b, bool on);
	void setTimeBaseScaleLedState();
	void setLedStates();

	void clipStatusMidiLearnPadPressed(bool on, Clip* whichLoopable);
	void noteRowMuteMidiLearnPadPressed(bool on, NoteRow* whichNoteRow);
	void endMidiLearnPressSession(MidiLearn newThingPressed = MidiLearn::NONE);
	void noteOnReceivedForMidiLearn(MIDICable& cable, int32_t channel, int32_t note, int32_t velocity);
	void pcReceivedForMIDILearn(MIDICable& cable, int32_t channel, int32_t pc) {}
	void ccReceivedForMIDILearn(MIDICable& cable, int32_t channel, int32_t cc, int32_t value);
	void drumMidiLearnPadPressed(bool on, Drum* drum, Kit* kit);
	void instrumentMidiLearnPadPressed(bool on, Instrument* instrument);
	void sectionMidiLearnPadPressed(bool on, uint8_t section);
	void midiLearnFlash();
	void setModLedStates();
	int32_t getModKnobMode();
	void modEncoderAction(int32_t whichModEncoder, int32_t offset);
	void modEncoderButtonAction(uint8_t whichModEncoder, bool on);
	void modButtonAction(uint8_t whichButton, bool on);
	void setKnobIndicatorLevels();
	void setKnobIndicatorLevel(uint8_t whichModEncoder);
	int32_t convertPatchCableKnobPosToIndicatorLevel(int32_t knobPos);
	void setActiveModControllableTimelineCounter(TimelineCounter* playPositionCounter,
	                                             bool shouldSendMidiFeedback = true);
	void setActiveModControllableWithoutTimelineCounter(ModControllable* modControllable, ParamManager* paramManager);
	/// @brief [interaction] Leave the mod encoders on nothing until a UI gives them something again.
	///
	/// For when what the active mod controllable belongs to is about to go -- a song swapped out or deleted, a clip
	/// freed -- since the knobs reach the view whichever UI is on top. Writes only the model stack; the knob
	/// indicators and mod LEDs are left to the caller.
	void releaseActiveModControllable();
	/// @brief [interaction] Whether the active model stack names @p song or anything it owns: the song itself, one of
	///        its clips, or one of its outputs (main or hibernating) as the ModControllable.
	///
	/// Walks only what the song owns -- its clip arrays and output lists -- and compares; it follows none of the song's
	/// per-clip reference fields (currentClip, syncScalingClip, beingRecordedFromClip), which may name clips already
	/// freed. @p song must be alive.
	bool activeModControllableNamesSong(Song* song) const;
	/// @brief [render] or [interaction] A song swap is taking @p outgoing out of play: let go of the active model stack
	///        if it names @p outgoing or anything it owns.
	///
	/// On the interaction tier the stack is released at once, whatever it names. On the render it is only asked for
	/// (postedStackRequests()), and applied by the interaction tier before its next job: an interaction handler the
	/// render preempted keeps a stack whose parts all still name the outgoing song, which stays alive until the
	/// interaction tier hands it to AudioEngine::retire(Song*).
	void songSwappedOut(Song* outgoing);
	/// @brief [render] or [interaction] The recording of the arrangement-only clip @p from has moved on to its clone
	///        @p to: point the active model stack at @p to, if it still names @p from -- at once on the interaction
	///        tier, by request (as songSwappedOut()) on the render.
	void recordingMovedToClip(Clip* from, Clip* to);
	/// @brief [interaction] Apply, in order, every change to the active model stack the render has asked for since the
	///        last call.
	///
	/// Called before every app-loop job (deluge_app_service()) and by AudioEngine::retire() before it hands anything
	/// over, so no request is left naming an object once the interaction tier has let go of it.
	void applyPostedStackRequests();
	void cycleThroughReverbPresets();
	char const* getReverbPresetDisplayName(int32_t preset);
	int32_t getCurrentReverbPreset();
	void setModRegion(uint32_t pos = 0xFFFFFFFF, uint32_t length = 0, int32_t noteRowId = 0);
	void notifyParamAutomationOccurred(ParamManager* paramManager, bool updateModLevels = true);
	void displayAutomation();
	void displayOutputName(Output* output, bool doBlink = true, Clip* clip = nullptr);
	void instrumentChanged(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument);
	/// @brief Move `modelStack`'s clip's instrument to the next/previous preset in its folder.
	/// @param offset Scroll direction/amount to move through the folder's presets.
	/// @param modelStack The caller's stack-local model stack; NOT carried past this call -- the
	///        Synth/Kit arm rebuilds its own from `clip` once the acquisition lands.
	/// @param doBlink Whether the redrawn output name should blink.
	void navigateThroughPresetsForInstrumentClip(int32_t offset, ModelStackWithTimelineCounter* modelStack,
	                                             bool doBlink = false);
	/// @brief navigateThroughPresetsForInstrumentClip()'s follow-up: from InstrumentLoad once the
	///        acquisition has landed for Synth/Kit, inline for MIDI/CV.
	/// @param offset Scroll direction/amount, carried from the dispatching call.
	/// @param clip The clip the gesture was made on (Song-owned; safe while the acquisition is out).
	/// @param doBlink Whether the redrawn output name should blink.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI/CV.
	void completeNavigateThroughPresetsForInstrumentClip(int32_t offset, InstrumentClip* clip, bool doBlink,
	                                                     const deluge::storage::AcquireResult* acquired = nullptr);
	void navigateThroughAudioOutputsForAudioClip(int32_t offset, AudioClip* clip, bool doBlink = false);
	/// @brief Change `modelStack`'s clip to a new output type: through an InstrumentLoad acquisition
	///        for Synth/Kit, inline for MIDI/CV.
	/// @param newOutputType The type to switch to.
	/// @param modelStack The caller's stack-local model stack; NOT carried past this call -- see
	///        navigateThroughPresetsForInstrumentClip()'s doc for why.
	/// @param doBlink Whether the redrawn output name should blink.
	/// @param consumer Which caller's completion the acquisition (once it runs) hands off to; see
	///        `PresetConsumer` -- `InstrumentClipMinder`/`SessionView` pass their own value so their
	///        redraw/LED tail runs from that completion instead of firing here before the swap has
	///        actually happened.
	/// @return `true` once the gesture is accepted -- for Synth/Kit this means DISPATCHED, not yet
	///         complete; a caller that needs to know whether the swap actually succeeded has to wait
	///         for its completion to run, exactly as the arranger's own changeOutputType() works.
	bool changeOutputType(OutputType newOutputType, ModelStackWithTimelineCounter* modelStack, bool doBlink = false,
	                      deluge::storage::PresetConsumer consumer = deluge::storage::PresetConsumer::ViewChangeType);
	/// @brief changeOutputType()'s follow-up: from InstrumentLoad for Synth/Kit, inline for MIDI/CV.
	///        Does the core swap and the View-level redraw only -- no caller-specific tail. Callers
	///        that need one route through completeChangeOutputTypeFor() instead, which this backs.
	/// @param newOutputType The type that was switched to.
	/// @param clip The clip the gesture was made on (Song-owned; safe while the acquisition is out).
	/// @param doBlink Whether the redrawn output name should blink.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI/CV.
	/// @return Whether the swap actually succeeded (the real outcome, not a dispatch acknowledgement).
	bool completeChangeOutputType(OutputType newOutputType, InstrumentClip* clip, bool doBlink,
	                              const deluge::storage::AcquireResult* acquired = nullptr);
	/// @brief Routes a completed (or synchronously-run) changeOutputType() to the completion its
	///        `consumer` asked for, so the caller-specific redraw/LED tail runs from exactly ONE place
	///        regardless of whether the acquisition was dispatched (Synth/Kit) or ran inline (MIDI/CV)
	///        -- never both, and never before the swap in completeChangeOutputType() above has run.
	/// @param consumer Which caller dispatched the original changeOutputType() call.
	/// @param newOutputType The type that was switched to.
	/// @param clip The clip the gesture was made on (Song-owned; safe while the acquisition is out).
	/// @param doBlink Whether the redrawn output name should blink.
	/// @param acquired Synth/Kit: what InstrumentLoad acquired. nullptr for MIDI/CV.
	/// @return Whether the swap actually succeeded.
	bool completeChangeOutputTypeFor(deluge::storage::PresetConsumer consumer, OutputType newOutputType,
	                                 InstrumentClip* clip, bool doBlink,
	                                 const deluge::storage::AcquireResult* acquired = nullptr);
	void drawOutputNameFromDetails(OutputType outputType, int32_t slot, int32_t subSlot, char const* name,
	                               bool isNameEmpty, bool editedByUser, bool doBlink, Clip* clip = nullptr);
	void startMIDILearn();
	void endMIDILearn();
	[[nodiscard]] RGB getClipMuteSquareColour(Clip* clip, RGB thisColour, bool allowMIDIFlash = true);
	ActionResult clipStatusPadAction(Clip* clip, bool on, int32_t yDisplayIfInSessionView = -1);
	void flashPlayEnable();
	void flashPlayDisable();
	void flashPlayRoutine();

	void activateMacro(uint32_t y);
	Clip* findNextClipForOutput(Output* output);
	bool renderMacros(int32_t column, uint32_t y, int32_t selectedMacro, RGB image[][kDisplayWidth + kSideBarWidth],
	                  uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

	// MIDI learn stuff
	MidiLearn thingPressedForMidiLearn = MidiLearn::NONE;
	bool deleteMidiCommandOnRelease;
	bool midiLearnFlashOn;
	bool shouldSaveSettingsAfterMidiLearn{};

	LearnedMIDI* learnedThing;
	Instrument* instrumentPressedForMIDILearn{};
	Drum* drumPressedForMIDILearn{};

	// deceivingly this is used only while learning drums
	Kit* kitPressedForMIDILearn{};

	/// What the mod encoders, mod buttons and MIDI follow act on. [interaction] Written only by the interaction tier,
	/// whose handlers read it across many instructions; the render reads only its pointers' values, to compare, and
	/// asks for any change through songSwappedOut() or recordingMovedToClip().
	ModelStackWithThreeMainThings activeModControllableModelStack{};
	uint8_t dummy[MODEL_STACK_MAX_SIZE - sizeof(ModelStackWithThreeMainThings)]{};

	bool pendingParamAutomationUpdatesModLevels{};

	bool clipArmFlashOn;
	bool blinkOn{};

	uint32_t timeSaveButtonPressed{};

	int32_t modNoteRowId{};
	uint32_t modPos;
	// 0 if not currently editing a region / step / holding a note. If you're gonna refer to this, you absolutely have
	// to first check that the TimelineCounter you're thinking of setting some automation on
	// == activeModControllableTimelineCounter
	uint32_t modLength;

	int32_t calculateKnobPosForDisplay(deluge::modulation::params::Kind kind, int32_t paramID, int32_t knobPos);
	void displayModEncoderValuePopup(deluge::modulation::params::Kind kind, int32_t paramID, int32_t newKnobPos,
	                                 PatchSource source1 = PatchSource::NONE, PatchSource source2 = PatchSource::NONE);
	void flushPendingModEncoderValuePopup();
	void potentiallyMakeItHarderToTurnKnob(int32_t whichModEncoder, ModelStackWithAutoParam* modelStackWithParam,
	                                       int32_t newKnobPos);
	void sendMidiFollowFeedback(ModelStackWithAutoParam* modelStackWithParam = nullptr, int32_t knobPos = kNoSelection,
	                            bool isAutomation = false);

	// vu meter rendering
	bool displayVUMeter;
	bool potentiallyRenderVUMeter(RGB image[][kDisplayWidth + kSideBarWidth]);

	void getParameterNameFromModEncoder(int32_t whichModEncoder, char* parameterName);

	bool isClipContext();

	int32_t calculateKnobPosForModEncoderTurn(deluge::modulation::params::Kind kind, int32_t knobPos, int32_t offset);

#if defined(DELUGE_HOST)
	/// @brief Host only: start or stop counting the render blocks that change activeModControllableModelStack, and
	///        reset the count. Off by default, so a threaded host render never reads the stack for it.
	static void watchRenderStackWrites(bool on);
	/// @brief Host only: how many render blocks changed activeModControllableModelStack while watched.
	static int32_t renderStackWrites();
	/// @brief Host only: put around a render block; counts it when the stack differs at its end from its start.
	class RenderBlockStackWatch {
	public:
		RenderBlockStackWatch();
		~RenderBlockStackWatch();
		RenderBlockStackWatch(const RenderBlockStackWatch&) = delete;
		RenderBlockStackWatch& operator=(const RenderBlockStackWatch&) = delete;

	private:
		bool watching_;
		void const* before_[4]{};
	};
#endif

private:
	void pretendModKnobsUntouchedForAWhile();
	void instrumentBeenEdited();
	void clearMelodicInstrumentMonoExpressionIfPossible();

	// vu meter rendering
	int32_t getMaxYDisplayForVUMeter(float level);
	int32_t cachedMaxYDisplayForVUMeterL;
	int32_t cachedMaxYDisplayForVUMeterR;
	void renderVUMeter(int32_t maxYDisplay, int32_t xDisplay, RGB thisImage[][kDisplayWidth + kSideBarWidth]);
	bool renderedVUMeter;

	// mod encoder action
	void modEncoderAction_nonExistentParam(int32_t whichModEncoder, int32_t offset,
	                                       ModelStackWithAutoParam* modelStackWithParam);
	void modEncoderAction_existentParam(int32_t whichModEncoder, int32_t offset,
	                                    ModelStackWithAutoParam* modelStackWithParam, bool noteTailsAllowedBefore);
	ModelStackWithAutoParam* getModelStackWithParam(int32_t whichModEncoder, bool& noteTailsAllowedBefore);

	// mod encoder button action
	void modEncoderButtonAction_deleteAutomation(uint8_t whichModEncoder);
	void modEncoderButtonAction_changeModControllable(uint8_t whichModEncoder, bool on);

	// Timing constants for display arbitration in displayModEncoderValuePopup (in AudioEngine sample units)
	static constexpr uint32_t MIN_DISPLAY_OWNERSHIP_TIME = kSampleRate; // 1 second (minimum juggling time)
	static constexpr uint32_t DISPLAY_TIMEOUT = kSampleRate / 4;        // 0.25 seconds (ball drop timeout)
	static constexpr uint32_t MIN_UPDATE_INTERVAL = kSampleRate / 22;   // ~45ms (minimum perceptible update frequency)

	bool hasPendingModEncoderValuePopup = false;
	deluge::modulation::params::Kind pendingPopupKind = deluge::modulation::params::Kind::NONE;
	int32_t pendingPopupParamID = -1;
	int32_t pendingPopupKnobPos = 0;
	PatchSource pendingPopupSource1 = PatchSource::NONE;
	PatchSource pendingPopupSource2 = PatchSource::NONE;
};

extern View view;
