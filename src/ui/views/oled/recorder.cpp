#include "ui/views/oled/recorder.h"

#include "ui/widgets/oled/text.h"

namespace deluge::ui::views::oled {

void recorder_status(widgets::oled::OledFrame& f, const RecorderStatus& d) {
	if (d.mode == RecorderStatus::Mode::Meter) {
		widgets::oled::threshold_meter(f, d.meter);
		return;
	}
	// `MetricBold13` is the face `kTextBigSizeY` (13) selects; the centring is over the frame's own width, which is
	// legacy's `OLED_MAIN_WIDTH_PIXELS` on the shipped panel.
	widgets::oled::text(f, gfx::oled::Rect{0, kStatusY, f.bounds().w, gfx::oled::kTextBigSizeY}, d.word,
	                    {.font = gfx::oled::FontId::MetricBold13, .align = widgets::oled::Align::Centre});
}

} // namespace deluge::ui::views::oled
