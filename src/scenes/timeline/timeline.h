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
/// | `xScroll[navSys]`, `xZoom[navSys]` (`model/song/song.h:196-197`) | here (`x_scroll`, `x_zoom`) | the state itself |
/// | `TimelineView::getPosFromSquare` (`timeline_view.cpp:369-411`) | here | pure arithmetic over scroll/zoom/triplets |
/// | `TimelineView::getSquareFromPos` (`:413-473`) | here | ditto |
/// | `TimelineView::getSquareEndFromPos` (`:475-482`) | here | ditto |
/// | `TimelineView::isSquareDefined` (`:484-505`) | here | ditto |
/// | `TimelineView::inTripletsView` (`:507-509`) | here | reads `supportsTriplets()` (a scene fact, so a field) and `tripletsOn` |
/// | `ClipView::getLengthChopAmount` (`clip_view.cpp:315-333`) | here | pure arithmetic over zoom and triplets |
/// | `ClipView::getLengthExtendAmount` (`clip_view.cpp:335-352`) | here | ditto |
/// | `TimelineView::horizontalEncoderAction`'s zoom arm (`:161-199`) | **splits** | the arithmetic and the zoom-delay latch are here; the UI-mode gates and the popup are the scene's |
/// | `TimelineView::horizontalEncoderAction`'s scroll arm (`:203-223`) | **splits** | same split |
/// | `TimelineView::initiateXScroll` (`:268-283`) | **splits** | `xScroll = newXScroll` is here; `setupScroll` + `PadLEDs::horizontal::setupScroll` + `UI_MODE_HORIZONTAL_SCROLL` are the dropped animation |
/// | `TimelineView::initiateXZoom` (`:305-329`) | **splits** | `xScroll = newScroll` is here; the rest is the dropped animation |
/// | `TimelineView::zoomToMax` (`:286-302`) | here | arithmetic plus `initiateXZoom`'s state half |
/// | `TimelineView::scrollRightToEndOfLengthIfNecessary` (`:331-343`) | here | arithmetic |
/// | `TimelineView::scrollLeftIfTooFarRight` (`:345-353`) | here | arithmetic |
/// | `TimelineView::tripletsButtonPressed` (`:355-363`) | **splits** | `tripletsOn`/`tripletsLevel` are here; `uiNeedsRendering` and the TRIPLETS LED are the scene's |
/// | `TimelineView::displayNumberOfBarsAndBeats` (`:241-265`) | **splits** | the bar/beat/sub-beat decomposition is here (`bars_and_beats`); the popup is the scene's |
/// | `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording` (`clip_navigation_timeline_view.h:32`) | here (`scroll_before_following`) | state |
/// | `ClipNavigationTimelineView::focusRegained` (`:26-28`) | here (`focus_regained`) | it does nothing but reset that field |
/// | `ClipNavigationTimelineView::horizontalScrollForLinearRecording` (`:36-51`) | **splits** | the length guard and the scroll write are here; the animate-or-not choice, `uiNeedsRendering` and the popup are the scene's |
/// | `ClipView::getTickSquare` (`clip_view.cpp:354-389`) | **splits** | the square and the follow arithmetic are here; `getCurrentlyRecordingLinearly()`, `currentUIMode`, `getCurrentUI() == this` and `armState` are the scene's, and arrive as `TickSquareGates` |
/// | `AudioClipView`'s `lastTickSquare` / `mustRedrawTickSquares` latch (`audio_clip_view.cpp:296-309`) | here (`take_tick_square`) | the "tick squares" the plan's file table assigns to this state; legacy keeps a private copy per view |
/// | `TimelineView::getMaxZoom` / `getMaxLength` (pure virtual, `timeline_view.h:35`, `:38`) | the scene | model questions whose answer differs per view (`ClipView`'s two at `clip_view.cpp:35-37`/`:39-41`, `AudioClipView`'s at `audio_clip_view.cpp:857-862`/`:864-870`); they arrive here as arguments |
/// | `TimelineView::setupScroll` (`:44-50`), `calculateZoomPinSquares` (`:52-63`), `tellMatrixDriverWhichRowsContainSomethingZoomable` (`:65-67`), `scrollFinished` (`:36-41`) | **dropped** | they are the `PadLEDs` zoom/scroll transition, which the port does not carry (see below) |
/// | `TimelineView::displayZoomLevel` (`:126-131`), `displayScrollPos` (`:230-239`), `setTripletsLEDState` (`:365-367`), `midiLearnFlash` (`:511-513`) | the scene | they draw, blink an LED, or ask for a redraw |
/// | `TimelineView::buttonAction` (`:69-124`), `ClipView::buttonAction` (`clip_view.cpp:47-70`) | the scene / `GlobalControls` | button routing; the tail is `view.buttonAction`, which is `services/global_controls.h` |
/// | `TimelineView::toTimelineView` (`timeline_view.h:33`), `getNavSysId` (`:40`) | **dropped** | a downcast in legacy's `UI` chain, and an index into the two-slot `Song::xScroll`/`xZoom` arrays. One `TimelineState` *is* one slot, so neither has anything to name |
/// | `ClipView::lengthenClip` (`clip_view.cpp:75-121`), `shortenClip` (`:124-144`), `changeClipLength` (`:264-313`), `horizontalEncoderAction` (`:146-263`) | the scene (E4 task 12) | they run the action logger and write `Clip::loopLength`; they *use* the helpers here, they are not them |
///
/// @par The zoom and scroll transitions are legacy's `PadLEDs` animation, and the port does not carry them
///
/// `initiateXScroll` and `initiateXZoom` each do two things: they set `Song::xScroll`/`xZoom`, and
/// they start a `PadLEDs` transition (`timeline_view.cpp:276-282`, `:307-328`). The shell has no
/// transition surface, so only the first half is here -- a scroll or a zoom lands on the new view
/// immediately instead of sliding into it. This is the same divergence E3 recorded for the marker
/// editor (`src/scenes/marker/marker_scene.h`, "The zoom transition animation"), and it imposes the
/// same **one-detent ceiling on the legacy oracle**: a successful legacy zoom leaves
/// `UI_MODE_HORIZONTAL_ZOOM` set until the animation finishes, and
/// `horizontalEncoderAction`'s own `isUIModeActiveExclusively(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)`
/// gate (`:164`) then refuses the next detent. Nothing runs that animation to completion in a spec
/// process. `sim/scenes_spec/timeline_spec.cpp` measures the ceiling rather than working around it.
///
/// @par This state is a mirror of `Song`'s, and `adopt`/`publish` are the only place that says so
///
/// Legacy's authority is `Song::xScroll[NAVIGATION_CLIP]` / `xZoom[NAVIGATION_CLIP]`, which is what
/// gets written to the song file and what `Clip::renderData` compares against. The scenes layer may
/// not name `currentSong` (`check_layer_rules.py`'s `scenes-currentSong` row), so the state lives
/// here and the two functions below are the seam. They are the *only* two places in this layer that
/// spell `NAVIGATION_CLIP`, deliberately: two root scenes each reaching into the array themselves
/// would be two places to get the slot wrong.
// clang-format on

