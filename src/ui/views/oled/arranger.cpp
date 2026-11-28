#include "ui/views/oled/arranger.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/text.h"

#include <algorithm>

namespace deluge::ui::views::oled {

void arranger_resting(widgets::oled::OledFrame& f, const ArrangerResting& d) {
	widgets::oled::title_note(f, d.song, d.position);
	gfx::oled::Rect r = detail::body_rect(f, 0);
	r.h = std::max(0, r.h - 2);
	widgets::oled::overview(f, r, d.lanes, d.units, d.spans, d.window_start, d.window_len, d.playhead);
}

void arranger_clip_held(widgets::oled::OledFrame& f, const ArrangerClipHeld& d) {
	widgets::oled::title_note(f, d.name, d.position);
	detail::text_pair(f, detail::row_y(f, 0), {d.length, d.repeats});
	const gfx::oled::Rect body = detail::body_rect(f, 1);
	constexpr int32_t kStrip = 7;
	const gfx::oled::Rect strip{0, body.y + std::max(0, (body.h - kStrip) / 2), body.w, std::min(kStrip, body.h)};
	widgets::oled::overview(f, strip, 1, d.units, d.lane, 0, 0, -1);
}

} // namespace deluge::ui::views::oled
