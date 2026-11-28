#include "ui/views/oled/keyboard.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/keys.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/values.h"

#include <algorithm>

namespace deluge::ui::views::oled {

void keyboard_playing(widgets::oled::OledFrame& f, const KeyboardPlaying& d) {
	constexpr int32_t kTopY = 3;
	detail::text_pair(f, kTopY, {d.instrument, d.scale});
	detail::big_text(f, d.chord);
	detail::text_centred(f, detail::last_row_y(f), d.notes);
}

void scale_held(widgets::oled::OledFrame& f, const ScaleHeld& d) {
	widgets::oled::title_note(f, d.scale, d.count);
	const gfx::oled::Rect body = detail::body_rect(f, 0);
	widgets::oled::octave(f, {1, body.y, std::max(0, body.w - 2), body.h}, d.mask, d.root);
}

void velocity_drums(widgets::oled::OledFrame& f, const VelocityDrums& d) {
	widgets::oled::title(f, d.drum);
	int32_t v = std::clamp(d.velocity, 0, 127);
	// a view draws and never reads input: the value goes in a region granted none
	f.region(f.bounds(), false, [&](widgets::oled::OledFrame& g) {
		widgets::oled::int_value(g, g.bounds(), v, {0, 127}, {.continuous = true}, false);
	});
}

} // namespace deluge::ui::views::oled
