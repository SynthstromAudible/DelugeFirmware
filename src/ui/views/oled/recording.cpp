#include "ui/views/oled/recording.h"

#include "ui/gfx/oled/font.h"
#include "ui/widgets/oled/counters.h"
#include "ui/widgets/oled/popup.h"
#include "ui/widgets/oled/text.h"

#include <algorithm>

namespace deluge::ui::views::oled {

void record_arming(widgets::oled::OledFrame& f, const RecordArming& d) {
	widgets::oled::title_note(f, d.title, d.count);
	detail::text_rows(f, d.sources);
}

void count_in(widgets::oled::OledFrame& f, const CountIn& d) {
	widgets::oled::title_note(f, d.title, d.note);
	detail::big_text(f, d.beat);
	constexpr int32_t kBoxes = 5;
	widgets::oled::beat_boxes(f, {0, f.bounds().h - kBoxes, f.bounds().w, kBoxes}, d.beats, d.beats_done);
}

void overdub(widgets::oled::OledFrame& f, const Overdub& d) {
	widgets::oled::title_note(f, d.title, d.pass);
	detail::big_text(f, d.position);
	const int32_t h = f.bounds().h;
	const int32_t x1 = f.bounds().w - 1;
	widgets::oled::amount_line(f, h - 2, 0, x1, d.progress);
	widgets::oled::amount_line(f, h - 1, 0, x1, d.progress);
}

void resampling(widgets::oled::OledFrame& f, const Resampling& d) {
	const int32_t w = f.bounds().w;
	constexpr int32_t kMark = 5;
	const int32_t mark_x = w - kMark - 2;
	detail::title_short_of(f, d.title, mark_x);
	for (int32_t y = 3; y < 3 + kMark; y++) {
		f.hline(y, mark_x, mark_x + kMark - 1);
	}
	const int32_t top = detail::row_y(f, 0);
	// the elapsed time stops 4 px short of the stop label, as a row's label does
	const int32_t stop_x = w - static_cast<int32_t>(d.stop_label.size()) * gfx::oled::kTextSpacingX;
	const int32_t elapsed_w = d.stop_label.empty() ? w : std::max(0, stop_x - 4);
	widgets::oled::text(f, {0, top + 2, elapsed_w, 13}, d.elapsed,
	                    {.font = gfx::oled::FontId::MetricBold13, .align = widgets::oled::Align::Left});
	detail::text_pair(f, top + 5, {{}, d.stop_label});
	widgets::oled::level_lines(f, {0, f.bounds().h - 2, w, 2}, d.level_l, d.level_r);
}

void stem_export(widgets::oled::OledFrame& f, const StemExport& d) {
	widgets::oled::title_note(f, d.title, d.of);
	detail::text_pair(f, detail::row_y(f, 0), {d.stem, d.time});
	const int32_t last = detail::last_row_y(f);
	constexpr int32_t kBar = 7;
	widgets::oled::progress(f, {0, last - kBar - 1, f.bounds().w, kBar}, d.done, d.total);
	detail::text_pair(f, last, {d.next, d.next_label});
}

} // namespace deluge::ui::views::oled