#include "definitions_cxx.hpp"

#include <cstdint>

class Song;

namespace deluge::scenes::timeline {

/// @brief `kShortPressTime` (`definitions_cxx.hpp:1028`, `kSampleRate / 2`) in milliseconds.
///
/// Legacy measures the zoom delay in audio samples off `AudioEngine::audioSampleTimer`
/// (`timeline_view.cpp:172`, `:184`). This layer has no clock at all -- every deadline is against
/// the `now_ms` the shell passes in (plan constraint 6) -- so the same half second is spelled in
/// milliseconds, exactly as `GlobalControls::kShortPressMs` already does.
inline constexpr uint32_t kShortPressMs = 500;

/// @brief `xScrollBeforeFollowingAutoExtendingLinearRecording`'s "none" sentinel
///        (`clip_navigation_timeline_view.cpp:24`, `:27`).
inline constexpr int32_t kNotFollowing = -1;

/// @brief `divide_round_negative` (`util/functions.cpp:1739-1746`), transcribed.
///
/// Not called through `util/functions.h`: `check_layer_rules.py`'s `scenes-includes-outside` row
/// allows this layer no `util/` include, and the whole function is five lines. Divisor must be
/// positive; rounds towards negative infinity, which is the entire reason `getSquareFromPos` calls
/// it rather than `/` (its own comment, `timeline_view.cpp:457-458`).
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
///        (`timeline_view.cpp:244-260`).
struct BarsBeats {
	uint32_t bar;
	uint32_t beat;
	uint32_t sub_beat;
};

/// @brief The three outcomes of one zoom detent, which legacy spells as two `goto getOut`s and a
///        fall-through (`timeline_view.cpp:176`, `:191`, `:194-198`).
enum class ZoomOutcome : uint8_t {
	/// The zoom limit refused it (`:189-192`).
	Refused,
	/// The "don't blow past the clip's own max zoom" delay is still running in this direction
	/// (`:171-177`).
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
	/// @brief Legacy's `Song::xScroll[NAVIGATION_CLIP]` (`model/song/song.h:197`). Signed, as it is
	///        there, and for the same reason: `getSquareFromPos` subtracts it.
	int32_t x_scroll = 0;
	/// @brief Legacy's `Song::xZoom[NAVIGATION_CLIP]` (`song.h:196`) -- ticks per pad column.
	uint32_t x_zoom = 0;

