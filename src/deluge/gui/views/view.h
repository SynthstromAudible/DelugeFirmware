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
#include "deluge-project/knob_focus.h"
#include "gui/colour/colour.h"
#include "hid/button.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "processing/audio_output.h"
#include "storage/preset_acquisition.h"
#include <atomic>
#include <cstdint>
#include <optional>

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
	void commitSectionMidiLearn();
	/// @brief Make the held thing's command @p learned: a song binding (a section's, a clip's or a kit row's mute, a
	/// track's or a kit row's input) through the song's mapping table, a global command in place.
	/// @param learned       The command, or a cleared one to unlearn it.
	/// @param inputFromNote A track's or kit row's input learned from a note (or a CC played as one): it takes the
	///                      cable's bend ranges and velocity-to-level default.
	void commitLearnedThing(const LearnedMIDI& learned, bool inputFromNote = false);
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
	/// For when what the knobs are on is about to go -- a song swapped out or deleted, a clip freed -- since the knobs
	/// reach the view whichever UI is on top. Writes only the focus; the knob indicators and mod LEDs are left to the
	/// caller.
	void releaseActiveModControllable();
	/// @brief [interaction] The engine stack of the owner the knobs move, resolved now from the focus (following the
	///        take the arrangement records, without making one). Valid until the song's structure next changes.
	/// @param memory MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
	/// @return The stack, or null for no focus.
	[[nodiscard]] ModelStackWithThreeMainThings* knobOwnerStack(void* memory) const;
	/// @brief [interaction] The knobs' owner stack, resolved now in a buffer of its own: for a gesture that needs the
	///        engine objects the knobs name.
	struct KnobStack {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack;
		explicit KnobStack(const View& view) : stack(view.knobOwnerStack(memory)) {}
		KnobStack(const KnobStack&) = delete;
		KnobStack& operator=(const KnobStack&) = delete;
		/// @brief The owner the knobs move, or null.
		[[nodiscard]] ModControllable* owner() const { return stack != nullptr ? stack->modControllable : nullptr; }
		/// @brief The owner's param manager, or null.
		[[nodiscard]] ParamManager* params() const { return stack != nullptr ? stack->paramManager : nullptr; }
		/// @brief The clip (its take while the arrangement records it) or song the owner is in, or null.
		[[nodiscard]] TimelineCounter* counter() const {
			return stack != nullptr ? stack->getTimelineCounterAllowNull() : nullptr;
		}
	};
	/// @brief [interaction] What the gold knobs point at, as addresses: the owner the views set, the region a held pad
	///        names, the song's compressor view and its VU meter.
	[[nodiscard]] const deluge::project::KnobFocus& knobFocus() const { return knobFocus_; }
	/// @copydoc knobFocus() const
	[[nodiscard]] deluge::project::KnobFocus& knobFocus() { return knobFocus_; }
	/// @brief [interaction] The clip the knobs are on, by id: none for the song, an arranger row with no clip, or no
	///        focus. A take the arrangement records is named by the session clip it records.
	[[nodiscard]] std::optional<uint32_t> knobsClipId() const;
	/// @brief [interaction] Are the knobs on @p clip of @p song? Compares ids and the song's identity; dereferences
	///        nothing the knobs name.
	[[nodiscard]] bool knobsOnClip(const Song& song, const Clip& clip) const;
	/// @brief [interaction] Are the knobs on the kit row @p drum of @p song?
	[[nodiscard]] bool knobsOnDrum(const Song& song, const Drum& drum) const;
	/// @brief [interaction] Are the knobs on something of a clip (a track's knobs, a CV track's included), not the
	///        song's or nothing?
	[[nodiscard]] bool knobsOnClipContext() const { return knobsOnClipContext_; }
	/// @brief [interaction] Does a held pad's step-edit region hold on @p counter (and, given one, on the row @p
	///        noteRowId)? Only a clip of the current song the knobs are on holds one.
	[[nodiscard]] bool stepEditRegionHolds(const TimelineCounter& counter,
	                                       std::optional<int32_t> noteRowId = std::nullopt) const;
	/// @brief [interaction] The held region's start, or 0xFFFFFFFF for none (an AutoParam's "current value" pos).
	[[nodiscard]] uint32_t modPos() const { return knobFocus_.region.pos; }
	/// @brief [interaction] The held region's length; 0 for none.
	[[nodiscard]] uint32_t modLength() const { return knobFocus_.region.length; }
	/// @brief [interaction] The held note's row id.
	[[nodiscard]] int32_t modNoteRowId() const { return knobFocus_.region.note_row_id; }
	/// @brief [interaction] The address of the param knob @p whichModEncoder moves (0 the lower, 1 the upper), in the
	///        clip the focus names: one the owner holds, or a cable a turn would make.
	/// @return The address, or none for a knob with no param or no focus.
	[[nodiscard]] std::optional<deluge::project::ParamAddress> knobAddress(int32_t whichModEncoder) const;
	/// @brief [interaction] Does the UI mode now let a mod button change the knobs' mode? None does, and those a
	///        held pad or row or a song load holds.
	[[nodiscard]] bool modButtonUIModeInRange() const;
	/// @brief [interaction] The mod button the knobs follow, from the records: the song's, a track's, a kit row's
	///        kit's.
	/// @return The mode, or -1 for no focus (a CV track's included, which has no modes).
	[[nodiscard]] int32_t knobMode() const;
	/// @brief [interaction] End the stutter of the owner the knobs are on.
	void endKnobsStutter();
	/// @brief [interaction] Let go of the knobs if they are on @p clip, which is about to go.
	void releaseKnobsIfOn(const Clip& clip);
	/// @brief [interaction] Let go of the knobs if they are on @p song or anything it owns, which is about to go.
	void releaseKnobsIfOnSong(const Song& song);
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
	/// While a section's pad is held for MIDI learn: the section, and its launch command as learn edits it (the
	/// command itself is model data; commitSectionMidiLearn() writes the edit back).
	uint8_t sectionPressedForMidiLearn{};
	LearnedMIDI sectionLearnScratch;
	/// Whether MIDI learn is editing @p section's launch command.
	[[nodiscard]] bool isLearningSection(int32_t section) const {
		return thingPressedForMidiLearn == MidiLearn::SECTION && learnedThing == &sectionLearnScratch
		       && sectionPressedForMidiLearn == section;
	}
	Instrument* instrumentPressedForMIDILearn{};
	/// The kit row whose input or mute is held for learn.
	Drum* drumPressedForMIDILearn{};
	/// The clip whose mute is held for learn.
	Clip* clipPressedForMIDILearn{};

	// deceivingly this is used only while learning drums
	Kit* kitPressedForMIDILearn{};

	bool pendingParamAutomationUpdatesModLevels{};

	bool clipArmFlashOn;
	bool blinkOn{};

	uint32_t timeSaveButtonPressed{};

	int32_t calculateKnobPosForDisplay(deluge::modulation::params::Kind kind, int32_t paramID, int32_t knobPos);
	void displayModEncoderValuePopup(deluge::modulation::params::Kind kind, int32_t paramID, int32_t newKnobPos,
	                                 PatchSource source1 = PatchSource::NONE, PatchSource source2 = PatchSource::NONE);
	void flushPendingModEncoderValuePopup();
	void potentiallyMakeItHarderToTurnKnob(int32_t whichModEncoder, ModelStackWithAutoParam* modelStackWithParam,
	                                       int32_t newKnobPos);
	void sendMidiFollowFeedback(ModelStackWithAutoParam* modelStackWithParam = nullptr, int32_t knobPos = kNoSelection,
	                            bool isAutomation = false);

	// vu meter rendering
	bool potentiallyRenderVUMeter(RGB image[][kDisplayWidth + kSideBarWidth]);

	void getParameterNameFromModEncoder(int32_t whichModEncoder, char* parameterName);

	int32_t calculateKnobPosForModEncoderTurn(deluge::modulation::params::Kind kind, int32_t knobPos, int32_t offset);

