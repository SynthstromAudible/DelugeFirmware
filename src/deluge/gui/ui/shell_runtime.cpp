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

#include "gui/ui/shell_runtime.h"

#include "board_config.h"                 // PLACE_SDRAM_BSS
#include "gui/ui/shell_action_dispatch.h" // hostInhibitsScreensaver
#include "gui/ui/shell_slice_ops.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "libdeluge/scheduler.h"             // getSystemTime()
#include "processing/engines/audio_engine.h" // audioSampleTimer
#include "storage/flash_storage.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>

namespace deluge::gui::ui {

ShellRuntime::ShellRuntime(const deluge::ui::shell::SceneRegistry& registry, const DelugeBoard& board,
                           const deluge::ui::shell::BoardProfile& profile)
    : surfaces_(board, profile), shell_(surfaces_, profile, registry, deluge::ui::shell::Services{}, config_) {
	// `Services{}` and a `ShellConfig` whose only set fields are board facts (`greys_in_send_path`, because legacy's
	// `prepareColour` greys every pad a host sends; `frame_ms`) on purpose, and both stay that way.
	//
	// The three real services each take this shell's own `SceneStack&`, so they cannot exist before it. They are built
	// over `shell()` and installed through `Shell::set_services` from `wireShellGuestServices()`, called at boot and
	// from the guest's `opened()`. This object is a static-init global, so the constructor is not a place a real
	// service (or a real setting) can be reached from at all: `screensaver_idle_ms` is seated through
	// `Shell::set_screensaver_idle_ms` from `Screensaver::settingsChanged()`, and `lowering.hold_time_ms` through
	// `InputLowering::set_hold_time_ms` on every event a host feeds.
	surfaces_.observe(shell_.stack());
}

uint32_t ShellRuntime::nowMs() const {
	return clock_.advance(AudioEngine::audioSampleTimer);
}

uint32_t ShellRuntime::accelerationClockMs() {
	const double seconds = getSystemTime();
	if (!(seconds > 0.0)) {
		// Also the NaN arm: every comparison against a NaN is false, so the negated `>` catches it.
		// Before the scheduler starts there is no elapsed time to report and 0 is the honest answer.
		return 0;
	}
	return static_cast<uint32_t>(static_cast<uint64_t>(seconds * 1000.0));
}

void ShellRuntime::tick(uint32_t now_ms, const ::UI* owner) {
	shell_.set_screensaver_inhibit(hostInhibitsScreensaver());
	shell_.set_acceleration_clock_ms(accelerationClockMs());
	shell_.set_owns_notices(getCurrentUI() == owner);
	const bool outer = std::exchange(ticking_, true);
	shell_.tick(now_ms);
	ticking_ = outer;
}

void ShellRuntime::rearm(uint32_t now_ms, const ::UI* owner) {
	// The guard is on `getCurrentUI()`, not on a list of the paths that open a UI over the host, so it does not go
	// stale when that list does. Returning early cannot strand the host: when the UI opened above it closes,
	// `closeUI` unsets `UI_SPECIFIC` and calls the host's `focusRegained()`, which arms a tick unconditionally.
	if (getCurrentUI() != owner) {
		return;
	}

	std::optional<uint32_t> deadline = shell_.next_wake(now_ms);
	if (shellSlicerBusy()) {
		const uint32_t poll = now_ms + kShellSlicerPollMs;
		deadline = deadline.has_value() && static_cast<int32_t>(*deadline - poll) < 0 ? *deadline : poll;
	}
	if (!deadline.has_value()) {
		// Nothing armed anywhere in the shell -- no gate deadline, no scene or service timer, no
		// declared blink (`Shell::next_wake`). Legacy has already cleared the timer
		// (`UITimerManager::routine`), so leaving it alone *is* "sleep until the next input",
		// and every path that can wake the shell re-arms it (`armTickSoon`).
		//
		// Except one, which no scene can express as a deadline: a press held back by the SD-routine
		// lock is waiting on a fact outside the shell entirely. Poll for it. At `kMinRearmMs` that
		// is slower than main's own retry, which runs from `readButtonsAndPads` on every pass of
		// the main loop (`origin/main:src/deluge/deluge.cpp`), so it is not a new cost -- and it only runs while
		// the lock is actually held.
		if (shell_.lowering().sd_busy()) {
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMinRearmMs);
		}
		return;
	}
	// Absolute deadline minus the clock the tick ran on, in the wrap-safe difference form. A
	// deadline at or before `now` -- which `Shell::next_wake()` reports routinely -- clamps up to
	// `kMinRearmMs` rather than arming zero. See the class's `@par The unit mismatch` and `@par Why the floor is 1 ms`.
	const int32_t in = static_cast<int32_t>(*deadline - now_ms);
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, std::clamp(in, kMinRearmMs, kMaxRearmMs));
}

