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

/// @file
/// @brief The automation view over an audio clip: the clip's two length edits, with the automation view's own scroll,
///        zoom and popups, and its play cursor and sidebar.

#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/l10n/l10n.h"
#include "gui/views/automation_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/action/action.h"
#include "model/clip/audio_clip.h"
#include "model/edits/clip_address_for.h"
#include "model/edits/edit.h"
#include "model/edits/resize_audio_clip.h"
#include "model/song/song.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace {

using Kind = deluge::edits::ResizeAudioClip::Kind;

/// @brief Apply one resize of the current clip as one undo step.
/// @return The step its length was logged in, or null; nullopt when the edit was refused.
std::optional<Action*> resize(AudioClip& clip, Kind kind, int32_t new_length, int32_t old_length) {
	const std::optional<deluge::project::ClipAddress> at = deluge::edits::clipAddressFor(clip);
	if (!at) {
		return std::nullopt;
	}
	deluge::edits::ResizeAudioClip edit{*at, kind, new_length, old_length};
	deluge::edits::Context ctx{*currentSong, currentSong->project};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		return std::nullopt;
	}
	return edit.lengthAction();
}

} // namespace

void AutomationView::setAudioClipLengthToSampleLength(AudioClip& clip) {
	const int32_t old_length = clip.loopLength();
	const std::optional<Action*> action = resize(clip, Kind::TO_SAMPLE_LENGTH, 0, old_length);
	if (!action) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NO_SAMPLE));
		return;
	}
	const int32_t new_length = clip.loopLength();
	if (new_length != old_length) {
		// After a shorten: scroll left if the view is now past the end, failing that zoom in to the new limit.
		if (new_length < old_length && new_length > 0 && !scrollLeftIfTooFarRight(new_length)) {
			(void)zoomToMax(true);
		}
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
		displayNumberOfBarsAndBeats(new_length, currentSong->persistentView.xZoom[NAVIGATION_CLIP], false, "LONG");
		if (*action != nullptr) {
			(*action)->xScrollClip[AFTER] = currentSong->persistentView.xScroll[NAVIGATION_CLIP];
		}
	}
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_LENGTH_ADJUSTED));
}

ActionResult AutomationView::editAudioClipLengthWithoutTimestretching(AudioClip& clip, int32_t offset) {
	// A take recorded with no clock cannot be resized.
	if (!clip.currentlyScrollableAndZoomable()) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CANT_EDIT_LENGTH));
		return ActionResult::DEALT_WITH;
	}
	// Scrolled short of the end, the first turn only scrolls there.
	if (scrollRightToEndOfLengthIfNecessary(clip.loopLength())) {
		return ActionResult::DEALT_WITH;
	}

	// The new length from the squares, as a clip view's length turn computes it.
	const auto old_length = static_cast<uint32_t>(clip.loopLength());
	bool right_on_square = false;
	const int32_t end_square = getSquareFromPos(static_cast<int32_t>(old_length), &right_on_square);
	uint32_t new_length = 0;
	if (offset > 0) {
		new_length = static_cast<uint32_t>(getPosFromSquare(end_square) + getLengthExtendAmount(end_square));
		if (new_length > static_cast<uint32_t>(kMaxSequenceLength)) {
			return ActionResult::DEALT_WITH;
		}
	}
	else {
		new_length = right_on_square ? old_length - static_cast<uint32_t>(getLengthChopAmount(end_square))
		                             : static_cast<uint32_t>(getPosFromSquare(end_square));
		if (static_cast<int32_t>(new_length) <= 0) {
			return ActionResult::DEALT_WITH;
		}
	}

	// The length and the sample's end together, so the audio keeps its speed.
	const std::optional<Action*> action =
	    resize(clip, Kind::NO_STRETCH, static_cast<int32_t>(new_length), static_cast<int32_t>(old_length));
	if (offset > 0) {
		if (!scrollRightToEndOfLengthIfNecessary(static_cast<int32_t>(new_length))) {
			uiNeedsRendering(this, 0xFFFFFFFF, 0);
		}
	}
	else if (!scrollLeftIfTooFarRight(static_cast<int32_t>(new_length)) && !zoomToMax(true)) {
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}

	displayNumberOfBarsAndBeats(new_length, currentSong->persistentView.xZoom[NAVIGATION_CLIP], false, "LONG");
	if (action && *action != nullptr) {
		(*action)->xScrollClip[AFTER] = currentSong->persistentView.xScroll[NAVIGATION_CLIP];
	}
	return ActionResult::DEALT_WITH;
}

void AutomationView::audioClipGraphicsRoutine() {
	if (isUIModeActive(UI_MODE_AUDIO_CLIP_COLLAPSING)) {
		return;
	}

	int32_t newTickSquare;

	if (!playbackHandler.playbackState || !currentSong->isClipActive(getCurrentClip())
	    || currentUIMode == UI_MODE_EXPLODE_ANIMATION || currentUIMode == UI_MODE_IMPLODE_ANIMATION
	    || playbackHandler.ticksLeftInCountIn) {
		newTickSquare = 255;
	}
	// Tempoless or arranger recording
	else if (!playbackHandler.isEitherClockActive()
	         || (currentPlaybackMode == &arrangement && getCurrentClip()->getCurrentlyRecordingLinearly())) {
		newTickSquare = kDisplayWidth - 1;
		if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
			audioClipNeedsRendering();
		}
	}
	else {
		newTickSquare = getTickSquare();
		if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
			audioClipNeedsRendering();
		}
		if (newTickSquare < 0 || newTickSquare >= kDisplayWidth) {
			newTickSquare = 255;
		}
	}

	if (PadLEDs::flashCursor != FLASH_CURSOR_OFF
	    && (newTickSquare != audioClipLastTickSquare || audioClipMustRedrawTickSquares)) {
		uint8_t tickSquares[kDisplayHeight];
		memset(tickSquares, newTickSquare, kDisplayHeight);

		std::array<uint8_t, 8> coloursArray = {0};
		if (getCurrentClip()->getCurrentlyRecordingLinearly()) {
			coloursArray.fill(2);
		}

		PadLEDs::setTickSquares(tickSquares, coloursArray.data());

		audioClipLastTickSquare = newTickSquare;
		audioClipMustRedrawTickSquares = false;
	}
}

void AutomationView::audioClipNeedsRendering() {
	// A take recording linearly grows under the cursor; the pads redraw unless a scroll or zoom animation has them.
	switch (currentUIMode) {
	case UI_MODE_HORIZONTAL_SCROLL:
	case UI_MODE_HORIZONTAL_ZOOM:
		break;
	default:
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
}
