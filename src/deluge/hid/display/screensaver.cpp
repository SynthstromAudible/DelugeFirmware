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

#include "hid/display/screensaver.h"
#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h"

#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_runtime.h"

namespace deluge::hid::display {

bool Screensaver::active_ = false;
bool Screensaver::frameDirty_ = false;
oled_canvas::Canvas Screensaver::canvas_;
ui::widgets::oled::Starfield Screensaver::starfield_;
ui::widgets::oled::Rainfall Screensaver::rainfall_;

// Rainfall repeats the panel dimensions, and Starfield derives its projection centre from them,
// rather than including cpu_specific.h -- it is target-only and would break the host specs. This
// file does see the real macros, so it is where the two are held to them.
static_assert(ui::widgets::oled::Rainfall::kWidth == OLED_MAIN_WIDTH_PIXELS);
static_assert(ui::widgets::oled::Rainfall::kHeight == OLED_MAIN_HEIGHT_PIXELS);
static_assert(ui::widgets::oled::Rainfall::kTopmost == OLED_MAIN_TOPMOST_PIXEL);

// kCentreY is deliberately the truncated integer division (26.0f), not the true centre (26.5) --
// project() truncates float positions the same way, so this matches what actually renders.
static_assert(ui::widgets::oled::Starfield::kCentreX == OLED_MAIN_WIDTH_PIXELS / 2.0f);
static_assert(ui::widgets::oled::Starfield::kCentreY == (OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_HEIGHT_PIXELS) / 2);

/// Frame interval for the animated modes (STARSCAPE, DELUGE). For comparison, scrolling text
/// re-arms at 15ms and 5ms (see OLED::scrollingAndBlinkingTimerEvent), so this is the lighter load.
constexpr int32_t kFrameIntervalMS = 50;

/// Blank re-check interval while already showing. Blank has no per-frame animation
/// to drive, so this exists solely so a popup or working animation that appears
/// while we're showing gets noticed -- without pushing an unchanged frame every tick.
constexpr int32_t kInhibitRecheckMS = 1000;

constexpr int32_t kMillisecondsPerMinute = 60000;

namespace {
/// @brief True for the modes that redraw every frame, as opposed to BLANK.
constexpr bool isAnimated(ScreensaverMode mode) {
	return mode == ScreensaverMode::STARSCAPE || mode == ScreensaverMode::DELUGE;
}

/// @brief Draw a filled square, clipping it to the panel.
///
/// @note Rows 0-4 are off-panel, hence the clip at OLED_MAIN_TOPMOST_PIXEL rather than zero.
void drawBlock(oled_canvas::Canvas& canvas, int32_t left, int32_t top, int32_t size) {
	for (int32_t dy = 0; dy < size; dy++) {
		for (int32_t dx = 0; dx < size; dx++) {
			const int32_t x = left + dx;
			const int32_t y = top + dy;
			if (x >= 0 && x < OLED_MAIN_WIDTH_PIXELS && y >= OLED_MAIN_TOPMOST_PIXEL && y < OLED_MAIN_HEIGHT_PIXELS) {
				canvas.drawPixel(x, y);
			}
		}
	}
}
} // namespace

void Screensaver::arm() {
	uiTimerManager.setTimer(TimerName::SCREENSAVER, FlashStorage::screensaverTimeoutMinutes * kMillisecondsPerMinute);
}

void Screensaver::noteActivity() {
	if (active_) {
		active_ = false;
		// The screensaver never writes to `main`, so this doesn't need to restore
		// anything -- it just marks the display dirty so the current `main` content
		// gets pushed again.
		OLED::markChanged();
	}

	if (FlashStorage::screensaverMode == ScreensaverMode::OFF) {
		return;
	}

	arm();
}

void Screensaver::timerEvent() {
	// Qualified as ::display: inside namespace deluge::hid::display, unqualified
	// "display" resolves to this very namespace, not the global Display pointer.
	if (FlashStorage::screensaverMode == ScreensaverMode::OFF) {
		return;
	}

	// **Two screensavers, and this is where it is settled.**
	//
	// `shell::Screensaver` has a real timeout (see `settingsChanged()` below), so the shell's
	// service arms and pushes `overlay::ScreensaverScene` on the same idle period this class
	// uses, from the same two settings, woken by the same presses --
	// `Shell::on_board_event`/`on_encoder_edges` call `note_activity()` exactly where
	// `dispatchInputEvent` calls `noteActivity()`. Both would draw: this class renders into its own
	// canvas and calls `OLED::markChanged()`, while the scene renders through the guest's surfaces
	// into `OLED::main`. That is a visibly wrong screen, not a duplicated no-op.
	// `shell::Screensaver` advances only when the shell ticks, and the shell ticks only while
	// `shellHostUI` is open (`ShellHostUI::tickShell`), so the collision is live whenever it is.
	//
	// **The guest owns the panel while it is the current UI, so its own service owns the idle
	// policy there.** The condition is `getCurrentUI()`, not "the guest exists" and not "the guest
	// is anywhere in the hierarchy": with a legacy UI open *over* the guest, that UI owns the panel
	// and the shell is not ticking at all (`openUI` in `gui/ui/ui.cpp` unsets
	// `TimerName::UI_SPECIFIC`), so this class is the only screensaver there and must keep running.
	//
	// **Re-armed, not disarmed**, and woken first -- the same three lines the inhibitor arm below
	// uses, in the same order, and for the same reason: a silent `return` would leave
	// `TimerName::SCREENSAVER` unarmed until the next `noteActivity()`, and this class must be ready
	// the instant the guest closes. `noteActivity()` itself is left alone: it is idempotent against
	// a screensaver that is not showing, and it is what re-arms both sides on a press.
	if (getCurrentUI() == &shellHostUI) {
		if (active_) {
			active_ = false;
			OLED::markChanged();
		}
		arm();
		return;
	}

	// Covers the drawPermanentPopupLookingText() popup class (stem export, session view,
	// automation view) plus the working animation and stem export itself. Re-checked every tick,
	// not just at activation, so one of these appearing while we're showing gets the screen back.
	//
	// Deliberately does NOT cover Display::popupText(..., persistent=true), which sets
	// oledPopupWidth instead: inhibiting on that would let a stuck persistent popup permanently
	// disable burn-in protection, which is worse than the alternative. The popup itself returns on
	// the first input regardless, and the screensaver only appears after minutes of idle, so the
	// window where it could cover one is short.
	if (OLED::isPermanentPopupPresent() || OLED::isWorkingAnimationPresent() || stemExport.processStarted) {
		if (active_) {
			active_ = false;
			OLED::markChanged();
		}
		arm();
		return;
	}

	bool changed = false;
	if (!active_) {
		active_ = true;
		switch (FlashStorage::screensaverMode) {
		case ScreensaverMode::STARSCAPE:
			starfield_.scatter();
			break;
		case ScreensaverMode::DELUGE:
			rainfall_.scatter();
			break;
		default:
			break;
		}
		changed = true;
	}
	else {
		switch (FlashStorage::screensaverMode) {
		case ScreensaverMode::STARSCAPE:
			starfield_.advance();
			changed = true;
			break;
		case ScreensaverMode::DELUGE:
			rainfall_.advance();
			changed = true;
			break;
		default:
			// BLANK while already showing: nothing changed, this tick is only an inhibitor
			// re-check.
			break;
		}
	}

	if (changed) {
		render();
		OLED::markChanged();
	}

	uiTimerManager.setTimer(TimerName::SCREENSAVER,
	                        isAnimated(FlashStorage::screensaverMode) ? kFrameIntervalMS : kInhibitRecheckMS);
}

void Screensaver::settingsChanged() {
	// **Re-supply the shell screensaver's copy of the same two settings.**
	// `deluge::ui::shell::Screensaver` is the policy half of this class, as a `Service`
	// (`ui/shell/screensaver.h`), and `src/ui/shell` may not read `storage/` -- so it holds one number
	// where this class re-reads `FlashStorage` on every arm and every tick, and something has to
	// push a change in. That is what `ui/scenes/registry.h`'s boot-wiring note asked whoever owns boot
	// to do, and this function is the better home than a boot call: it is already the one place a
	// change to either setting funnels through, from all of its writers -- `deluge_boot` (`deluge.cpp`, after
	// `FlashStorage::readSettings()`), the menu's two items (below) and a settings reset
	// (`gui::ui::reapplyLiveSettingsAfterReset`). A boot call would have covered only the first, and the guest's copy
	// would go stale the moment the user changed either setting.
	//
	// The formula is this class's own two reads, spelled as one number: `arm()`'s
	// `screensaverTimeoutMinutes * kMillisecondsPerMinute` and the `ScreensaverMode::OFF` early-outs of
	// `noteActivity()`, `timerEvent()` and this function, which the shell spells as `idle_ms == 0`.
	//
	// **The menu's two items.** They write the `ui/screensaver/mode` and `ui/screensaver/timeout` DEVICE rows, whose
	// store in the device port (`gui/ui/shell_host/device_settings.cpp`) sets the same two `FlashStorage` globals and
	// calls this function, as main's two settings items call it.
	deluge::gui::ui::shellRuntime().shell().set_screensaver_idle_ms(
	    FlashStorage::screensaverMode == ScreensaverMode::OFF
	        ? 0u
	        : static_cast<uint32_t>(FlashStorage::screensaverTimeoutMinutes) * kMillisecondsPerMinute);

	if (active_) {
		active_ = false;
		OLED::markChanged();
	}

	if (FlashStorage::screensaverMode == ScreensaverMode::OFF) {
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		return;
	}

	arm();
}

void Screensaver::render() {
	canvas_.clear();
	frameDirty_ = true;

	switch (FlashStorage::screensaverMode) {
	case ScreensaverMode::STARSCAPE:
		renderStarfield();
		break;
	case ScreensaverMode::DELUGE:
		renderRainfall();
		break;
	default:
		break; // BLANK: a cleared canvas is the whole picture.
	}
}

void Screensaver::renderStarfield() {
	for (size_t i = 0; i < ui::widgets::oled::Starfield::kNumStars; i++) {
		const ui::widgets::oled::Starfield::Projected star = starfield_.project(i);
		drawBlock(canvas_, star.x, star.y, star.size);
	}
}

void Screensaver::renderRainfall() {
	for (size_t drop = 0; drop < ui::widgets::oled::Rainfall::kNumDrops; drop++) {
		for (size_t cell = 0; cell < rainfall_.length_of(drop); cell++) {
			const ui::widgets::oled::Rainfall::Block block = rainfall_.cell_at(drop, cell);
			drawBlock(canvas_, block.x, block.y, block.size);
		}
	}

	// Every few minutes a whole logo falls through the field. It draws through the same clipped
	// block helper as the rain, because it is built from the same shapes.
	if (rainfall_.logo_active()) {
		for (size_t cell = 0; cell < ui::widgets::oled::Rainfall::kLogoCells; cell++) {
			const ui::widgets::oled::Rainfall::Block block = rainfall_.logo_cell_at(cell);
			drawBlock(canvas_, block.x, block.y, block.size);
		}
	}
}

} // namespace deluge::hid::display
