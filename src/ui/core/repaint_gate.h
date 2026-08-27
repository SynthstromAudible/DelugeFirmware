#pragma once
#include <cstdint>
#include <optional>

namespace deluge::ui {

/// One per surface. Widgets with motion call request_repaint_after; navigation calls request_repaint.
class RepaintGate {
public:
	void request_repaint() { requested_ = true; }
	void request_repaint_after(uint32_t ms, uint32_t now_ms);
	bool needs_frame(uint32_t now_ms, bool had_input, bool model_dirty); // consumes the request
	std::optional<uint32_t> next_deadline() const { return deadline_; }

private:
	bool first_ = true;
	bool requested_ = false;
	std::optional<uint32_t> deadline_;
};

} // namespace deluge::ui
