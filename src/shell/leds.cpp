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
		// `setLedState(led, returnToState)` (`indicator_leds.cpp:122-126`), and every caller that
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
