#pragma once
#include "shell/surfaces.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::shell {

/// @brief A sink for the `Main` OLED surface's bytes, wired by the firmware to the HIDSysex
///        MIDI display bridge. `nullptr` (the default) means no mirror is wired -- the case for
///        every board and every test that never calls `Shell::set_mirror`.
/// @param bytes The panel's 1bpp page-major framebuffer, as `OledSurface::canvas.bytes()`.
/// @param w     Panel width in pixels.
/// @param h     Panel height in pixels.
using MirrorSink = void (*)(std::span<const uint8_t> bytes, uint16_t w, uint16_t h);

/// @brief Mirrors the `Main` surface's blits out over `sink`, throttled to at most one send per
///        `min_interval_ms`.
///
/// **Deliberate simplification, not a literal port.** Legacy's `HIDSysex::sendDisplayIfChanged`
/// (`hid/hid_sysex.cpp:65-80`) mirrors at full display rate whenever the MIDI cable's send buffer
/// has room (`sendBufferSpace() >= 512`), and backs off to retry in 100 ms
/// (`uiTimerManager.setTimer(TimerName::SYSEX_DISPLAY, 100)`, `:75`) only when that buffer is
/// congested -- there is no unconditional cap in legacy. This shell has no MIDI cable to ask, so
/// the brief mandates an unconditional 100 ms floor between sends instead; that is a policy this
/// port chooses, not a fact reproduced from legacy.
///
/// What legacy's backoff *does* guarantee, and what this must too: a frame that arrives inside the
/// throttle window is not lost. Legacy's re-armed timer fires `sendDisplayIfChanged` again and
/// delivers whatever the display last rendered; here, a blit that lands before `min_interval_ms`
/// has elapsed is remembered as `pending` (referencing the surface's own framebuffer storage, which
/// `SurfaceSet` never reallocates, so nothing is copied) and `Shell::flush` retries it every tick
/// via `retry()` until the interval passes -- so a later blit arriving before that retry simply
/// updates which frame `pending` points at, and the *latest* one is what eventually goes out.
struct Mirror {
	/// The sink to mirror through; `nullptr` means "not wired".
	MirrorSink sink = nullptr;
	/// The throttle floor between sends; the brief's port of legacy's 100 ms retry interval
	/// (`HIDSysex::sendDisplayIfChanged`, `hid_sysex.cpp:75`) as an unconditional cap -- see the
	/// class note on why this is a policy choice rather than a literal read of legacy's condition.
	uint32_t min_interval_ms = 100;
	/// The clock the last mirrored frame actually went out at; meaningless until `sent_once`.
	uint32_t last_sent_ms = 0;
	/// Whether a frame has ever gone out -- so the very first blit is never held back waiting on
	/// an interval measured from a `last_sent_ms` that is really just its zero-initialized default.
	bool sent_once = false;
	/// Whether a blit arrived inside the throttle window and is still owed a send.
	bool pending = false;
	/// The most recently blitted frame, while `pending` -- a view over `OledSurface::canvas`'s own
	/// storage (stable for the `SurfaceSet`'s lifetime), not a copy.
	std::span<const uint8_t> pending_bytes;
	uint16_t pending_w = 0, pending_h = 0;

	/// @brief Record that \p s was just blitted: send immediately if the throttle has elapsed,
	///        otherwise remember it as `pending` for `retry()`.
	/// @param s      The surface that was just blitted; the caller filters to `SurfaceRole::Main`
	///               (`HIDSysex` mirrors the one main panel, not every OLED a board might have).
	/// @param now_ms The tick's wall clock.
	void after_blit(const OledSurface& s, uint32_t now_ms) {
		if (sink == nullptr) {
			return;
		}
		if (due(now_ms)) {
			send(s.canvas.bytes(), s.geometry.width, s.geometry.height, now_ms);
			return;
		}
		// Held back by the throttle: remember it (overwriting any earlier pending frame -- only
		// the latest matters) rather than dropping it, so `retry()` can still deliver it.
		pending = true;
		pending_bytes = s.canvas.bytes();
		pending_w = s.geometry.width;
		pending_h = s.geometry.height;
	}

	/// @brief If a frame is pending and the throttle interval has now elapsed, send it.
	///
	/// Called every tick regardless of whether anything blitted this tick: the deferred frame must
	/// still reach the mirror once its deadline passes, even with no new render to trigger
	/// `after_blit` again -- matching legacy's re-armed `SYSEX_DISPLAY` timer firing on its own.
	/// @param now_ms The tick's wall clock.
	void retry(uint32_t now_ms) {
		if (!pending || sink == nullptr || !due(now_ms)) {
			return;
		}
		send(pending_bytes, pending_w, pending_h, now_ms);
	}

	/// @brief When `retry()` should next be tried, for `Shell::next_wake()`.
	/// @return The deadline, or `std::nullopt` while nothing is pending.
	std::optional<uint32_t> next_deadline() const {
		if (!pending) {
			return std::nullopt;
		}
		return sent_once ? last_sent_ms + min_interval_ms : last_sent_ms;
	}

private:
	/// @brief Has the throttle interval elapsed (or never applied)?
	bool due(uint32_t now_ms) const { return !sent_once || now_ms - last_sent_ms >= min_interval_ms; }

	void send(std::span<const uint8_t> bytes, uint16_t w, uint16_t h, uint32_t now_ms) {
		sink(bytes, w, h);
		last_sent_ms = now_ms;
		sent_once = true;
		pending = false;
	}
};

} // namespace deluge::ui::shell
