#pragma once

// clang-format off
// The table below is a markdown table: its rows are longer than the column limit and reflowing them
// merges cells into each other (see the mangled table in `marker/marker_scene.h`, which was
// reflowed). Format is re-enabled immediately after the comment block.
/// @file
/// @brief `TimelineState` -- the horizontal scroll, the horizontal zoom and the tick square that
///        every clip view shares, lifted out of legacy's three-class `RootUI` chain.
///
/// Legacy's clip views inherit `TimelineView` (`gui/views/timeline_view.{h,cpp}`) ->
/// `ClipNavigationTimelineView` (`clip_navigation_timeline_view.{h,cpp}`) -> `ClipView`
/// (`clip_view.{h,cpp}`). Those three classes hold **three different kinds of thing** and only two
/// of them move here.
///
/// @par What moved, what did not, and why
///
/// | legacy symbol | destination | why |
/// |---|---|---|
/// | `xScroll[navSys]`, `xZoom[navSys]` (`PersistentViewState` in `model/song/song_view_state.h`, reached as `Song::persistentView`) | here (`x_scroll`, `x_zoom`) | the state itself |
/// | `TimelineView::getPosFromSquare` (`timeline_view.cpp`) | here | pure arithmetic over scroll/zoom/triplets |
/// | `TimelineView::getSquareFromPos` | here | ditto |
/// | `TimelineView::getSquareEndFromPos` | here | ditto |
/// | `TimelineView::isSquareDefined` | here | ditto |
/// | `TimelineView::inTripletsView` | here | reads `supportsTriplets()` (a scene fact, so a field) and `tripletsOn` |
/// | `ClipView::getLengthChopAmount` (`clip_view.cpp`) | here | pure arithmetic over zoom and triplets |
/// | `ClipView::getLengthExtendAmount` (`clip_view.cpp`) | here | ditto |
/// | `TimelineView::horizontalEncoderAction`'s zoom arm (`timeline_view.cpp`) | **splits** | the arithmetic and the zoom-delay latch are here; the UI-mode gates and the popup are the scene's |
/// | `TimelineView::horizontalEncoderAction`'s scroll arm (`timeline_view.cpp`) | **splits** | same split |
/// | `TimelineView::initiateXScroll` | **splits** | `xScroll = newXScroll` is here; `setupScroll` + `PadLEDs::horizontal::setupScroll` + `UI_MODE_HORIZONTAL_SCROLL` are the dropped animation |
/// | `TimelineView::initiateXZoom` | **splits** | `xScroll = newScroll` is here; the rest is the dropped animation |
/// | `TimelineView::zoomToMax` | here | arithmetic plus `initiateXZoom`'s state half |
/// | `TimelineView::scrollRightToEndOfLengthIfNecessary` | here | arithmetic |
/// | `TimelineView::scrollLeftIfTooFarRight` | here | arithmetic |
/// | `TimelineView::tripletsButtonPressed` | **splits** | `tripletsOn`/`tripletsLevel` are here; `uiNeedsRendering` and the TRIPLETS LED are the scene's |
/// | `TimelineView::displayNumberOfBarsAndBeats` | **splits** | the bar/beat/sub-beat decomposition is here (`bars_and_beats`); the popup is the scene's |
/// | `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording` (`clip_navigation_timeline_view.h`) | here (`scroll_before_following`) | state |
/// | `ClipNavigationTimelineView::focusRegained` | here (`focus_regained`) | it does nothing but reset that field |
/// | `ClipNavigationTimelineView::horizontalEncoderAction`'s first statement (`clip_navigation_timeline_view.cpp`) | here (`stop_following`, called by both gestures) | it clears that same field on every horizontal turn, so a manual scroll or zoom during an auto-extending linear recording forgets the restore point. See the ordering note on `stop_following` |
/// | `ClipNavigationTimelineView::horizontalScrollForLinearRecording` | **splits** | the length guard and the scroll write are here; the animate-or-not choice, `uiNeedsRendering` and the popup are the scene's |
/// | `ClipView::getTickSquare` (`clip_view.cpp`) | **splits** | the square and the follow arithmetic are here; `getCurrentlyRecordingLinearly()`, `currentUIMode`, `getCurrentUI() == this` and `armState` are the scene's, and arrive as `TickSquareGates` |
/// | `AudioClipView`'s `lastTickSquare` / `mustRedrawTickSquares` latch (`AudioClipView::graphicsRoutine`) | the scene | not part of the shared tick-square arithmetic: the two fields are `AudioClipView`-**private** (`origin/main:src/deluge/gui/views/audio_clip_view.h`), have no counterpart anywhere in the timeline chain, and have exactly one consumer. `ClipView::getTickSquare` below is the genuinely shared half |
/// | `TimelineView::getMaxZoom` / `getMaxLength` (pure virtual, `timeline_view.h`) | the scene | model questions whose answer differs per view (`ClipView`'s two in `clip_view.cpp`, `AudioClipView`'s in `origin/main:src/deluge/gui/views/audio_clip_view.cpp`); they arrive here as arguments |
/// | `TimelineView::setupScroll`, `calculateZoomPinSquares`, `tellMatrixDriverWhichRowsContainSomethingZoomable`, `scrollFinished` | **dropped** | they are the `PadLEDs` zoom/scroll transition, which the port does not carry (see below) |
/// | `TimelineView::displayZoomLevel`, `displayScrollPos`, `setTripletsLEDState`, `midiLearnFlash` | the scene | they draw, blink an LED, or ask for a redraw |
/// | `TimelineView::buttonAction`, `ClipView::buttonAction` (`clip_view.cpp`) | the scene / `GlobalControls` | button routing; the tail is `view.buttonAction`, which is `services/global_controls.h` |
/// | `TimelineView::toTimelineView` (`timeline_view.h`), `getNavSysId` | **dropped** | a downcast in legacy's `UI` chain, and an index into the two-slot `Song::xScroll`/`xZoom` arrays. One `TimelineState` *is* one slot, so neither has anything to name |
/// | `ClipView::lengthenClip` (`clip_view.cpp`), `shortenClip`, `changeClipLength`, `horizontalEncoderAction` | the scene | they run the action logger and write `Clip::loopLength`; they *use* the helpers here, they are not them |
///
/// @par The zoom and scroll transitions are legacy's `PadLEDs` animation, and the port does not carry them
///
/// `initiateXScroll` and `initiateXZoom` each do two things: they set `Song::xScroll`/`xZoom`, and they start a
/// `PadLEDs` transition (`TimelineView::initiateXScroll` and `TimelineView::initiateXZoom` in `timeline_view.cpp`). The
/// shell has no transition surface, so only the first half is here -- a scroll or a zoom lands on the new view
/// immediately instead of sliding into it. (`src/ui/scenes/marker/marker_scene.h`'s "The zoom slide and the playhead" is
/// the marker editor's own slide.) That imposes a **one-detent ceiling on the legacy oracle**: a successful legacy zoom
/// leaves `UI_MODE_HORIZONTAL_ZOOM` set until the animation finishes, and `horizontalEncoderAction`'s own
/// `isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)` gate then refuses the next detent. Nothing
/// runs that animation to completion in a spec process.
///
/// **Clearing that mode by hand does buy the next detent here.** Measured, in
/// `tests/integration/ui/scenes/timeline_spec.cpp`: the scroll example takes a second detent and lands on page
/// two, and the zoom examples take a second detent and are then stopped by the max-zoom *delay*
/// rather than by the mode. `WaveformBasicNavigator::zoom`'s tail
/// (`origin/main:src/deluge/gui/waveform/waveform_basic_navigator.cpp`) is the same three steps that end the animation arm of
/// `TimelineView::initiateXZoom` (setting `UI_MODE_HORIZONTAL_ZOOM`, `PadLEDs::recordTransitionBegin`,
/// `PadLEDs::renderZoom`), and contains no wait of its own.
///
/// @par This state starts from the song's clip navigation, which `adopt` takes
///
/// The authority is the song's clip navigation slot, `Song::xScroll[NAVIGATION_CLIP]` /
/// `xZoom[NAVIGATION_CLIP]`, which is what gets written to the song file and what the audio clip view's
/// columns are read at. A scene reads it through its clip-facts port (`bind::ClipFacts::clip_navigation`),
/// whose host names the slot, and `adopt` copies what that answers: no scene spells the slot itself.
// clang-format on

