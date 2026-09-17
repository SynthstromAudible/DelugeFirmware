/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include <cstdint>
#include <string>

class Clip;
class Instrument;
class InstrumentClip;
class UI;

namespace deluge::storage {

/// @brief Which gesture's follow-up the completion runs.
///
/// The op has no caller waiting on a return value, so it must be told what the acquisition was
/// FOR. Same device as `changingOutputType_`, which tells one Open listing's follow-up from
/// another's in the shared onBrowserOpened() hook.
///
/// This is also the ONLY thing that selects which directory reader the op runs: the follow-up calls
/// its own existing reader (findAnUnlaunchedPresetIncludingWithinSubfolders(),
/// confirmPresetOrNextUnlaunchedOne(), doPresetNavigation() or Browser::getUnusedSlot()) down its own
/// unchanged call chain. There is deliberately no separate "which query" field: one existed, was set
/// at all eleven dispatch sites and read nowhere, and implied the op dispatched on the query rather
/// than on the consumer.
enum class PresetConsumer : uint8_t {
	ArrangerNewInstrument,  ///< the arranger's new-instrument pad
	ArrangerChangeType,     ///< ArrangerView::changeOutputType()
	ArrangerNavigate,       ///< ArrangerView::navigateThroughPresets()
	SessionChangeType,      ///< SessionView's Grid-layout output-type button, inside buttonAction()
	SessionNavigate,        ///< SessionView's Grid-layout preset-navigation gesture
	                        ///< (commandChangeClipPreset()). The Rows-layout arm of the same gesture
	                        ///< delegates to View and uses ViewNavigateClipPreset instead.
	SessionNewTrack,        ///< SessionView::padAction()'s empty-pad "create a clip here" gesture
	                        ///< (Rows layout only -- Grid pads go to gridHandlePads()).
	SessionNewTrackEnter,   ///< SessionView::finishGridNewTrackClip()'s ENTER continuation: the grid
	                        ///< new-track gesture that creates, plays and transitions into the clip.
	SessionNewTrackSelect,  ///< SessionView::finishGridNewTrackClip()'s SELECT continuation: the grid
	                        ///< new-track gesture that creates and selects the clip. Distinct from
	                        ///< SessionNewTrackEnter because the two post-create tails differ.
	SessionNewTrackClone,   ///< SessionView::gridClonePad() copying a clip into an EMPTY track column,
	                        ///< which has to acquire an instrument for the new track.
	ViewChangeType,         ///< View::changeOutputType(), reached via InstrumentClipView::changeOutputType()
	                        ///< -- the KIT/SYNTH buttons on InstrumentClipView, AutomationView and
	                        ///< KeyboardScreen all route through it (handleInstrumentChange()), as do
	                        ///< InstrumentClipView's own MIDI/CV buttons. Runs InstrumentClipView's full
	                        ///< tail: InstrumentClipMinder's LEDs, plus recolour + repaint.
	ViewNavigateClipPreset, ///< View::navigateThroughPresetsForInstrumentClip()
	SessionRowsChangeType,  ///< SessionView's Rows-layout output-type gesture (`:620`) -- distinct from
	                        ///< SessionChangeType, which is the Grid-layout call site at `:626`; the two
	                        ///< reach the directory through different functions with different tails.
	ViewChangeTypeMinder,   ///< View::changeOutputType() reached via InstrumentClipMinder::changeOutputType()
	                        ///< DIRECTLY, bypassing InstrumentClipView -- today only KeyboardScreen's own
	                        ///< MIDI/CV buttons (keyboard_screen.cpp does not override changeOutputType(),
	                        ///< so the unqualified call there resolves to the base). Runs
	                        ///< InstrumentClipMinder's LEDs ONLY: InstrumentClipView was never in this
	                        ///< call chain, so its recolour/repaint never ran for this path either.
	MinderNewInstrument,    ///< InstrumentClipMinder::buttonAction()'s own SYNTH branch (FM/shift),
	                        ///< reached DIRECTLY the same way ViewChangeTypeMinder is -- InstrumentClipView
	                        ///< was never in this call chain. No extra tail: the direct call sites this
	                        ///< replaces discarded createNewInstrument()'s bool too.
	ViewNewInstrument,      ///< InstrumentClipView::createNewInstrument(), reached via
	                        ///< handleInstrumentChange() -- the shift/FM new-instrument buttons on
	                        ///< InstrumentClipView and AutomationView route through it (KeyboardScreen's
	                        ///< own shift/FM buttons use KeyboardNewInstrument instead). Runs
	                        ///< InstrumentClipView's full tail: recolour + repaint, plus
	                        ///< enterDrumCreator() for a new Kit.
	KeyboardNewInstrument,  ///< InstrumentClipView::createNewInstrument(), reached via
	                        ///< handleInstrumentChange() from KeyboardScreen's own shift/FM buttons.
	                        ///< Runs ViewNewInstrument's exact tail, PLUS KeyboardScreen::selectLayout() --
	                        ///< keyboard_screen.cpp used to gate that on createNewInstrument()'s
	                        ///< synchronous return, which this rung's dispatch removed; a shared
	                        ///< ViewNewInstrument arm has no KeyboardScreen instance to call it on, so
	                        ///< this exists to give the layout refresh somewhere correct to run.
	BlankSongPreset,        ///< The song a Deluge boots with, and the one Create New Song makes: the first SYNTHS
	                        ///< preset, asked for by InstrumentLoad::routine() when it sees
	                        ///< Song::blankPresetPending. No gesture and no view -- the follow-up is the song's own
	                        ///< completeBlankPreset().
	KeyboardChangeType,     ///< View::changeOutputType(), reached via InstrumentClipView::changeOutputType()
	                        ///< from KeyboardScreen's own KIT/SYNTH buttons when NEITHER shift nor FM is
	                        ///< held. Runs ViewChangeType's exact tail, PLUS KeyboardScreen::selectLayout()
	                        ///< -- the exact counterpart of KeyboardNewInstrument, and it exists for the
	                        ///< same reason: keyboard_screen.cpp gated that layout refresh on
	                        ///< changeOutputType()'s return, which for Synth/Kit now means "dispatched",
	                        ///< so the refresh ran against the PRE-swap instrument and the keyboard was
	                        ///< left on the wrong layout for good.
};

/// @brief Whether `consumer`'s gesture CREATES something (a clip, a track, an instrument).
///
/// The coalescer is latest-wins, which is right for a gesture that names a destination -- a held
/// navigation encoder or a mashed output-type button only ever wanted where the user settled. It is
/// wrong for a gesture that makes a new thing: superseding the middle of three rapid presses does
/// not converge on anything, it loses a clip. `dispatchPresetAcquisition()` uses this to refuse a
/// gesture rather than overwrite a queued creation.
///
/// Exhaustive with no `default:`, so -Werror=switch makes a future PresetConsumer state which side
/// it is on rather than silently defaulting to "coalesceable".
/// @param consumer The gesture's consumer.
/// @return true if superseding this gesture would lose work the user asked to be created.
constexpr bool isCreationConsumer(PresetConsumer consumer) {
	switch (consumer) {
	case PresetConsumer::ArrangerNewInstrument:
	case PresetConsumer::SessionNewTrack:
	case PresetConsumer::SessionNewTrackEnter:
	case PresetConsumer::SessionNewTrackSelect:
	case PresetConsumer::SessionNewTrackClone:
	case PresetConsumer::MinderNewInstrument:
	case PresetConsumer::ViewNewInstrument:
	case PresetConsumer::KeyboardNewInstrument:
		return true;
	case PresetConsumer::ArrangerChangeType:
	case PresetConsumer::ArrangerNavigate:
	case PresetConsumer::SessionChangeType:
	case PresetConsumer::SessionNavigate:
	case PresetConsumer::ViewChangeType:
	case PresetConsumer::ViewNavigateClipPreset:
	case PresetConsumer::SessionRowsChangeType:
	case PresetConsumer::ViewChangeTypeMinder:
	case PresetConsumer::KeyboardChangeType:
	case PresetConsumer::BlankSongPreset:
		// Not a creation: the clip and its default synth exist already, and a gesture the user makes meanwhile is
		// welcome to supersede this -- completeBlankPreset() would refuse the swap anyway.
		return false;
	}
	return false;
}

/// @brief Everything an acquisition needs, captured at dispatch time.
///
/// @note This is the snapshot. Once dispatched, the op reads ONLY these fields for anything that
///       crosses its yield points -- never live Browser or view state, which the UI task can
///       mutate or free while the op is yielded. `oldInstrument` is safe because it is Song-owned;
///       a FileItem* would not be. See the rung 5 spec, section 6.
struct PresetRequest {
	PresetConsumer consumer{PresetConsumer::ArrangerNewInstrument};
	OutputType outputType{OutputType::SYNTH};
	Availability availability{Availability::ANY};

