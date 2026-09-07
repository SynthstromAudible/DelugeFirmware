#include "shell/leds.h"

namespace deluge::ui::shell {

bool led_is_on(const LedState& s, uint32_t now_ms) {
	switch (s.kind) {
	case LedState::Kind::On:
		return true;
	case LedState::Kind::Off:
		return false;
	case LedState::Kind::Blink:
		// The alert deadline wins: legacy's `blinksLeft` countdown ended with
		// `setLedState(led, returnToState)` (`indicator_leds.cpp:122-128`), and every caller that
		// used it passed an LED that was meant to be *on* afterwards -- `indicateAlertOnLed`
		// blinks and leaves it lit. `until_ms == 0` is "blink for ever", which is what a plain
		// `blinkLed(led)` (`numBlinks = 255`) did.
		//
		// Difference form, not `now_ms >= s.until_ms`: `until_ms` is an absolute clock that wraps
		// at ~49.7 days of uptime, and an absolute compare would read a blink armed across the wrap
		// as already finished and latch it solid-on instead of blinking. Same form as
		// `RepaintGate::needs_frame` (`ui/core/repaint_gate.cpp:13`).
		if (s.until_ms != 0 && static_cast<int32_t>(now_ms - s.until_ms) >= 0) {
			return true;
		}
		// A period of zero has no phase to be in; dividing by it would trap. Read it as lit, which
		// is the state the declaration is asking to be seen in.
		if (s.period_ms == 0) {
			return true;
		}
		return ((now_ms / s.period_ms) & 1u) == 0;
	}
	return false;
}

std::optional<uint32_t> led_next_change(const LedState& s, uint32_t now_ms) {
	if (s.kind != LedState::Kind::Blink) {
		return std::nullopt; // `On` and `Off` never change on their own.
	}
	// Already latched solid by the alert deadline, or a period with no phase to be in -- both read
	// lit for ever after, so there is nothing to wake for. Same difference form as `led_is_on`.
	if (s.until_ms != 0 && static_cast<int32_t>(now_ms - s.until_ms) >= 0) {
		return std::nullopt;
	}
	if (s.period_ms == 0) {
		return std::nullopt;
	}
	// The next half-period boundary. `now_ms % period` is 0 exactly on a boundary, which makes this
	// a full period rather than zero -- correct, because the flip at `now_ms` has already been
	// rendered by the caller that asked.
	uint32_t next = s.period_ms - (now_ms % s.period_ms);
	// The alert deadline is a change too: at it the LED goes solid on, whatever phase it was in.
	if (s.until_ms != 0) {
		const uint32_t to_deadline = s.until_ms - now_ms;
		if (to_deadline < next) {
			next = to_deadline;
		}
	}
	return next;
}

void LedCompose::compose(LedFrame& out, std::span<Service* const> services,
                         std::span<Scene* const> stack_bottom_to_top) {
	for (Service* service : services) {
		if (service != nullptr) {
			service->leds(out);
		}
	}
	for (Scene* scene : stack_bottom_to_top) {
		if (scene != nullptr) {
			scene->leds(out);
		}
	}
}

bool led_send_needed(const LedSurface& s, size_t board_led) {
	if (board_led >= LedFrame::kMaxEntries || !s.desired_declared[board_led]) {
		return false;
	}
	return !s.sent_once[board_led] || s.last_on[board_led] != s.desired_on[board_led];
}

} // namespace deluge::ui::shell
