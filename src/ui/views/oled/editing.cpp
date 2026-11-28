#include "ui/views/oled/editing.h"

#include "ui/views/oled/detail/layout.h"
#include "ui/widgets/core/state.h"
#include "ui/widgets/oled/counters.h"
#include "ui/widgets/oled/text.h"
#include "ui/widgets/oled/waveform.h"

#include <algorithm>
#include <array>

namespace deluge::ui::views::oled {

void zoom_held(widgets::oled::OledFrame& f, const ZoomHeld& d) {
	widgets::oled::title_note(f, d.title, d.length);
	detail::big_text(f, d.zoom);
	constexpr int32_t kBar = 7;
	widgets::oled::position_bar(f, {0, f.bounds().h - kBar, f.bounds().w, kBar}, d.total, d.start, d.len);
}

void quantize_held(widgets::oled::OledFrame& f, const QuantizeHeld& d) {
	widgets::oled::title_note(f, d.title, d.amount_text);
	widgets::oled::quantize_graph(f, detail::body_rect(f, 0), d.length_ticks, d.grid_ticks, d.notes, d.amount);
}

void step_held(widgets::oled::OledFrame& f, const StepHeld& d) {
	// a view draws and never reads input: the columns go in a region granted none
	f.region(f.bounds(), false, [&](widgets::oled::OledFrame& g) {
		widgets::core::ColumnsState state{};
		widgets::oled::columns(g, g.bounds(), state, d.columns, d.title);
	});
}

void automation_moment(widgets::oled::OledFrame& f, const AutomationMoment& d) {
	widgets::oled::title_note(f, d.param, d.note);
	widgets::oled::automation_lane(f, detail::body_rect(f, 0), d.length_ticks, d.nodes, d.empty_value, d.window_start,
	                               d.window_len, d.playhead);
}

void slicer(widgets::oled::OledFrame& f, const Slicer& d) {
	widgets::oled::title_note(f, d.title, d.count);
	const int32_t top = detail::row_y(f, 0);
	const int32_t last = detail::last_row_y(f);
	widgets::oled::sliced_waveform(f, {0, top, f.bounds().w, std::max(0, last - 1 - top)}, d.peaks, d.slice_cols,
	                               d.focused);
	detail::text_pair(f, last, {d.start, d.length});
}

void moving_kit_row(widgets::oled::OledFrame& f, const ListMoment& d) {
	widgets::oled::title_note(f, d.title, d.note);
	detail::list_rows(f, d.rows, d.current);
}

void randomized_drums(widgets::oled::OledFrame& f, const ListMoment& d) {
	widgets::oled::title_note(f, d.title, d.note);
	detail::list_rows(f, d.rows, d.current);
}

void undo(widgets::oled::OledFrame& f, const Undo& d) {
	widgets::oled::title_note(f, d.title, d.left);
	const std::array<detail::RowText, 3> rows{{{d.what, {}}, {d.where, {}}, {d.redo_label, d.redo_hint}}};
	detail::text_rows(f, rows);
}

} // namespace deluge::ui::views::oled