	/// @brief `RootUI::supportsTriplets()` (`gui/ui/root_ui.h:32`), as a field.
	///
	/// A scene fact, not model state: `AudioClipView` overrides it to false
	/// (`audio_clip_view.h:38`), `ArrangerView` too (`arranger_view.h:78`), everything else takes
	/// the base's true.
	bool supports_triplets = true;
	/// @brief Legacy's `Song::tripletsOn` (`song.h:200`).
	bool triplets_on = false;
	/// @brief Legacy's `Song::tripletsLevel` (`song.h:201`) -- ticks in one of the three triplets.
	uint32_t triplets_level = 0;

	/// @brief `TimelineView::delayHorizontalZoomUntil` (`timeline_view.h:78`), in milliseconds.
	///
	/// Zero means "no delay running", exactly as legacy uses it (`timeline_view.cpp:171`, `:183`).
	uint32_t delay_zoom_until_ms = 0;
	/// @brief `TimelineView::delayHorizontalZoomMagnitude` (`timeline_view.h:81`) -- -1 or +1.
	int8_t delay_zoom_magnitude = 0;

	/// @brief `ClipNavigationTimelineView::xScrollBeforeFollowingAutoExtendingLinearRecording`
	///        (`clip_navigation_timeline_view.h:32`). `kNotFollowing` means none.
	int32_t scroll_before_following = kNotFollowing;

	/// @brief `AudioClipView::lastTickSquare` (`audio_clip_view.h:68`).
	int32_t last_tick_square = 0;
	/// @brief `AudioClipView::mustRedrawTickSquares` (`audio_clip_view.h:69`).
	bool must_redraw_tick_squares = false;

	/// @name The `Song` seam
	/// @{
	/// @brief Take scroll, zoom and the triplets settings from @p song's clip navigation slot.
	///
	/// The port of nothing: legacy needs no such call because its state never left `Song`. See the
	/// file comment for why this layer's did, and why this is the only place that names the slot.
	/// @param song The song to read.
	void adopt(const Song& song);
	/// @brief Write scroll, zoom and the triplets settings back into @p song's clip navigation slot.
	/// @param song The song to write.
	void publish(Song& song) const;
	/// @}

	/// @name Pure arithmetic
	/// @{
	/// @brief `TimelineView::inTripletsView` (`timeline_view.cpp:507-509`).
	/// @return True when triplets are on and this scene supports them.
	[[nodiscard]] bool in_triplets_view() const;

