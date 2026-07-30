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

/// @brief Blanks the OLED, or replaces it with a starfield, after a configurable period without
///        physical input.
///
/// OLED only: 7SEG units never activate it. All state is static -- there is exactly one screensaver.
///
/// @note It renders into its own canvas rather than the main one, and so never writes to `main`:
///       waking up is just "stop overriding and mark dirty". Other UI timers (side-scrollers, the
///       working animation) can and do keep writing to `main` while the screensaver is showing, so
///       whatever they have left there is already current when it wakes.
class Screensaver {
public:
	/// @name Event entry points
	/// @{

	/// @brief Register that a physical control was touched: wake the screensaver if it is showing,
	///        and re-arm the idle timer either way.
	///
	/// @warning This sits on the input hot path. The wake check runs first, before the mode-is-off
	///          check, so a mode that somehow reached OFF without going through settingsChanged()
	///          can never strand the panel permanently blank.
	static void noteActivity();

	/// @brief Activate the screensaver, or advance it one animation frame.
	///
	/// Dispatched from UITimerManager when TimerName::SCREENSAVER fires.
	static void timerEvent();

	/// @brief Apply a change to the mode or timeout setting: wake if showing, then re-arm or cancel
	///        the timer for the new settings.
	static void settingsChanged();

	/// @}

	/// @name Display integration
	/// @{

	/// @return True while the screensaver is overriding the panel contents.
	[[nodiscard]] static bool isActive() { return active_; }

	/// @brief Test and clear the "our canvas changed" flag.
	///
	/// Lets sendMainImage() tell "our frame actually changed" apart from "some unrelated timer
	/// (scrolling text, working animation) marked the display dirty while we are showing" -- the
	/// latter must not re-enqueue an unchanged frame indefinitely.
	///
	/// @return True if the canvas changed since the last call.
	static bool consumeFrameDirty() {
		bool was = frameDirty_;
		frameDirty_ = false;
		return was;
	}

	/// @brief The canvas the screensaver draws into, which sendMainImage() sends in place of `main`.
	///
	/// @warning DO NOT USE THIS OUTSIDE OF OLED::sendMainImage().
	/// @return The screensaver's own canvas.
	static oled_canvas::Canvas& getCanvas() { return canvas_; }

	/// @}

private:
	/// @brief Set the idle timer from the configured timeout.
	static void arm();
	/// @brief Draw the current frame into canvas_ and mark it dirty.
	static void render();

	static bool active_;
	static bool frameDirty_;
	static oled_canvas::Canvas canvas_;
	static Starfield starfield_;
};

} // namespace deluge::hid::display