#include "ui/scenes/bind/ports/clip_facts.h"

#include <cstdint>

namespace deluge::ui::scenes::timeline {

/// @brief `kShortPressTime` (`definitions_cxx.hpp`, `kSampleRate / 2`) in milliseconds.
///
/// Legacy measures the zoom delay in audio samples off `AudioEngine::audioSampleTimer`
/// (`TimelineView::horizontalEncoderAction`). This layer has no clock at all -- every deadline is against
/// the `now_ms` the shell passes in -- so the same half second is spelled in
/// milliseconds, exactly as `GlobalControls::kShortPressMs` already does.
inline constexpr uint32_t kShortPressMs = 500;

/// @brief `xScrollBeforeFollowingAutoExtendingLinearRecording`'s "none" sentinel
///        (`clip_navigation_timeline_view.cpp`, where `ClipNavigationTimelineView::focusRegained` resets it).
inline constexpr int32_t kNotFollowing = -1;

/// @brief `divide_round_negative` (`util/functions.cpp`), transcribed.
///
/// Not called through `util/functions.h`: `scripts/layers.toml`'s `scenes-includes-outside` rule
/// allows this layer no `util/` include, and the whole function is five lines. Divisor must be
/// positive; rounds towards negative infinity, which is the entire reason `getSquareFromPos` calls
/// it rather than `/` (its own comment, `timeline_view.cpp`).
/// @param dividend Any value.
/// @param divisor  Must be positive.
/// @return @p dividend / @p divisor, rounded towards negative infinity.
constexpr int32_t divide_round_negative(int32_t dividend, int32_t divisor) {
	if (dividend >= 0) {
		return static_cast<int32_t>(static_cast<uint32_t>(dividend) / static_cast<uint32_t>(divisor));
	}
	return -static_cast<int32_t>(static_cast<uint32_t>(-dividend - 1) / static_cast<uint32_t>(divisor)) - 1;
}

/// @brief What `displayNumberOfBarsAndBeats` computes before it formats it
///        (`timeline_view.cpp`).
struct BarsBeats {
	uint32_t bar;
	uint32_t beat;
	uint32_t sub_beat;
};

/// @brief The three outcomes of one zoom detent, which `TimelineView::horizontalEncoderAction` spells as two early
///        returns and a fall-through.
enum class ZoomOutcome : uint8_t {
	/// The zoom limit refused it.
	Refused,
	/// The "don't blow past the clip's own max zoom" delay is still running in this direction.
	Delayed,
	/// `x_zoom` and `x_scroll` both moved.
	Zoomed,
};

/// @brief What one square of the pad grid means, and the gestures that move it.
///
/// A plain aggregate: no constructor, no invariants beyond the ones legacy has, and every field
/// public because legacy's equivalents are either public `Song` members or protected view members
/// that the port's scene owns outright.
struct TimelineState {
	/// @brief Legacy's `Song::xScroll[NAVIGATION_CLIP]` (`origin/main:src/deluge/model/song/song.h`; here
	///        `PersistentViewState::xScroll`, `model/song/song_view_state.h`). Signed, as it is there, and for the same
	///        reason: `getSquareFromPos` subtracts it.
	int32_t x_scroll = 0;
	/// @brief Legacy's `Song::xZoom[NAVIGATION_CLIP]` (`origin/main:src/deluge/model/song/song.h`; here
	/// `PersistentViewState::xZoom`, `model/song/song_view_state.h`) -- ticks per pad column.
	uint32_t x_zoom = 0;

