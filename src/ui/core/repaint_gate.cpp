#include "ui/core/repaint_gate.h"

namespace deluge::ui {

void RepaintGate::request_repaint_after(uint32_t ms, uint32_t now_ms) {
	const uint32_t due = now_ms + ms;
	if (!deadline_ || static_cast<int32_t>(due - *deadline_) < 0) {
		deadline_ = due;
	}
}

bool RepaintGate::needs_frame(uint32_t now_ms, bool had_input, bool model_dirty) {
	bool due = deadline_ && static_cast<int32_t>(now_ms - *deadline_) >= 0;
	bool fire = first_ || requested_ || had_input || model_dirty || due;
	if (fire) {
		first_ = false;
		requested_ = false;
		if (due) {
			deadline_.reset();
		}
	}
	return fire;
}

} // namespace deluge::ui
