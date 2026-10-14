#pragma once
#include <cstdint>
#include <optional>

namespace deluge::ui {

/// @brief One per surface: decides whether it needs to render another frame.
///
/// Widgets with motion call request_repaint_after(); navigation calls request_repaint().
class RepaintGate {
public:
	/// @brief Ask for a frame as soon as possible (e.g. in response to input this tick).
	void request_repaint() { requested_ = true; }
	/// @brief Ask for a frame no later than \p ms milliseconds from \p now_ms.
	///
	/// @note If a deadline is already armed for sooner, the earlier one wins.
	/// @param ms     How far in the future, in milliseconds, to arm the deadline.
	/// @param now_ms The current time, in milliseconds.
	void request_repaint_after(uint32_t ms, uint32_t now_ms);
	/// @brief Does the surface need to render a frame right now? Consumes the pending request
	///        and/or deadline if so.
	/// @param now_ms     The current time, in milliseconds.
	/// @param had_input  Whether input arrived this tick.
	/// @param model_dirty Whether the underlying model changed independently of input.
	/// @return True if a frame is due: the first call ever, an explicit request, input, a dirty
	///         model, or an armed deadline that has passed.
	bool needs_frame(uint32_t now_ms, bool had_input, bool model_dirty); // consumes the request
	/// @brief The currently armed repaint deadline, if any.
	/// @return The deadline in milliseconds, or `std::nullopt` if none is armed.
	std::optional<uint32_t> next_deadline() const { return deadline_; }

private:
	bool first_ = true;
	bool requested_ = false;
	std::optional<uint32_t> deadline_;
};

} // namespace deluge::ui
