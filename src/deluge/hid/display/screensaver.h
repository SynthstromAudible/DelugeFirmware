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

#include "hid/display/oled_canvas/canvas.h"
#include "hid/display/starfield.h"

namespace deluge::hid::display {

/// Blanks the OLED, or replaces it with a starfield, after a configurable period
/// without physical input. OLED only: 7SEG units never activate it.
///
/// Renders into its own canvas rather than the main one, so the screensaver never
/// writes to `main`: waking up is just "stop overriding and mark dirty". Other UI
/// timers (side-scrollers, the working animation, etc.) can and do keep writing to
/// `main` while we're showing, so whatever they've left there is already current.
class Screensaver {
public:
	/// Called from the input path whenever a physical control is touched. Wakes the
	/// screensaver if it is showing, and re-arms the idle timer either way.
	///
	/// Sits on the input hot path: returns immediately when the feature is off.
	static void noteActivity();

	/// Dispatched from UITimerManager when TimerName::SCREENSAVER fires. Either
	/// activates the screensaver or advances it one animation frame.
	static void timerEvent();

	/// Called after the mode or timeout setting changes: wakes if showing, then
	/// re-arms or cancels the timer for the new settings.
	static void settingsChanged();

	[[nodiscard]] static bool isActive() { return active_; }

	/// True if our canvas changed since the last call, and clears the flag. Lets
	/// sendMainImage() tell "our frame actually changed" apart from "some unrelated
	/// timer (scrolling text, working animation) marked the display dirty while we're
	/// showing" -- the latter must not re-enqueue an unchanged frame indefinitely.
	static bool consumeFrameDirty() {
		bool was = frameDirty_;
		frameDirty_ = false;
		return was;
	}

	/// XXX: DO NOT USE THIS OUTSIDE OF OLED::sendMainImage().
	static oled_canvas::Canvas& getCanvas() { return canvas_; }

private:
	/// Set the idle timer from the configured timeout.
	static void arm();
	/// Draw the current frame into canvas_ and mark it dirty.
	static void render();

	static bool active_;
	static bool frameDirty_;
	static oled_canvas::Canvas canvas_;
	static Starfield starfield_;
};

} // namespace deluge::hid::display