	int32_t offset{};   ///< Navigate only: scroll direction.
	bool doBlink{};     ///< Navigate and ViewChangeType/SessionRowsChangeType: whether the redrawn name
	                    ///< should blink.
	int32_t yPressed{}; ///< The view row the gesture was made on, captured before any yield.
	int32_t xPressed{}; ///< SessionNewTrack only: the pad column the gesture was made on.
	/// @brief ArrangerNewInstrument only: the audition-pad row the press physically landed on, BEFORE
	///        `yPressed`'s clamp into the visible output range.
	/// @note Carried separately from `yPressed` because it is what the completion asks the pad matrix
	///       about (`matrixDriver.isPadPressed(kDisplayWidth + 1, ...)`) to decide whether the finger
	///       is still down. The clamped row is a screen row, not a pad.
	int32_t yPressedRaw{};
	/// @brief SessionNewTrack only: `yDisplay + songViewYScroll` as it read when the pad went down.
	/// @note Snapshotted rather than re-derived, because the completion's own insertAndResyncNewClip()
	///       can advance songViewYScroll before the value is used.
	int32_t clipIndex{};
	/// @brief SessionNewTrackEnter/Select only: the grid press coordinates, packed `y << 16 | x`.
	/// @note Already the shape the create-and-place half takes, and already carried across the
	///       clip-type picker's wait as `pendingGridClipXY`.
	uint32_t packedXY{};
	/// @brief SessionNewTrackEnter/Select only: whether the pad that started the gesture was STILL
	///        pressed when the gesture dispatched.
	/// @note Snapshotted, never re-read: the dispatching caller runs clipPressEnded() ->
	///       gridResetPresses() the instant it returns, so a completion comparing `packedXY` against
	///       the live gridFirstPressedX/Y would always conclude "released" and take a different branch
	///       than the synchronous path did.
	bool padStillPressed{};
	/// @brief SessionNewTrack only: whether SHIFT was held when the pad went down.
	/// @note Snapshotted like every other input. The completion uses it for `clipWasSelectedWithShift`
	///       (a colour choice); reading Buttons::isShiftButtonPressed() live from there was faithful to
	///       the old ordering only while the gap was zero, and the gap is now a storage round-trip.
	bool shiftHeld{};
	uint32_t targetSection{}; ///< SessionNewTrackClone only: the section the copy is dropped into.
	/// @brief MinderNewInstrument/ViewNewInstrument/KeyboardNewInstrument only: whether the new Synth
	///        should be FM (DX7).
	bool isDx{};

