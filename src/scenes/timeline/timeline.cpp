/// @file
/// @brief `TimelineState` -- see `timeline.h` for what moved here out of legacy's `TimelineView` ->
///        `ClipNavigationTimelineView` -> `ClipView` chain, and what did not.
///
/// @par Transcription rule
///
/// Every body below keeps legacy's **declared types** rather than tidying them, because the
/// conversions those types cause are part of the answer. `int32_t / uint32_t` is an *unsigned*
/// division in C++, and three of the expressions here rely on it -- `newScroll`'s page snap
/// (`timeline_view.cpp:195`), `prevBlockStart` (`:374`) and the max-length comparisons
/// (`:216`, `clip_navigation_timeline_view.cpp:38`). Rewriting them "cleanly" in one signed type
/// would change results for negative scrolls, which is exactly the eleventh value a spec that
/// agreed on ten would miss. The project builds without `-Wconversion`
/// (`cmake/deluge_common_options.cmake`), so the mixed-type spellings compile as written.
///
/// @par Two things legacy does here that are dead, and are transcribed anyway
///
///   * `horizontalEncoderAction`'s `&& delayHorizontalZoomUntil == 0` (`:183`) can never be false:
///     the block above it (`:171-180`) either returns through `goto getOut` or leaves the field
///     zero, and if that block was skipped the field was already zero. Kept as-is so the two sides
///     read the same.
///   * `displayNumberOfBarsAndBeats` (`:241-265`) declares `quantization` and `tooLongText` and
///     **reads neither** -- the body uses `Song::getBarLength()` for the quantum and never mentions
///     the "too long" text. Every one of its five call sites passes both
///     (`:238`, `clip_view.cpp:175`, `audio_clip_view.cpp:798`, `:848`,
///     `arranger_view.cpp:2970`). `bars_and_beats` takes
///     neither; the caller keeps `Song::getBarLength()`, which is the one input that mattered.
///
/// @par A legacy bug, ported rather than fixed
///
/// `ClipView::getTickSquare`'s "stop following and go back" arm compares the *scroll* it is about
/// to restore against the *zoom*: `if (newXScroll != currentSong->xZoom[NAVIGATION_CLIP])`
/// (`clip_view.cpp:382`). Every neighbouring line reads `xScroll[NAVIGATION_CLIP]`; this one reads
/// `xZoom`. The effect is that the restore is skipped exactly when the remembered scroll happens to
/// equal the current zoom, and performed when it equals the current scroll (where legacy plainly
/// meant to skip it). `tick_square` reproduces it, because a port whose job is to agree with legacy
/// cannot quietly disagree with it. It is the sixth entry in `src/scenes/README.md`'s "Defects this
/// port found in shipping firmware" and in
/// `docs/superpowers/specs/2026-08-26-ui-migration-design.md` §10.5, and carries a `@warning` on
/// `TimelineState::tick_square` in `timeline.h`. Fixing it means one change against both sides and
/// both spec expectations at once.

#include "scenes/timeline/timeline.h"

#include "model/song/song.h"

#include <algorithm>

