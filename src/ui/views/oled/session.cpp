#include "ui/views/oled/session.h"

#include "ui/gfx/oled/font.h"
#include "ui/widgets/oled/counters.h"
#include "ui/widgets/oled/text.h"

#include <cstdint>

namespace deluge::ui::views::oled {

namespace {

using gfx::oled::kTextSpacingX;
using gfx::oled::kTextSpacingY;

/// The metronome, 7 columns of one byte each (legacy's session-screen icon).
constexpr uint8_t kMetronome[] = {0b11100000, 0b11011100, 0b11000011, 0b11100001, 0b11010011, 0b11011000, 0b11100100};
constexpr int32_t kTopY = 3;

/// The session's third row: 13 rows above the bottom (30 on the 43-row panel).
int32_t third_y(const widgets::oled::OledFrame& f) {
	return f.bounds().h - 13;
}

/// Position left, tempo right, the metronome 10 px left of the tempo; the position stops 4 px short of either.
void top_row(widgets::oled::OledFrame& f, std::string_view position, std::string_view tempo, bool metronome) {
	const int32_t tempo_x = f.bounds().w - static_cast<int32_t>(tempo.size()) * kTextSpacingX;
	const int32_t icon_x = tempo_x - 10;
	if (metronome) {
		f.graphic(kMetronome, icon_x, kTopY, 7, 8, 1);
	}
	const int32_t right = metronome ? icon_x : tempo_x;
	const int32_t end = (metronome || !tempo.empty()) ? right - 4 : f.bounds().w;
	if (!position.empty() && end > 0) {
		f.region({0, kTopY, end, kTextSpacingY}, false,
		         [&](widgets::oled::OledFrame& band) { band.glyphs(position, 0, 0, kTextSpacingX, kTextSpacingY); });
	}
	if (!tempo.empty()) {
		f.glyphs(tempo, tempo_x, kTopY, kTextSpacingX, kTextSpacingY);
	}
}

/// The name, centred between the top row's foot and the third row (17 on the 43-row panel).
void middle_name(widgets::oled::OledFrame& f, std::string_view s) {
	detail::name_centred(f, (kTopY + kTextSpacingY + third_y(f)) / 2 - 4, s);
}

} // namespace

void session_resting(widgets::oled::OledFrame& f, const SessionResting& d) {
	top_row(f, d.position, d.tempo, d.metronome);
	middle_name(f, d.song);
	detail::text_pair(f, third_y(f), {d.scale, d.swing});
	widgets::oled::level_lines(f, {0, f.bounds().h - 2, f.bounds().w, 2}, d.level_l, d.level_r);
}

void session_queued(widgets::oled::OledFrame& f, const SessionQueued& d) {
	top_row(f, d.position, d.tempo, d.metronome);
	middle_name(f, d.next_name);
	detail::text_pair(f, third_y(f), {d.next_label, d.when});
	const int32_t h = f.bounds().h;
	const int32_t x1 = f.bounds().w - 1;
	widgets::oled::amount_line(f, h - 2, 0, x1, d.progress);
	widgets::oled::amount_line(f, h - 1, 0, x1, d.progress);
}

void clip_held(widgets::oled::OledFrame& f, const ClipHeld& d) {
	widgets::oled::title_note(f, d.name, d.type);
	detail::text_pair(f, detail::row_y(f, 0), {d.length, d.launch});
	// three rows clear of the text row, as the sketch has it, so the strip's top note never touches the words
	gfx::oled::Rect strip = detail::body_rect(f, 1);
	strip.y += 3;
	strip.h -= 3;
	widgets::oled::note_strip(f, strip, d.length_ticks, d.notes, d.low, d.high, 0, 0, -1);
}

void section_held(widgets::oled::OledFrame& f, const SectionHeld& d) {
	widgets::oled::title_note(f, d.name, d.position);
	detail::text_rows(f, d.rows);
}

} // namespace deluge::ui::views::oled