	/// @brief Browser::currentDir at dispatch -- the op sets currentDir from THIS, unconditionally.
	/// @note Most readers immediately overwrite it as they descend (doPresetNavigation() from
	///       oldInstrument->dirPath, createNewInstrument() from getInstrumentFolder()), and that is
	///       their business. This is the FLOOR: without it the op would inherit whatever the UI task
	///       last happened to leave in that static, which is the one thing between the two tiers here.
	std::string dirPath;

	/// @brief The instrument navigated away from, or being replaced. Song-owned, so it stays valid
	///        across the op's yields.
	Instrument* oldInstrument{};
	/// @brief The clip the gesture was made on, if any. Song-owned, so it stays valid across the
	///        op's yields the same way `oldInstrument` does -- but note `isRequestInstrumentStillLive()`
	///        guards only `oldInstrument`; a consumer that reads `clip` without also setting
	///        `oldInstrument` gets no liveness check on the clip itself.
	InstrumentClip* clip{};
	/// @brief SessionNewTrackClone only: the clip being copied. Song-owned, so it survives the op's
	///        yields, and its liveness IS re-checked: that consumer sets `oldInstrument` to
	///        `sourceClip->output` for exactly that reason, so a clip deleted mid-flight takes its
	///        Instrument out of the Song's output list with it and the completion is skipped.
	Clip* sourceClip{};

