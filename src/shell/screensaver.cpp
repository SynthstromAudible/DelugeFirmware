#include "shell/screensaver.h"

namespace deluge::ui::shell {

Screensaver::Screensaver(SceneStack& stack, uint32_t idle_ms, uint32_t seed)
    : stack_(stack), idle_ms_(idle_ms), seed_(seed) {
}

void Screensaver::note_activity(uint32_t now_ms) {
	if (active_) {
		// Legacy never restores anything on wake either: the screensaver never wrote to the scene
		// beneath it, so waking it is simply "stop showing" (`screensaver.cpp:86-89`). Popping by
		// kind rather than a bare "pop the top" also takes down anything a caller pushed above the
		// screensaver while it was up.
		stack_.pop(SceneKind::Screensaver);
		active_ = false;
	}
	if (idle_ms_ == 0) {
		return; // The `ScreensaverMode::OFF` equivalent: never re-armed.
	}
	deadline_ms_ = now_ms + idle_ms_;
	armed_ = true;
}

void Screensaver::tick(uint32_t now_ms, uint32_t /*dt_ms*/) {
	now_ms_ = now_ms;
	if (idle_ms_ == 0) {
		return;
	}
	if (!armed_) {
		// No `note_activity()` has run yet: idle counts from the first tick, the same as legacy
		// arming its timer at boot rather than treating "never touched" as "already idle forever".
		deadline_ms_ = now_ms + idle_ms_;
		armed_ = true;
		return;
	}
	// Difference form, not `now_ms >= deadline_ms_`: the deadline is an absolute clock that wraps
	// at ~49.7 days of uptime, and an absolute compare across that wrap reads a freshly armed
	// (wrapped, therefore small) deadline as long past and pushes the screensaver over the view the
	// user is actively using. The same form `RepaintGate::needs_frame` (`ui/core/repaint_gate.cpp:13`),
	// `Mirror::due` and every `InputLowering` hold comparison already use.
	if (!active_ && !refused_ && static_cast<int32_t>(now_ms - deadline_ms_) >= 0) {
		// `arg` is deterministic in `seed_` alone, so two services built with the same seed push
		// the scene with the same instantiation argument every time.
		active_ = stack_.push(SceneId{SceneKind::Screensaver, static_cast<uint16_t>(seed_ & 0xffffu)});
		if (!active_) {
			// The stack refused it -- because `SceneKind::Screensaver` names no registered scene
			// (the shipped registry does register one; a profile or spec need not). Latch off
			// rather than retrying: the deadline is still past on the next tick, so an unlatched
			// retry is an unbounded loop that pushes, fails, and (before the `SceneStack::push`
			// fix above it) invalidated every gate, once per tick, for ever. See this class's own
			// doc.
			refused_ = true;
			push_refused_++;
		}
	}
}

std::optional<uint32_t> Screensaver::next_timer() const {
	if (idle_ms_ == 0 || active_ || refused_ || !armed_) {
		return std::nullopt;
	}
	// Difference form again, for the same wrap reason as `tick` above: an absolute
	// `deadline_ms_ > now_ms_` reads a wrapped deadline as already past and reports 0, which pins
	// `Shell::next_wake()` to now and stops the scheduler ever sleeping.
	const int32_t remaining = static_cast<int32_t>(deadline_ms_ - now_ms_);
	return remaining > 0 ? static_cast<uint32_t>(remaining) : 0u;
}

} // namespace deluge::ui::shell