	/// @brief `RootUI::supportsTriplets()` (`gui/ui/root_ui.h`), as a field.
	///
	/// A scene fact, not model state: `AudioClipView` overrides it to false (`AudioClipView::supportsTriplets` in
	/// `origin/main:src/deluge/gui/views/audio_clip_view.h`), `ArrangerView` too (`arranger_view.h`), everything else
	/// takes the base's true.
	bool supports_triplets = true;
	/// @brief Legacy's `Song::tripletsOn`.
	bool triplets_on = false;
	/// @brief `Song::tripletsLevel`, which matches main's (`origin/main:src/deluge/model/song/song.h`) -- ticks in one
	/// of the three triplets.
	uint32_t triplets_level = 0;

	/// @brief `TimelineView::delayHorizontalZoomUntil` (`timeline_view.h`), in milliseconds.
	///
	/// Zero means "no delay running", exactly as legacy uses it (the `delayHorizontalZoomUntil` checks in
	/// `TimelineView::horizontalEncoderAction`, `timeline_view.cpp`).
	uint32_t delay_zoom_until_ms = 0;
	/// @brief `TimelineView::delayHorizontalZoomMagnitude` (`timeline_view.h`) -- -1 or +1.
	int8_t delay_zoom_magnitude = 0;

	/// @brief `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording`
	///        (`clip_navigation_timeline_view.h`). `kNotFollowing` means none.
	///
	/// @note Legacy's is **`static`** -- one value shared by every `ClipNavigationTimelineView` in
	///       the process, so `sessionView` clearing it is `audioClipView` clearing it. This one is
	///       per-instance. Harmless while the port has one clip timeline: the field is only ever
	///       written by the clip navigation slot's own gestures, and a second timeline would want
	///       its own restore point anyway. Worth knowing before a second `TimelineState` exists,
	///       because it is the one place the port is deliberately *not* a transcription.
	int32_t scroll_before_following = kNotFollowing;