	/// @brief ArrangerNewInstrument only: the UI whose rows the completion repaints.
	/// @note The arranger's audition pads are driven from three different UIs (ArrangerView itself,
	///       AutomationView and PerformanceView), and uiNeedsRendering() only marks rows for a UI that
	///       is actually in the navigation hierarchy -- passing the wrong one silently repaints
	///       nothing. Every UI is a statically allocated singleton, so this pointer is at least as
	///       stable across the op's yields as a Song-owned one.
	UI* renderUi{};
};

/// @brief Which of today's walkers an acquisition's walk runs, on the storage owner.
enum class AcquireWalk : uint8_t {
	Unlaunched, ///< findAnUnlaunchedPresetIncludingWithinSubfolders(): the first preset nobody in the song uses.
	Confirm,    ///< confirmPresetOrNextUnlaunchedOne(): a named preset if it may be used, or the next one that may.
	Navigate,   ///< doPresetNavigation(): the next preset along from a named one, either way.
	UnusedSlot, ///< Browser::getUnusedSlot(): a name no preset of the type has. Nothing is loaded.
};

/// @brief The walk an acquisition asks for, fixed by its gesture's prologue before the walk is queued.
struct AcquireWalkSpec {
	AcquireWalk walk{AcquireWalk::Unlaunched};
	OutputType outputType{OutputType::SYNTH}; ///< The type walked for, and parsed.
	Availability availability{Availability::ANY};
	/// Unlaunched: whether the song's own Instruments count as using a preset. False for the blank song, whose default
	/// synth is named "0" and would otherwise be the first thing the walk landed on.
	bool includeSong{true};
	std::string dirPath; ///< The folder the walk starts in.
	std::string name;    ///< Confirm: the preset asked for. Navigate: the preset moved on from, without extension.
	int32_t offset{};    ///< Navigate: which way to move.
	/// Navigate: the Instrument moved on from. The walk compares it and never reads through it.
	Instrument* oldInstrument{};
	bool blink{}; ///< Navigate: whether the name shown when the walk lands blinks.
};

/// @brief What an acquisition hands its gesture's follow-up.
struct AcquireResult {
	/// Why there is no Instrument; NO_ERROR_BUT_GET_OUT when a navigate found nowhere else to go.
	Error error{Error::NONE};
	/// The Instrument: parsed from the card and not yet in the song (@ref loadedFromFile), off the hibernation list
	/// (@ref wasHibernating), or already active in the song (@ref alreadyInSong). The follow-up adds it, or deletes one
	/// it will not use that was loaded from the card.
	Instrument* instrument{};
	bool alreadyInSong{};
	bool wasHibernating{};
	bool loadedFromFile{};
	std::string name; ///< UnusedSlot: the slot name found; empty when there is none.
};

} // namespace deluge::storage
