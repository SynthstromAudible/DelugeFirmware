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

namespace deluge::storage {

/// @brief Which directory query an acquisition runs.
///
/// Each maps to one existing LoadInstrumentPresetUI reader, whose body is unchanged: the query
/// selects which one the op calls, it does not reimplement them.
enum class PresetQuery : uint8_t {
	AnyUnlaunched, ///< findAnUnlaunchedPresetIncludingWithinSubfolders()
	ConfirmOrNext, ///< confirmPresetOrNextUnlaunchedOne()
	Navigate,      ///< doPresetNavigation()
};

/// @brief Which gesture's follow-up the completion runs.
///
/// The op has no caller waiting on a return value, so it must be told what the acquisition was
/// FOR. Same device as `changingOutputType_`, which tells one Open listing's follow-up from
/// another's in the shared onBrowserOpened() hook.
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
};

/// @brief Everything an acquisition needs, captured at dispatch time.
///
/// @note This is the snapshot. Once dispatched, the op reads ONLY these fields for anything that
///       crosses its yield points -- never live Browser or view state, which the UI task can
///       mutate or free while the op is yielded. `oldInstrument` is safe because it is Song-owned;
///       a FileItem* would not be. See the rung 5 spec, section 6.
struct PresetRequest {
	PresetQuery query{PresetQuery::AnyUnlaunched};
	PresetConsumer consumer{PresetConsumer::ArrangerNewInstrument};
	OutputType outputType{OutputType::SYNTH};
	Availability availability{Availability::ANY};

	int32_t offset{};   ///< Navigate only: scroll direction.
	bool doBlink{};     ///< Navigate and ViewChangeType/SessionRowsChangeType: whether the redrawn name
	                    ///< should blink.
	int32_t yPressed{}; ///< The view row the gesture was made on, captured before any yield.
	int32_t xPressed{}; ///< SessionNewTrack only: the pad column the gesture was made on.
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
	uint32_t targetSection{}; ///< SessionNewTrackClone only: the section the copy is dropped into.

	std::string searchName; ///< ConfirmOrNext only: the preset name to confirm.
	std::string dirPath;    ///< Browser::currentDir at dispatch -- the op sets currentDir from THIS.

	/// @brief The instrument navigated away from, or being replaced. Song-owned, so it stays valid
	///        across the op's yields.
	Instrument* oldInstrument{};
	/// @brief The clip the gesture was made on, if any. Song-owned, so it stays valid across the
	///        op's yields the same way `oldInstrument` does -- but note `isRequestInstrumentStillLive()`
	///        guards only `oldInstrument`; a consumer that reads `clip` without also setting
	///        `oldInstrument` gets no liveness check on the clip itself.
	InstrumentClip* clip{};
	/// @brief SessionNewTrackClone only: the clip being copied. Song-owned, so it survives the op's
	///        yields -- but, like `clip`, it gets no liveness re-check: only `oldInstrument` does.
	Clip* sourceClip{};
};

} // namespace deluge::storage