#if defined(DELUGE_HOST)
	/// @brief Host only: start or stop counting the render blocks that change the knobs' render-visible words, and
	///        reset the count. Off by default, so a threaded host render never reads them for it.
	static void watchRenderStackWrites(bool on);
	/// @brief Host only: how many render blocks changed the knobs' render-visible words while watched.
	static int32_t renderStackWrites();
	/// @brief Host only: put around a render block; counts it when the knobs' words differ at its end from its start.
	///        Reads them on the render: a probe for the single-executor golden_vt_render, not for use under the TSan
	///        lens.
	class RenderBlockStackWatch {
	public:
		RenderBlockStackWatch();
		~RenderBlockStackWatch();
		RenderBlockStackWatch(const RenderBlockStackWatch&) = delete;
		RenderBlockStackWatch& operator=(const RenderBlockStackWatch&) = delete;

	private:
		bool watching_;
		void const* before_[2]{};
	};
#endif

private:
	/// @brief Set the focus's owner from @p stack, a view's stack for its target, keeping its region, compressor view
	///        and VU meter.
	void setKnobFocusFrom(const ModelStackWithThreeMainThings* stack);
	/// @brief Take the knobs' param manager, which the render compares automation against, from the focus now.
	void refreshKnobParamManager();
	/// @brief The address of @p param, a param of the knobs' owner, with the clip the focus names.
	[[nodiscard]] std::optional<deluge::project::ParamAddress>
	knobParamAddress(const ModelStackWithAutoParam& param) const;

	deluge::project::KnobFocus knobFocus_{};
	/// The song the focus's ids are in, as an identity only: never dereferenced.
	const Song* knobSong_{};
	/// The song the compressor view was opened in, as an identity only: a new song starts out of it.
	const Song* compSong_{};
	bool knobsOnClipContext_{};
	/// The param manager of the owner the knobs move, as an identity: the render compares an automation's against it.
	std::atomic<const ParamManager*> knobParamManager_{};

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
	void modEncoderAction_nonExistentParam(ModControllable& owner, int32_t whichModEncoder, int32_t offset,
	                                       ModelStackWithAutoParam* modelStackWithParam);
	void modEncoderAction_existentParam(int32_t whichModEncoder, int32_t offset,
	                                    ModelStackWithAutoParam* modelStackWithParam, bool noteTailsAllowedBefore);
	/// @brief Does @p modelStackWithParam name a param a knob turns: one the owner holds, or a cable it makes?
	static bool isParamOfKnob(const ModelStackWithAutoParam* modelStackWithParam);

	// mod encoder button action
	void modEncoderButtonAction_deleteAutomation(uint8_t whichModEncoder, ModelStackWithThreeMainThings* stack);
	void modEncoderButtonAction_changeModControllable(uint8_t whichModEncoder, bool on,
	                                                  ModelStackWithThreeMainThings* stack);

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
