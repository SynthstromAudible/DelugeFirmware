#pragma once
#include "shell/surfaces.h"

#include <cstdint>
#include <span>

namespace deluge::ui::shell {

/// @brief A sink for the `Main` OLED surface's bytes, wired by the firmware to the HIDSysex
///        MIDI display bridge. `nullptr` (the default) means no mirror is wired -- the case for
///        every board and every test that never calls `Shell::set_mirror`.
/// @param bytes The panel's 1bpp page-major framebuffer, as `OledSurface::canvas.bytes()`.
/// @param w     Panel width in pixels.
/// @param h     Panel height in pixels.
using MirrorSink = void (*)(std::span<const uint8_t> bytes, uint16_t w, uint16_t h);

/// @brief Mirrors the `Main` surface's blits out over `sink`, throttled the way legacy throttles
///        the sysex display mirror.
///
/// Port of `HIDSysex::sendDisplayIfChanged` (`hid/hid_sysex.cpp:65-80`): every real blit there
/// re-arms `TimerName::SYSEX_DISPLAY` at 100 ms (`:75`, `uiTimerManager.setTimer(TimerName::
/// SYSEX_DISPLAY, 100)`) when the mirror is congested, so the mirror never goes out more than
/// once every 100 ms. There is no timer thread here: `Shell::flush` calls `after_blit` straight
/// from the tick that produced a real blit, and the interval is checked against that tick's own
/// clock rather than a re-armed callback.
struct Mirror {
	/// The sink to mirror through; `nullptr` means "not wired".
	MirrorSink sink = nullptr;
	/// Legacy's `SYSEX_DISPLAY` throttle interval (`HIDSysex::sendDisplayIfChanged`,
	/// `hid_sysex.cpp:75`).
	uint32_t min_interval_ms = 100;
	/// The clock the last mirrored frame went out at; meaningless until `sent_once`.
	uint32_t last_ms = 0;
	/// Whether a frame has ever gone out -- so the very first blit is never held back waiting on
	/// an interval measured from a `last_ms` that is really just its zero-initialized default.
	bool sent_once = false;

	/// @brief Mirror \p s's canvas if a sink is wired and the throttle interval has elapsed.
	/// @param s      The surface that was just blitted; the caller filters to `SurfaceRole::Main`
	///               (`HIDSysex` mirrors the one main panel, not every OLED a board might have).
	/// @param now_ms The tick's wall clock.
	void after_blit(const OledSurface& s, uint32_t now_ms) {
		if (sink == nullptr) {
			return;
		}
		if (sent_once && now_ms - last_ms < min_interval_ms) {
			return;
		}
		sink(s.canvas.bytes(), s.geometry.width, s.geometry.height);
		last_ms = now_ms;
		sent_once = true;
	}
};

} // namespace deluge::ui::shell
