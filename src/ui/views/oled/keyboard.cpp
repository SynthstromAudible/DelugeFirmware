#include "ui/views/oled/keyboard.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/oled/keys.h"
#include "ui/widgets/oled/popup.h"
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
	const int32_t w = f.bounds().w;
	const int32_t h = f.bounds().h;
	// the digits where a big value sits: `int_value` draws them core::kValueY under its rect's top. Not its continuous
	// layout, whose bar sits on a fixed panel row and runs through the digits on the 64-row panel. A view draws and
	// never reads input: the value goes in a region granted none.
	const int32_t digits_top = detail::row_y(f, 0) - 1 - widgets::core::kValueY;
	f.region({0, 0, w, h}, false, [&](widgets::oled::OledFrame& g) {
		widgets::oled::int_value(g, {0, digits_top, w, h - digits_top}, v, {0, 127}, {}, false);
	});
	// the velocity as a bar along the last 8 rows, inset as the continuous bar is
	constexpr int32_t kBar = 8;
	constexpr int32_t kInset = 12;
	widgets::oled::progress(f, {kInset, h - kBar, std::max(0, w - 2 * kInset), kBar}, v, 127);
}

} // namespace deluge::ui::views::oled
