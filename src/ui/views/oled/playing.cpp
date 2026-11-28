#include "ui/views/oled/playing.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/counters.h"
#include "ui/widgets/oled/popup.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/waveform.h"

#include <algorithm>
#include <array>

namespace deluge::ui::views::oled {

void auditioning_row(widgets::oled::OledFrame& f, const AuditioningRow& d) {
	widgets::oled::title_note(f, d.drum, d.row);
	const int32_t top = detail::row_y(f, 0);
	const int32_t last = detail::last_row_y(f);
	widgets::oled::waveform(f, {0, top, f.bounds().w, std::max(0, last - 1 - top)}, d.peaks);
	detail::text_pair(f, last, {d.velocity, d.length});
}

void stutter_held(widgets::oled::OledFrame& f, const StutterHeld& d) {
	widgets::oled::title(f, d.title);
	detail::big_text(f, d.rate);
	widgets::oled::beat_boxes(f, {0, f.bounds().h - 5, f.bounds().w, 4}, d.repeats,
	                          std::clamp(d.repeats_done, 0, std::max(0, d.repeats)), 6, 2);
}

void tap_tempo(widgets::oled::OledFrame& f, const TapTempo& d) {
	widgets::oled::title(f, d.title);
	detail::big_text(f, d.tempo);
	widgets::oled::beat_boxes(f, {0, f.bounds().h - 5, f.bounds().w, 5}, d.taps,
	                          std::clamp(d.taps_done, 0, std::max(0, d.taps)));
}

void midi_learn(widgets::oled::OledFrame& f, const MidiLearn& d) {
	widgets::oled::title(f, d.title);
	const std::array<detail::RowText, 2> rows{{{d.target, d.value}, {d.cc, d.channel}}};
	detail::text_rows(f, rows);
	const int32_t h = f.bounds().h;
	const int32_t x1 = f.bounds().w - 1;
	widgets::oled::amount_line(f, h - 2, 0, x1, d.level);
	widgets::oled::amount_line(f, h - 1, 0, x1, d.level);
}

void mod_source_held(widgets::oled::OledFrame& f, const ModSourceHeld& d) {
	widgets::oled::title(f, d.title);
	detail::list_rows(f, d.rows, -1);
}

void loading_song(widgets::oled::OledFrame& f, const LoadingSong& d) {
	widgets::oled::title(f, d.title);
	detail::name_centred(f, detail::row_y(f, 0), d.song);
	const int32_t last = detail::last_row_y(f);
	constexpr int32_t kBar = 7;
	widgets::oled::progress(f, {0, last - kBar - 1, f.bounds().w, kBar}, d.done, d.total);
	detail::text_pair(f, last, {d.label, d.count});
}

} // namespace deluge::ui::views::oled
