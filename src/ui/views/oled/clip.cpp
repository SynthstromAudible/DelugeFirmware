#include "ui/views/oled/clip.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/waveform.h"

#include <algorithm>

namespace deluge::ui::views::oled {

void instrument_clip_resting(widgets::oled::OledFrame& f, const InstrumentClipResting& d) {
	widgets::oled::output_name(f, f.bounds(), d.name);
	constexpr int32_t kStrip = 10;
	const gfx::oled::Rect strip{0, std::max(0, f.bounds().h - kStrip), f.bounds().w, std::min(kStrip, f.bounds().h)};
	widgets::oled::note_strip(f, strip, d.length_ticks, d.notes, d.low, d.high, d.window_start, d.window_len,
	                          d.playhead);
}

void audio_clip_resting(widgets::oled::OledFrame& f, const AudioClipResting& d) {
	widgets::oled::title(f, d.name);
	const int32_t top = detail::row_y(f, 0);
	const int32_t last = detail::last_row_y(f);
	const gfx::oled::Rect wave{0, top, f.bounds().w, std::max(0, last - 1 - top)};
	widgets::oled::waveform(f, wave, d.peaks);
	widgets::oled::window_marks(f, wave, d.window_x, d.window_w, d.playhead_x);
	detail::text_pair(f, last, {d.length, d.pitch});
}

void kit_clip_resting(widgets::oled::OledFrame& f, const KitClipResting& d) {
	widgets::oled::title(f, d.kit);
	detail::list_rows(f, d.rows, d.current);
}

void cv_clip_resting(widgets::oled::OledFrame& f, const CvClipResting& d) {
	widgets::oled::title_note(f, d.title, d.gate_label);
	detail::big_text(f, d.voltage);
	detail::text_pair(f, detail::last_row_y(f), {d.note, d.gate_state});
}

} // namespace deluge::ui::views::oled