void ShellRuntime::armTickSoon() {
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMinRearmMs);
}

namespace {
/// @brief Take up the device's hold time, which the settings menu may have moved since the last event: legacy re-reads
///        `FlashStorage::holdTime` on every press.
void syncHoldTime(deluge::ui::shell::Shell& shell) {
	shell.lowering().set_hold_time_ms(
	    static_cast<uint16_t>(static_cast<uint32_t>(FlashStorage::holdTime) * 1000u / kSampleRate));
}
} // namespace

void ShellRuntime::feed(const DelugeInputEvent& ev) {
	syncHoldTime(shell_);
	shell_.on_board_event(ev, nowMs());
}

void ShellRuntime::feedPad(uint8_t x, uint8_t y, int32_t velocity, bool shortcutWithoutShift) {
	syncHoldTime(shell_);
	shell_.on_host_pad(x, y, static_cast<uint8_t>(velocity), shortcutWithoutShift, nowMs());
}

void ShellRuntime::feedDetents(deluge::ui::widgets::core::EncoderRole role, int32_t detents, uint64_t time_us) {
	if (detents == 0) {
		return;
	}
	const std::span<const deluge::ui::widgets::core::EncoderRole> byIndex =
	    shell_.lowering().profile().encoder_by_index;
	const int32_t per = config_.lowering.edges_per_detent > 0 ? config_.lowering.edges_per_detent : 1;
	const int32_t limit = 127 / per;
	const auto edges = static_cast<int8_t>(std::clamp(detents, -limit, limit) * per);
	for (size_t i = 0; i < byIndex.size(); i++) {
		if (byIndex[i] != role) {
			continue;
		}
		shell_.on_encoder_edges(static_cast<uint8_t>(i), edges, nowMs(), time_us);
		return;
	}
}

void ShellRuntime::copyGrid(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], int32_t x0, int32_t x1) {
	if (image == nullptr) {
		return;
	}
	const deluge::ui::gfx::grid::PadGrid& grid = surfaces_.grid().grid();
	const deluge::ui::gfx::grid::GridDimensions dims = grid.dims();
	const int32_t rows = std::min<int32_t>(dims.rows, kDisplayHeight);
	const int32_t last = std::min<int32_t>(x1, dims.cols());
	for (int32_t y = 0; y < rows; y++) {
		if ((whichRows & (1u << y)) == 0) {
			continue;
		}
		for (int32_t x = x0; x < last; x++) {
			image[y][x] = grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
		}
	}
}

void ShellRuntime::copyOled(deluge::hid::display::oled_canvas::Canvas& canvas) {
	if (!surfaces_.canReachMainPanel()) {
		return;
	}
	const deluge::ui::shell::OledSurface* main = surfaces_.oled(deluge::ui::shell::SurfaceRole::Main);
	if (main == nullptr) {
		return;
	}
	const std::span<const uint8_t> src = main->canvas.bytes();
	const std::span<uint8_t> dest = canvas.bytes();
	if (src.size() != dest.size()) {
		return;
	}
	std::copy(src.begin(), src.end(), dest.begin());
}

} // namespace deluge::gui::ui

namespace {
// SDRAM: the shell and its scene stack are menu-time state, never read by the audio tier or an interrupt. The scenes'
// profile, not the shell's: it carries the l10n bridge, so a popup a scene raises by string id shows its text.
PLACE_SDRAM_BSS deluge::gui::ui::ShellRuntime theShellRuntime{
    deluge::ui::scenes::scene_registry(), deluge::ui::shell::kDelugeBoard, deluge::ui::scenes::deluge_profile()};
} // namespace

deluge::gui::ui::ShellRuntime& deluge::gui::ui::shellRuntime() {
	return theShellRuntime;
}