namespace deluge::scenes::timeline {

void TimelineState::adopt(const Song& song) {
	x_scroll = song.xScroll[NAVIGATION_CLIP];
	x_zoom = song.xZoom[NAVIGATION_CLIP];
	triplets_on = song.tripletsOn;
	triplets_level = song.tripletsLevel;
}

void TimelineState::publish(Song& song) const {
	song.xScroll[NAVIGATION_CLIP] = x_scroll;
	song.xZoom[NAVIGATION_CLIP] = x_zoom;
	song.tripletsOn = triplets_on;
	song.tripletsLevel = triplets_level;
}

// --- pure arithmetic --------------------------------------------------------------------------

bool TimelineState::in_triplets_view() const {
	// `TimelineView::inTripletsView` (`timeline_view.cpp:507-509`).
	return supports_triplets && triplets_on;
}

int32_t TimelineState::pos_from_square(int32_t square, int32_t scroll, uint32_t zoom) const {
	// `TimelineView::getPosFromSquare(square, xScroll, xZoom)` (`timeline_view.cpp:369-398`).
	if (in_triplets_view()) {
		// If zoomed in just a normal amount...
		if (zoom < triplets_level) {
			int32_t prevBlockStart = scroll / (triplets_level * 3) * (triplets_level * 3);
			int32_t blockStartRelativeToScroll = (prevBlockStart - scroll) / 3 * 4; // Negative or 0

			int32_t posRelativeToBlock =
			    static_cast<int32_t>(square) * (zoom * 4 / 3) - blockStartRelativeToScroll; // Rel. to block start pos
			uint32_t numBlocksIn = posRelativeToBlock / (triplets_level * 4);

			// These two lines affect the resulting "pos" of cols which are "undefined" so they can
			// be detected as such
			uint32_t numTripletsIn = posRelativeToBlock / triplets_level;
			if (numTripletsIn % 4 == 3) {
				posRelativeToBlock = (numBlocksIn + 1) * triplets_level * 3;
			}
			else {
				posRelativeToBlock -= numBlocksIn * triplets_level;
			}

			return posRelativeToBlock + prevBlockStart;
		}
		if (zoom < triplets_level * 2) {
			return scroll + square * zoom + (square % 2) * triplets_level / 2;
		}
	}

	return scroll + square * zoom;
}

int32_t TimelineState::pos_from_square(int32_t square) const {
	// `TimelineView::getPosFromSquare(square, localScroll)` (`timeline_view.cpp:400-411`), whose
	// `localScroll == -1` default and `getNavSysId()` lookup both resolve to this state's own pair.
	return pos_from_square(square, x_scroll, x_zoom);
}

int32_t TimelineState::square_from_pos(int32_t pos, bool* right_on_square, int32_t scroll, uint32_t zoom) const {
	// `TimelineView::getSquareFromPos(pos, rightOnSquare, xScroll, xZoom)` (`timeline_view.cpp:413-460`).
	int32_t posRelativeToScroll = pos - scroll;

	if (in_triplets_view()) {
		if (zoom < triplets_level) {
			int32_t blockStartPos = scroll / (triplets_level * 3) * (triplets_level * 3);
			int32_t blockStartRelativeToScroll = blockStartPos - scroll; // Will be negative or 0
			int32_t posRelativeToBlockStart = pos - blockStartPos;

			if (right_on_square != nullptr) {
				*right_on_square = (posRelativeToBlockStart % (zoom * 4 / 3) == 0);
			}

			// Keep as separate step, for rounding purposes
			int32_t numBlocksIn = divide_round_negative(posRelativeToBlockStart, triplets_level * 3);

			int32_t semiFinal =
			    posRelativeToBlockStart + blockStartRelativeToScroll * 4 / 3 + numBlocksIn * triplets_level;
			return divide_round_negative(semiFinal, zoom * 4 / 3);
		}
		if (zoom < triplets_level * 2) {
			int32_t posRelativeToTripletsStart = posRelativeToScroll % (triplets_level * 3);
			if (right_on_square != nullptr) {
				*right_on_square =
				    (posRelativeToTripletsStart == 0 || posRelativeToTripletsStart == triplets_level * 2);
			}

			if (posRelativeToTripletsStart >= triplets_level * 2) {
				posRelativeToTripletsStart -= triplets_level * 2;
			}
			return divide_round_negative(posRelativeToScroll - posRelativeToTripletsStart, zoom);
		}
	}

	if (right_on_square != nullptr) {
		*right_on_square = (posRelativeToScroll >= 0 && (posRelativeToScroll % zoom) == 0);
	}

	// Have to divide the two things separately before subtracting, otherwise negative results get
	// rounded the wrong way!
	return divide_round_negative(pos - scroll, zoom);
}

int32_t TimelineState::square_from_pos(int32_t pos, bool* right_on_square) const {
	// `TimelineView::getSquareFromPos(pos, rightOnSquare, localScroll)` (`timeline_view.cpp:462-473`).
	return square_from_pos(pos, right_on_square, x_scroll, x_zoom);
}

int32_t TimelineState::square_end_from_pos(int32_t pos) const {
	// `TimelineView::getSquareEndFromPos` (`timeline_view.cpp:475-482`).
	bool rightOnSquare = false;
	int32_t square = square_from_pos(pos, &rightOnSquare);
	if (!rightOnSquare) {
		square++;
	}
	return square;
}

bool TimelineState::square_defined(int32_t square, int32_t scroll, uint32_t zoom) const {
	// `TimelineView::isSquareDefined(square, xScroll, xZoom)` (`timeline_view.cpp:484-492`).
	if (!in_triplets_view()) {
		return true;
	}
	if (zoom > triplets_level) {
		return true;
	}
	return pos_from_square(square + 1, scroll, zoom) > pos_from_square(square, scroll, zoom);
}

bool TimelineState::square_defined(int32_t square) const {
	// `TimelineView::isSquareDefined(square, xScroll)` (`timeline_view.cpp:495-505`), the one
	// legacy's own comment marks "Deprecate this". It differs from the overload above only in
	// where it gets the zoom, which here is the same field either way.
	return square_defined(square, x_scroll, x_zoom);
}

int32_t TimelineState::length_chop_amount(int32_t square) const {
	// `ClipView::getLengthChopAmount` (`clip_view.cpp:315-333`).
	square--; // We want the width of the square before
	while (!square_defined(square)) {
		square--;
	}

	if (in_triplets_view()) {
		if (x_zoom < triplets_level) {
			return x_zoom * 4 / 3;
		}
		if (x_zoom < triplets_level * 2) {
			return x_zoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return x_zoom;
}

int32_t TimelineState::length_extend_amount(int32_t square) const {
	// `ClipView::getLengthExtendAmount` (`clip_view.cpp:335-352`). Same body as the chop, minus the
	// initial decrement and searching forwards rather than back.
	while (!square_defined(square)) {
		square++;
	}

	if (in_triplets_view()) {
		if (x_zoom < triplets_level) {
			return x_zoom * 4 / 3;
		}
		if (x_zoom < triplets_level * 2) {
			return x_zoom * 2 / 3 * (((square + 1) % 2) + 1);
		}
	}
	return x_zoom;
}

BarsBeats TimelineState::bars_and_beats(uint32_t number, uint32_t one_bar, bool count_from_one) {
	// `TimelineView::displayNumberOfBarsAndBeats` (`timeline_view.cpp:244-260`), minus its `sprintf`
	// and its popup.
	uint32_t whichBar = number / one_bar;
	uint32_t posWithinBar = number - whichBar * one_bar;
	uint32_t whichBeat = posWithinBar / (one_bar >> 2);
	uint32_t posWithinBeat = posWithinBar - whichBeat * (one_bar >> 2);
	uint32_t whichSubBeat = posWithinBeat / (one_bar >> 4);

	if (count_from_one) {
		whichBar++;
		whichBeat++;
		whichSubBeat++;
	}

	return BarsBeats{whichBar, whichBeat, whichSubBeat};
}

// --- gestures ---------------------------------------------------------------------------------

void TimelineState::stop_following() {
	// `ClipNavigationTimelineView::horizontalEncoderAction`'s first statement
	// (`clip_navigation_timeline_view.cpp:32`), which runs on every horizontal turn before the
	// delegate at `:33`.
	scroll_before_following = kNotFollowing;
}

void TimelineState::focus_regained() {
	// `ClipNavigationTimelineView::focusRegained` (`clip_navigation_timeline_view.cpp:26-28`), whose
	// whole body is this one line. `ClipView::focusRegained` (`clip_view.cpp:43-45`) is a straight
	// delegation to it.
	scroll_before_following = kNotFollowing;
}

ZoomOutcome TimelineState::zoom(int32_t offset, uint32_t max_zoom, uint32_t now_ms) {
	// `TimelineView::horizontalEncoderAction`'s zoom arm (`timeline_view.cpp:165-198`). The two
	// enclosing gates -- `isUIModeActive(UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON)` (`:161`) and
	// `isUIModeActiveExclusively(...) || inNoteRowEditor` (`:164`) -- are the scene's, as is the
	// `currentlyScrollableAndZoomable()` refusal one level up (`clip_view.cpp:256-258`).
	//
	// `:32` runs before all of that, on every horizontal turn, and is unconditional.
	stop_following();

	int32_t oldXZoom = x_zoom;
	int32_t zoomMagnitude = -offset;
	uint32_t newZoom = zoomMagnitude == -1 ? oldXZoom >> 1 : oldXZoom << 1;
	uint32_t clipLengthMaxZoom = max_zoom;

	// When this timer is set, we may need to wait a bit before allowing zooming
	if (delay_zoom_until_ms != 0) {
		// `util::infinite_a_lt_b` (`util/misc.h:100-102`), transcribed for the same reason
		// `divide_round_negative` is: this layer may not include `util/`. Wrap-safe on the shell's
		// millisecond clock exactly as legacy's is on `AudioEngine::audioSampleTimer`.
		bool isWithinZoomDelay = static_cast<int32_t>(now_ms - delay_zoom_until_ms) < 0;

		// Prevent further zooming in the delayed direction (see below)
		if (isWithinZoomDelay && (delay_zoom_magnitude == zoomMagnitude)) {
			return ZoomOutcome::Delayed;
		}

		delay_zoom_until_ms = 0;
	}

	// Prevent quickly scrolling past the clip's max zoom level
	if (newZoom == clipLengthMaxZoom && delay_zoom_until_ms == 0) {
		delay_zoom_until_ms = now_ms + kShortPressMs;
		delay_zoom_magnitude = static_cast<int8_t>(zoomMagnitude);
	}

	// Constrain to zoom limits. Note that the delay above was armed BEFORE this refusal, and legacy
	// arms it the same way round (`:183-186` then `:189-192`).
	if ((zoomMagnitude == -1 && oldXZoom <= 3)
	    || (zoomMagnitude == 1 && oldXZoom >= std::min<uint32_t>(clipLengthMaxZoom * 16, kMaxZoom))) {
		return ZoomOutcome::Refused;
	}

	x_zoom = newZoom;
	// `:195` computes the new scroll and `initiateXZoom` (`:312`) is what actually stores it. The
	// division is unsigned -- see the transcription note at the top of this file.
	int32_t newScroll = x_scroll / (newZoom * kDisplayWidth) * (newZoom * kDisplayWidth);
	x_scroll = newScroll;
	return ZoomOutcome::Zoomed;
}

bool TimelineState::scroll(int32_t offset, uint32_t max_length) {
	// `TimelineView::horizontalEncoderAction`'s scroll arm (`timeline_view.cpp:206-222`). The gates
	// at `:203` and `:206` are the scene's, and so is `displayScrollPos()` at `:221` -- which sits
	// outside the `if` at `:216`, so legacy shows the position even when nothing moved.
	//
	// `:32` runs before all of that, on every horizontal turn, and is unconditional.
	stop_following();

	int32_t newXScroll = x_scroll + offset * x_zoom * kDisplayWidth;

	// Make sure we don't scroll too far left
	if (newXScroll < 0) {
		newXScroll = 0;
	}

	// Make sure we don't scroll too far right
	if (newXScroll < max_length || offset < 0) {
		if (newXScroll != x_scroll) {
			// `initiateXScroll` (`:268-283`), state half.
			x_scroll = newXScroll;
			return true;
		}
	}
	return false;
}

bool TimelineState::zoom_to_max(uint32_t max_zoom, bool in_only) {
	// `TimelineView::zoomToMax` (`timeline_view.cpp:286-302`), with `initiateXZoom`'s scroll store
	// (`:312`) folded in and its animation dropped. `howMuchMoreMagnitude` (`:296`) is not
	// transcribed: its only consumer is `PadLEDs::zoomMagnitude`, which is the animation.
	uint32_t maxZoom = max_zoom;
	uint32_t oldZoom = x_zoom;
	if (maxZoom != oldZoom && (!in_only || maxZoom < oldZoom)) {
		x_zoom = maxZoom;
		int32_t newScroll = x_scroll / (maxZoom * kDisplayWidth) * (maxZoom * kDisplayWidth);
		x_scroll = newScroll;
		return true;
	}
	return false;
}

bool TimelineState::scroll_right_to_end_of_length_if_necessary(int32_t max_length) {
	// `TimelineView::scrollRightToEndOfLengthIfNecessary` (`timeline_view.cpp:331-343`).
	if (pos_from_square(kDisplayWidth) < max_length) {
		uint32_t displayLength = x_zoom * kDisplayWidth;
		x_scroll = (max_length - 1) / displayLength * displayLength;
		return true;
	}
	return false;
}

bool TimelineState::scroll_left_if_too_far_right(int32_t max_length) {
	// `TimelineView::scrollLeftIfTooFarRight` (`timeline_view.cpp:345-353`).
	if (pos_from_square(0) >= max_length) {
		x_scroll = x_scroll - x_zoom * kDisplayWidth;
		return true;
	}
	return false;
}

bool TimelineState::scroll_for_linear_recording(int32_t new_scroll, uint32_t max_length) {
	// `ClipNavigationTimelineView::horizontalScrollForLinearRecording`
	// (`clip_navigation_timeline_view.cpp:36-51`). Both of legacy's arms (`:41` and `:44-45`) store
	// the same scroll; only the animation and the redraw request differ, and neither is this
	// layer's. The `displayScrollPos()` at `:48` is inside the length guard and outside the arm
	// choice -- but it is ALSO inside `if (!display->hasPopup())` (`:47`), so a true return means
	// "legacy would have CONSIDERED showing the scroll position", not "legacy showed it". A caller
	// that shows one unconditionally will show a popup legacy suppresses.
	if (new_scroll < max_length) {
		x_scroll = new_scroll;
		return true;
	}
	return false;
}

void TimelineState::toggle_triplets() {
	// `TimelineView::tripletsButtonPressed` (`timeline_view.cpp:355-363`), minus `uiNeedsRendering`
	// and `setTripletsLEDState()`.
	triplets_on = !triplets_on;
	if (triplets_on) {
		triplets_level = x_zoom * 4 / 3;
	}
}

// --- tick square ------------------------------------------------------------------------------

TimelineState::TickSquare TimelineState::tick_square(int32_t live_pos, const TickSquareGates& gates) {
	// `ClipView::getTickSquare` (`clip_view.cpp:354-389`).
	TickSquare out;
	out.square = square_from_pos(live_pos);

	// See if we maybe want to do an auto-scroll
	if (gates.recording_linearly) {
		if (out.square == kDisplayWidth && gates.ui_idle
		    && (gates.clip_unarmed || scroll_before_following != kNotFollowing)) {

			if (scroll_before_following == kNotFollowing) {
				scroll_before_following = x_scroll;
			}

			int32_t newXScroll = x_scroll + x_zoom * kDisplayWidth;
			out.scrolled = scroll_for_linear_recording(newXScroll, gates.max_length);
		}
	}

	// Or if not, cancel following scrolling along, and go back to where we started
	else {
		if (scroll_before_following != kNotFollowing) {
			int32_t newXScroll = scroll_before_following;
			scroll_before_following = kNotFollowing;

			// `:382` compares the restored SCROLL against the ZOOM. That is legacy's line, not a
			// transcription slip -- see the note at the top of this file.
			if (newXScroll != static_cast<int32_t>(x_zoom)) {
				out.scrolled = scroll_for_linear_recording(newXScroll, gates.max_length);
			}
		}
	}

	return out;
}

} // namespace deluge::scenes::timeline
