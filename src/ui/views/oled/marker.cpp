#include "ui/views/oled/marker.h"

#include "ui/gfx/oled/icons.h"
#include "ui/widgets/oled/sample_position.h"
#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

void marker_position(widgets::oled::OledFrame& f, const MarkerPosition& d) {
	widgets::oled::title(f, d.label);
	if (d.loop_locked) {
		widgets::oled::title_badge(f, gfx::oled::icons::markerLock);
	}
	// The body's first line is legacy's `OLED_MAIN_TOPMOST_PIXEL + 17`. The frame's origin already accounts for the
	// bezel, so this is `title_height + 5`, read off the style so the two lines stay under the separator on a panel
	// whose title face is taller.
	widgets::oled::sample_position(
	    f, gfx::oled::Rect{1, f.style.title_height + 5, f.bounds().w - 1, 2 * widgets::oled::kSamplePositionLineGap},
	    d.position, d.sample_rate);
}

} // namespace deluge::ui::views::oled
