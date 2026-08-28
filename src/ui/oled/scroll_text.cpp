#include "ui/oled/scroll_text.h"

#include "ui/core/metrics.h"
#include "ui/oled/text.h"

#include <algorithm>

namespace deluge::ui::oled {

using gfx::oled::FontId;
using gfx::oled::Rect;
using gfx::oled::string_width_px;

Response scroll_text(OledFrame& f, Rect rect, std::string_view s, FontId font, ScrollText& state, bool allow_motion,
                     bool highlight, int32_t clear_pad_x) {
	const int32_t height = text_height_for(font);
	const int32_t spacing = spacing_x_for(font);
	const int32_t width = string_width_px(s, height);

	// The label or its box changed since the last call (or this is the very first call a
	// fresh ScrollText ever sees, since both *_last fields default to 0): treat it as a
	// newly-appeared label, exactly as a fresh legacy setupSideScroller call would.
	const bool changed = state.text_w_last != width || state.rect_w_last != rect.w;
	state.text_w_last = static_cast<uint16_t>(width);
	state.rect_w_last = static_cast<uint16_t>(rect.w);

	if (width <= rect.w) {
		// Fits: not actively scrolling. Legacy resets scroller->pos to 0 (and, for the same
		// "not scrolling" reason, sideScrollerDirection to 1) whenever the widget isn't
		// scrolling (oled.cpp:975-981, 1020-1021) -- applied here whenever there's no
		// scrolling to do at all, so a label that shrinks to fit starts clean instead of
		// carrying a stale mid-scroll offset into a render that never scrolls it back.
		state.offset = 0;
		state.dir = 1;
	}
	else if (!allow_motion) {
		// legacy: playback running freezes the label at its start and forces the direction
		// forward (oled.cpp:1020-1021).
		state.offset = 0;
		state.dir = 1;
	}
	else {
		const uint32_t now = f.now_ms();
		const int32_t max_offset = width - rect.w;
		if (changed) {
			// legacy setupSideScroller (oled.cpp:913-914): a freshly-appeared or resized label
			// arms the *first* tick to the same kScrollTime pause used at the ping-pong ends,
			// not an immediate 15/5ms step.
			state.offset = 0;
			state.dir = 1;
			state.next_step_ms = now + kScrollTime;
		}
		// Wrap-safe compare, matching RepaintGate (ui/core/repaint_gate.cpp) and the grid's
		// gesture timing: `now` is a free-running millisecond counter that wraps every ~49.7 days,
		// and a plain `now >= next_step_ms` stalls the scroller for the rest of the epoch when it
		// does. The signed difference is correct across the wrap for any interval < 2^31 ms.
		else if (static_cast<int32_t>(now - state.next_step_ms) >= 0) {
			state.offset += state.dir;
			if (state.offset <= 0 || state.offset >= max_offset) {
				state.offset = static_cast<int16_t>(std::clamp<int32_t>(state.offset, 0, max_offset));
				state.dir = static_cast<int8_t>(-state.dir);
				state.next_step_ms = now + kScrollTime;
			}
			else {
				state.next_step_ms = now + (state.dir > 0 ? kScrollStepForwardMs : kScrollStepBackMs);
			}
		}
		// Same reasoning: an unsigned `next_step_ms - now` across a wrap is a ~49-day deadline.
		// The delay is always small and non-negative here (the branch above just set it), so the
		// signed difference is the honest one; clamp at 0 in case a caller's clock jumped past it.
		const int32_t delay = static_cast<int32_t>(state.next_step_ms - now);
		f.gate().request_repaint_after(static_cast<uint32_t>(std::max<int32_t>(delay, 0)), now);
	}

	// clear_pad_x widens only the cleared rect -- legacy's endX += 4 (oled.cpp:984-992), which
	// never touches drawString's own end_x, invertArea's span, or the scroll bounds (see
	// scroll_text.h's doc comment).
	Rect clear_rect = rect;
	clear_rect.w += clear_pad_x;
	f.clear(clear_rect);
	f.glyphs(s, rect.x, rect.y, spacing, height, state.offset, rect.right(), false);
	if (highlight) {
		f.invert(rect);
	}
	return {};
}

} // namespace deluge::ui::oled
