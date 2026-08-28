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
	if (!active_ && now_ms >= deadline_ms_) {
		// `arg` is deterministic in `seed_` alone, so two services built with the same seed push
		// the scene with the same instantiation argument every time.
		active_ = stack_.push(SceneId{SceneKind::Screensaver, static_cast<uint16_t>(seed_ & 0xffffu)});
	}
}

std::optional<uint32_t> Screensaver::next_timer() const {
	if (idle_ms_ == 0 || active_ || !armed_) {
		return std::nullopt;
	}
	return deadline_ms_ > now_ms_ ? deadline_ms_ - now_ms_ : 0;
}

} // namespace deluge::ui::shell
