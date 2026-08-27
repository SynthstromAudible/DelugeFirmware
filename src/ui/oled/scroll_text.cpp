#include "ui/oled/scroll_text.h"

#include "ui/core/metrics.h"
#include "ui/oled/text.h"

#include <algorithm>

namespace deluge::ui::oled {

using gfx::oled::FontId;
using gfx::oled::Rect;
using gfx::oled::string_width_px;

Response scroll_text(OledFrame& f, Rect rect, std::string_view s, FontId font, ScrollText& state, bool allow_motion,
                     bool highlight) {
	const int32_t height = text_height_for(font);
	const int32_t spacing = spacing_x_for(font);
	const int32_t width = string_width_px(s, height);

	if (width > rect.w) {
		if (!allow_motion) {
			// legacy: playback running freezes the label at its start.
			state.offset = 0;
		}
		else {
			const uint32_t now = f.input.now_ms;
			const int32_t max_offset = width - rect.w;
			if (state.next_step_ms == 0) {
				// Not yet armed -- OLED::setupSideScroller schedules the first tick without
				// moving the label; this call plays that role the first time the string is
				// found not to fit.
				state.next_step_ms = now + (state.dir > 0 ? kScrollStepForwardMs : kScrollStepBackMs);
			}
			else if (now >= state.next_step_ms) {
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
			f.gate.request_repaint_after(state.next_step_ms - now, now);
		}
	}

	f.clear(rect);
	f.glyphs(s, rect.x, rect.y, spacing, height, state.offset, rect.right(), false);
	if (highlight) {
		f.invert(rect);
	}
	return {};
}

} // namespace deluge::ui::oled