	/// @brief `TimelineView::getPosFromSquare(square, xScroll, xZoom)` (`timeline_view.cpp:369-398`).
	/// @param square    Pad column, which may be outside the display.
	/// @param scroll    Scroll to measure from.
	/// @param zoom      Ticks per column.
	/// @return The tick position of @p square's left edge.
	[[nodiscard]] int32_t pos_from_square(int32_t square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom
	///        (`TimelineView::getPosFromSquare(square, localScroll)`, `:400-411`).
	/// @param square Pad column.
	[[nodiscard]] int32_t pos_from_square(int32_t square) const;

	/// @brief `TimelineView::getSquareFromPos(pos, rightOnSquare, xScroll, xZoom)` (`:413-460`).
	/// @param pos              Tick position.
	/// @param[out] right_on_square Set to whether @p pos lands exactly on a column edge; may be null.
	/// @param scroll           Scroll to measure from.
	/// @param zoom             Ticks per column.
	/// @return The column @p pos falls in, which may be negative or past the display.
	[[nodiscard]] int32_t square_from_pos(int32_t pos, bool* right_on_square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom (`:462-473`).
	/// @param pos              Tick position.
	/// @param[out] right_on_square May be null.
	[[nodiscard]] int32_t square_from_pos(int32_t pos, bool* right_on_square = nullptr) const;

	/// @brief `TimelineView::getSquareEndFromPos` (`:475-482`).
	/// @param pos Tick position.
	/// @return The column @p pos ends in -- one past `square_from_pos` unless it was exact.
	[[nodiscard]] int32_t square_end_from_pos(int32_t pos) const;

	/// @brief `TimelineView::isSquareDefined(square, xScroll, xZoom)` (`:484-492`).
	/// @param square Pad column.
	/// @param scroll Scroll to measure from.
	/// @param zoom   Ticks per column.
	/// @return False only for the triplet columns that represent no time at all.
	[[nodiscard]] bool square_defined(int32_t square, int32_t scroll, uint32_t zoom) const;
	/// @brief The same, from this state's own scroll and zoom (`:495-505`).
	/// @param square Pad column.
	[[nodiscard]] bool square_defined(int32_t square) const;

	/// @brief `ClipView::getLengthChopAmount` (`clip_view.cpp:315-333`).
	/// @param square The column whose *preceding* square's width is wanted.
	/// @return Ticks to remove when shortening by one square.
	[[nodiscard]] int32_t length_chop_amount(int32_t square) const;
	/// @brief `ClipView::getLengthExtendAmount` (`clip_view.cpp:335-352`).
	/// @param square The column to extend into.
	/// @return Ticks to add when lengthening by one square.
	[[nodiscard]] int32_t length_extend_amount(int32_t square) const;

	/// @brief `displayNumberOfBarsAndBeats`' decomposition (`timeline_view.cpp:244-260`).
	///
	/// Legacy's function takes `quantization` and `tooLongText` and **reads neither** -- see the
	/// note in `timeline.cpp`. Only the three numbers it actually computes are here; the popup, and
	/// `Song::getBarLength()`, stay with the caller.
	/// @param number         The tick position or length to describe.
	/// @param one_bar        `Song::getBarLength()` (`model/song/song.cpp:5518-5520`).
	/// @param count_from_one True to number bars/beats from 1 rather than 0.
	/// @return The bar, beat and sub-beat.
	[[nodiscard]] static BarsBeats bars_and_beats(uint32_t number, uint32_t one_bar, bool count_from_one);
	/// @}

	/// @name Gestures
	/// @{
	/// @brief `ClipNavigationTimelineView::focusRegained` (`clip_navigation_timeline_view.cpp:26-28`).
	void focus_regained();

	/// @brief `TimelineView::horizontalEncoderAction`'s zoom arm (`timeline_view.cpp:165-198`),
	///        state half.
	///
	/// Everything the caller must have decided already: that the horizontal encoder's button is
	/// held, that no other UI mode blocks it, and that the clip is scrollable and zoomable at all
	/// (`ClipView::horizontalEncoderAction`, `clip_view.cpp:256-258`).
	/// @param offset   Signed detents. Legacy negates it into a magnitude: +1 zooms *in*.
	/// @param max_zoom The scene's `getMaxZoom()`.
	/// @param now_ms   The shell's clock, for the max-zoom delay.
	/// @return What happened, so the caller knows whether to show the zoom-level popup (`:198`).
	ZoomOutcome zoom(int32_t offset, uint32_t max_zoom, uint32_t now_ms);

	/// @brief `TimelineView::horizontalEncoderAction`'s scroll arm (`timeline_view.cpp:206-222`),
	///        state half.
	///
	/// Note that legacy shows the scroll position **whether or not the scroll moved** (`:221` sits
	/// outside the `if` at `:216`), so the return value says only whether `x_scroll` changed.
	/// @param offset     Signed detents.
	/// @param max_length The scene's `getMaxLength()`.
	/// @return True if `x_scroll` moved.
	bool scroll(int32_t offset, uint32_t max_length);

	/// @brief `TimelineView::zoomToMax` (`timeline_view.cpp:286-302`), state half.
	/// @param max_zoom The scene's `getMaxZoom()`.
	/// @param in_only  True to zoom only when @p max_zoom is *further in* than the current zoom.
	/// @return True if anything moved.
	bool zoom_to_max(uint32_t max_zoom, bool in_only = false);

	/// @brief `TimelineView::scrollRightToEndOfLengthIfNecessary` (`timeline_view.cpp:331-343`).
	/// @param max_length The length to scroll to the end of.
	/// @return True if it scrolled.
	bool scroll_right_to_end_of_length_if_necessary(int32_t max_length);

	/// @brief `TimelineView::scrollLeftIfTooFarRight` (`timeline_view.cpp:345-353`).
	/// @param max_length The length the view must not sit entirely past.
	/// @return True if it scrolled.
	bool scroll_left_if_too_far_right(int32_t max_length);

	/// @brief `ClipNavigationTimelineView::horizontalScrollForLinearRecording`
	///        (`clip_navigation_timeline_view.cpp:36-51`), state half.
	///
	/// Legacy's two arms (`:41` animated, `:44-45` instant) write the same scroll; only the
	/// animation and the redraw request differ, and neither is this layer's. The popup at `:47-49`
	/// is the caller's, which is why this returns whether it got past the guard.
	/// @param new_scroll The scroll to move to.
	/// @param max_length The scene's `getMaxLength()`.
	/// @return True if `new_scroll` was inside @p max_length and was stored -- which is also
	///         legacy's condition for showing the scroll-position popup, so it is "true" even when
	///         @p new_scroll equalled the scroll already there.
	bool scroll_for_linear_recording(int32_t new_scroll, uint32_t max_length);

	/// @brief `TimelineView::tripletsButtonPressed` (`timeline_view.cpp:355-363`), state half.
	///
	/// The redraw request and `setTripletsLEDState()` are the caller's.
	void toggle_triplets();
	/// @}

	/// @name Tick square
	/// @{
	/// @brief What `ClipView::getTickSquare` asks its view before it auto-scrolls
	///        (`clip_view.cpp:359-363`).
	struct TickSquareGates {
		/// `getCurrentClip()->getCurrentlyRecordingLinearly()` (`:359`).
		bool recording_linearly = false;
		/// `(!currentUIMode || currentUIMode == UI_MODE_AUDITIONING) && getCurrentUI() == this`
		/// (`:361-362`). Both halves are the shell's and the scene's, not this state's.
		bool ui_idle = false;
		/// `getCurrentClip()->armState == ArmState::OFF` (`:363`). The `||
		/// xScrollBefore... != -1` half of that same condition is this state's own and is applied
		/// here.
		bool clip_unarmed = false;
		/// The scene's `getMaxLength()`, for the scroll that may follow.
		uint32_t max_length = 0;
	};

	/// @brief What one `getTickSquare` produced.
	struct TickSquare {
		/// The column the play position is in. May be negative or >= `kDisplayWidth`; legacy's
		/// callers are the ones that turn that into 255 (`audio_clip_view.cpp:291-293`).
		int32_t square = 0;
		/// True if the auto-extending-linear-recording follow moved `x_scroll`, so the caller can
		/// redraw and show the scroll position (`clip_navigation_timeline_view.cpp:45-49`).
		bool scrolled = false;
	};

	/// @brief `ClipView::getTickSquare` (`clip_view.cpp:354-389`), state half.
	/// @param live_pos `getCurrentClip()->getLivePos()` (`:356`).
	/// @param gates    What the scene had to answer.
	/// @return The column, and whether the follow scrolled.
	TickSquare tick_square(int32_t live_pos, const TickSquareGates& gates);

	/// @brief `AudioClipView::graphicsRoutine`'s tick-square latch (`audio_clip_view.cpp:296-309`).
	///
	/// Legacy sends the squares to `PadLEDs` only when the square moved or a redraw was forced, and
	/// clears the force in the same breath. The `PadLEDs::setTickSquares` call itself is the
	/// scene's; this is the decision in front of it.
	/// @param new_square The square just computed (255 for "no playhead", as legacy uses).
	/// @return True if the caller should push the squares out.
	bool take_tick_square(int32_t new_square);
	/// @}
};

} // namespace deluge::scenes::timeline