	/// @brief Take scroll, zoom and the triplets settings from the song's clip navigation.
	///
	/// The port of nothing: legacy needs no such call because its state never left `Song`. See the
	/// file comment for where the navigation comes from.
	/// @param navigation The navigation, as the clip-facts port answers it.
	void adopt(const bind::ClipNavigation& navigation);

	/// @name Pure arithmetic
	/// @{
	/// @brief `TimelineView::inTripletsView` (`timeline_view.cpp`).
	/// @return True when triplets are on and this scene supports them.
	[[nodiscard]] bool in_triplets_view() const;

	/// @brief `TimelineView::getPosFromSquare(square, xScroll, xZoom)` (`timeline_view.cpp`).
	/// @param square    Pad column, which may be outside the display.
	/// @param scroll    Scroll to measure from.
	/// @param zoom      Ticks per column.
	/// @return The tick position of @p square's left edge.
	[[nodiscard]] int32_t pos_from_square(int32_t square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom
	///        (`TimelineView::getPosFromSquare(square, localScroll)`).
	/// @param square Pad column.
	[[nodiscard]] int32_t pos_from_square(int32_t square) const;

	/// @brief `TimelineView::getSquareFromPos(pos, rightOnSquare, xScroll, xZoom)`.
	/// @param pos              Tick position.
	/// @param[out] right_on_square Set to whether @p pos lands exactly on a column edge; may be null.
	/// @param scroll           Scroll to measure from.
	/// @param zoom             Ticks per column.
	/// @return The column @p pos falls in, which may be negative or past the display.
	[[nodiscard]] int32_t square_from_pos(int32_t pos, bool* right_on_square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom.
	/// @param pos              Tick position.
	/// @param[out] right_on_square May be null.
	[[nodiscard]] int32_t square_from_pos(int32_t pos, bool* right_on_square = nullptr) const;

	/// @brief `TimelineView::getSquareEndFromPos`.
	/// @param pos Tick position.
	/// @return The column @p pos ends in -- one past `square_from_pos` unless it was exact.
	[[nodiscard]] int32_t square_end_from_pos(int32_t pos) const;

	/// @brief `TimelineView::isSquareDefined(square, xScroll, xZoom)`.
	/// @param square Pad column.
	/// @param scroll Scroll to measure from.
	/// @param zoom   Ticks per column.
	/// @return False only for the triplet columns that represent no time at all.
	[[nodiscard]] bool square_defined(int32_t square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom.
	/// @param square Pad column.
	[[nodiscard]] bool square_defined(int32_t square) const;

	/// @brief `ClipView::getLengthChopAmount` (`clip_view.cpp`).
	/// @param square The column whose *preceding* square's width is wanted.
	/// @return Ticks to remove when shortening by one square.
	[[nodiscard]] int32_t length_chop_amount(int32_t square) const;
	/// @brief `ClipView::getLengthExtendAmount` (`clip_view.cpp`).
	/// @param square The column to extend into.
	/// @return Ticks to add when lengthening by one square.
	[[nodiscard]] int32_t length_extend_amount(int32_t square) const;

	/// @brief `displayNumberOfBarsAndBeats`' decomposition (`timeline_view.cpp`).
	///
	/// Legacy's function takes `quantization` and `tooLongText` and **reads neither** -- see the
	/// note in `timeline.cpp`. Only the three numbers it actually computes are here; the popup, and
	/// `Song::getBarLength()`, stay with the caller.
	/// @param number         The tick position or length to describe.
	/// @param one_bar        `Song::getBarLength()` (`model/song/song.cpp`).
	/// @param count_from_one True to number bars/beats from 1 rather than 0.
	/// @return The bar, beat and sub-beat.
	[[nodiscard]] static BarsBeats bars_and_beats(uint32_t number, uint32_t one_bar, bool count_from_one);
	/// @}

	/// @name Gestures
	/// @{
	/// @brief `ClipNavigationTimelineView::focusRegained` (`clip_navigation_timeline_view.cpp`).
	void focus_regained();

	/// @brief Forget the auto-extending-linear-recording restore point --
	///        `ClipNavigationTimelineView::horizontalEncoderAction`'s first statement
	///        (`clip_navigation_timeline_view.cpp`).
	///
	/// Legacy performs this on **every** horizontal encoder turn, before delegating to
	/// `TimelineView::horizontalEncoderAction`: once the user has scrolled or zoomed by
	/// hand, the position the follow would have snapped back to is stale, and `getTickSquare`'s
	/// restore arm (`clip_view.cpp`) must not fire.
	///
	/// `zoom()` and `scroll()` both call it, so a scene that only calls the gestures gets it for
	/// free. It is public because of **where** legacy does it: at the top of
	/// `ClipNavigationTimelineView::horizontalEncoderAction`, which is *above*
	/// `TimelineView::horizontalEncoderAction`'s re-entry guards and UI-mode gates
	/// (`timeline_view.cpp`). So legacy clears the field even when those
	/// gates then refuse the turn, and this port -- whose scene applies those gates before calling
	/// a gesture -- does not. A scene that wants that last case exactly can call this at its own
	/// horizontal-encoder entry; calling it twice costs nothing.
	void stop_following();

	/// @brief `TimelineView::horizontalEncoderAction`'s zoom arm (`timeline_view.cpp`),
	///        state half.
	///
	/// Everything the caller must have decided already: that the horizontal encoder's button is
	/// held, that no other UI mode blocks it, and that the clip is scrollable and zoomable at all
	/// (`ClipView::horizontalEncoderAction`, `clip_view.cpp`).
	///
	/// Calls `stop_following()` first, unconditionally -- legacy's first statement of
	/// `ClipNavigationTimelineView::horizontalEncoderAction`, which runs whether or not the zoom is then refused.
	/// @param offset   Signed detents. Legacy negates it into a magnitude: +1 zooms *in*.
	/// @param max_zoom The scene's `getMaxZoom()`.
	/// @param now_ms   The shell's clock, for the max-zoom delay.
	/// @return What happened, so the caller knows whether to show the zoom-level popup.
	ZoomOutcome zoom(int32_t offset, uint32_t max_zoom, uint32_t now_ms);

	/// @brief `TimelineView::horizontalEncoderAction`'s scroll arm (`timeline_view.cpp`),
	///        state half.
	///
	/// Note that legacy shows the scroll position **whether or not the scroll moved** (the `displayScrollPos()` call in
	/// `TimelineView::horizontalEncoderAction` sits outside the `if (newXScroll != currentSong->xScroll[navSysId])`),
	/// so the return value says only whether `x_scroll` changed.
	///
	/// Calls `stop_following()` first, unconditionally -- legacy's first statement in
	/// `ClipNavigationTimelineView::horizontalEncoderAction`, as `zoom()` does.
	/// @param offset     Signed detents.
	/// @param max_length The scene's `getMaxLength()`.
	/// @return True if `x_scroll` moved.
	bool scroll(int32_t offset, uint32_t max_length);

	/// @brief `TimelineView::zoomToMax` (`timeline_view.cpp`), state half.
	/// @param max_zoom The scene's `getMaxZoom()`.
	/// @param in_only  True to zoom only when @p max_zoom is *further in* than the current zoom.
	/// @return True if anything moved.
	bool zoom_to_max(uint32_t max_zoom, bool in_only = false);

	/// @brief `TimelineView::scrollRightToEndOfLengthIfNecessary` (`timeline_view.cpp`).
	/// @param max_length The length to scroll to the end of.
	/// @return True if it scrolled.
	bool scroll_right_to_end_of_length_if_necessary(int32_t max_length);

	/// @brief `TimelineView::scrollLeftIfTooFarRight` (`timeline_view.cpp`).
	/// @param max_length The length the view must not sit entirely past.
	/// @return True if it scrolled.
	bool scroll_left_if_too_far_right(int32_t max_length);

	/// @brief `ClipNavigationTimelineView::horizontalScrollForLinearRecording`
	///        (`clip_navigation_timeline_view.cpp`), state half.
	///
	/// Legacy's two arms (the `initiateXScroll` call animated, the `else` arm instant) write the same scroll; only the
	/// animation and the redraw request differ, and neither is this layer's. The `displayScrollPos()` popup
	/// is the caller's, which is why this returns whether it got past the guard.
	/// @param new_scroll The scroll to move to.
	/// @param max_length The scene's `getMaxLength()`.
	/// @return True if `new_scroll` was inside @p max_length and was stored -- "true" even when
	///         @p new_scroll equalled the scroll already there, because legacy's popup is inside
	///         the same length guard rather than inside the store.
	/// @note   That popup is **also** guarded by `if (!display->hasPopup())`
	///         (`clip_navigation_timeline_view.cpp`), so a true return means "legacy would have
	///         considered showing the scroll position", not "legacy showed it". A caller that
	///         shows one unconditionally will show a popup legacy suppresses.
	bool scroll_for_linear_recording(int32_t new_scroll, uint32_t max_length);

	/// @brief `TimelineView::tripletsButtonPressed` (`timeline_view.cpp`), state half.
	///
	/// The redraw request and `setTripletsLEDState()` are the caller's.
	void toggle_triplets();
	/// @}

	/// @name Tick square
	/// @{
	/// @brief What `ClipView::getTickSquare` asks its view before it auto-scrolls
	///        (`clip_view.cpp`).
	struct TickSquareGates {
		/// `getCurrentClip()->getCurrentlyRecordingLinearly()`.
		bool recording_linearly = false;
		/// `(!currentUIMode || currentUIMode == UI_MODE_AUDITIONING) && getCurrentUI() == this`
		/// Both halves are the shell's and the scene's, not this state's.
		bool ui_idle = false;
		/// `getCurrentClip()->armState == ArmState::OFF`. The `||
		/// xScrollBefore... != -1` half of that same condition is this state's own and is applied
		/// here.
		bool clip_unarmed = false;
		/// The scene's `getMaxLength()`, for the scroll that may follow.
		uint32_t max_length = 0;
	};

	/// @brief What one `getTickSquare` produced.
	struct TickSquare {
		/// The column the play position is in. May be negative or >= `kDisplayWidth`; legacy's
		/// callers are the ones that turn that into 255 (`AudioClipView::graphicsRoutine` in
		/// `origin/main:src/deluge/gui/views/audio_clip_view.cpp`).
		int32_t square = 0;
		/// True if the auto-extending-linear-recording follow moved `x_scroll`, so the caller can redraw and show the
		/// scroll position (`ClipNavigationTimelineView::horizontalScrollForLinearRecording` in
		/// `clip_navigation_timeline_view.cpp`).
		bool scrolled = false;
	};

	/// @brief `ClipView::getTickSquare` (`clip_view.cpp`), state half.
	///
	/// @warning **This reproduces a defect in shipping firmware.** The arm that stops following an
	///          auto-extending linear recording and restores the scroll it started from compares
	///          that scroll against the **zoom**: `if (newXScroll !=
	///          currentSong->xZoom[NAVIGATION_CLIP])` (`clip_view.cpp`). Every neighbouring
	///          line in the same arm reads `xScroll[NAVIGATION_CLIP]`; this one reads `xZoom`. So
	///          the restore is skipped exactly when the remembered scroll happens to equal the
	///          current zoom, and performed when it equals the current scroll -- where legacy
	///          plainly meant to skip it. Ported faithfully and asserted on both sides in
	///          `tests/integration/ui/scenes/timeline_spec.cpp`, because a port whose job is to agree with
	///          legacy may not quietly disagree with it. Recorded in `src/ui/scenes/README.md`'s
	///          "Defects this port found in shipping firmware"; fixing it means
	///          one change against both sides and both spec expectations at once.
	/// @param live_pos `getCurrentClip()->getLivePos()`.
	/// @param gates    What the scene had to answer.
	/// @return The column, and whether the follow scrolled.
	TickSquare tick_square(int32_t live_pos, const TickSquareGates& gates);

	/// @note `AudioClipView`'s `lastTickSquare` / `mustRedrawTickSquares` latch (`AudioClipView::graphicsRoutine` in
	///       `origin/main:src/deluge/gui/views/audio_clip_view.cpp`) is **not** here. It is private to that one view,
	///       has no counterpart in the timeline chain, and has one consumer; it belongs to `AudioClipScene`. See the
	///       table at the top of this file.
	/// @}
};

} // namespace deluge::ui::scenes::timeline
