#include "support/frames.h"
#include "ui/widgets/oled/timeline_strips.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kTop = 5; // the 48-row rig's hidden rows: canvas row = local row + 5

int32_t lit_count(uitest::Rig48& r) {
	int32_t n = 0;
	for (int32_t y = 0; y < 48; y++) {
		for (int32_t x = 0; x < 128; x++) {
			n += r.lit(x, y) ? 1 : 0;
		}
	}
	return n;
}

/// The arrangement of the canvas's "Arranger · idle" sketch.
constexpr std::array<LaneSpan, 27> kSong{{
    {0, 0, 8, false},  {0, 8, 16, false},  {0, 24, 16, false}, {0, 40, 8, false},  {0, 48, 8, false},
    {1, 8, 16, false}, {1, 24, 16, false}, {1, 44, 12, false}, {2, 4, 4, false},   {2, 8, 8, false},
    {2, 16, 8, false}, {2, 24, 8, false},  {2, 32, 8, false},  {2, 44, 12, false}, {3, 8, 32, false},
    {3, 40, 8, true},  {4, 0, 8, false},   {4, 16, 16, false}, {4, 40, 16, false}, {5, 24, 16, false},
    {5, 44, 8, false}, {6, 16, 8, false},  {6, 32, 8, false},  {7, 6, 2, false},   {7, 22, 2, false},
    {7, 38, 2, false}, {7, 46, 8, true},
}};

/// A 64-step clip over two octaves: a bass line and a few chords.
std::vector<StripNote> two_octave_clip() {
	std::vector<StripNote> notes;
	constexpr int32_t kStep = 24;
	for (int32_t s = 0; s < 64; s += 4) {
		notes.push_back({.start = s * kStep, .length = 2 * kStep, .pitch = static_cast<uint8_t>(48 + (s / 4) % 5)});
	}
	for (int32_t s = 8; s < 64; s += 16) {
		for (uint8_t p : {60, 64, 67}) {
			notes.push_back({.start = s * kStep, .length = 6 * kStep, .pitch = static_cast<uint8_t>(p + (s / 16) % 3)});
		}
	}
	return notes;
}

} // namespace

// clang-format off
describe window_marks_w("ui::widgets::oled::window_marks", $ {
	it("boxes the window dotted and inverts the playhead", _ {
		uitest::Rig48 r;
		const Rect area{0, 17, 128, 24};
		window_marks(r.frame, area, 32, 32, 40);
		expect(r.lit(32, 17 + kTop)).to_be_true();      // the box's corner
		expect(r.lit(32, 17 + 23 + kTop)).to_be_true();
		expect(r.lit(33, 17 + kTop)).to_be_false();     // dotted
		for (int32_t y = 18; y < 17 + 23; y++) {
			expect(r.lit(40, y + kTop)).to_be_true();   // the playhead's column, inverted over nothing
		}
	});

	it("draws nothing for w <= 0 and playhead < 0", _ {
		uitest::Rig48 r;
		window_marks(r.frame, {0, 17, 128, 24}, 10, 0, -1);
		expect(lit_count(r)).to_equal(0);
	});
});

describe overview_w("ui::widgets::oled::overview", $ {
	it("draws a span solid on its lane", _ {
		uitest::Rig48 r;
		const std::array<LaneSpan, 1> one{{{.lane = 1, .start = 8, .length = 16, .muted = false}}};
		overview(r.frame, {0, 17, 128, 24}, 8, 64, one, 0, 0, -1);
		expect(r.lit(32, 17 + 3 + 1 + kTop)).to_be_true();
		expect(r.lit(32, 17 + 3 + 2 + kTop)).to_be_false(); // a lane's spans are one row shorter than the lane
	});

	it("leaves a one-column gap between back-to-back spans", _ {
		uitest::Rig48 r;
		const std::array<LaneSpan, 2> two{{{0, 0, 8, false}, {0, 8, 8, false}}};
		overview(r.frame, {0, 17, 128, 8}, 1, 64, two, 0, 0, -1);
		expect(r.lit(14, 17 + 3 + kTop)).to_be_true();
		expect(r.lit(15, 17 + 3 + kTop)).to_be_false();
		expect(r.lit(16, 17 + 3 + kTop)).to_be_true();
	});

	it("dots a muted span's outline", _ {
		uitest::Rig48 r;
		const std::array<LaneSpan, 1> one{{{.lane = 0, .start = 8, .length = 16, .muted = true}}};
		overview(r.frame, {0, 17, 128, 8}, 1, 64, one, 0, 0, -1);
		expect(r.lit(16, 17 + kTop)).to_be_true();     // corner
		expect(r.lit(30, 17 + 3 + kTop)).to_be_false(); // inside
	});

	it("outlines the other spans when one is selected", _ {
		uitest::Rig48 r;
		const std::array<LaneSpan, 4> four{{{0, 0, 8, false}, {0, 16, 8, false, true}, {0, 32, 8, false},
		                                     {0, 48, 8, false}}};
		overview(r.frame, {0, 17, 128, 8}, 1, 64, four, 0, 0, -1);
		expect(r.lit(40, 17 + 3 + kTop)).to_be_true();  // span 1's centre: filled
		expect(r.lit(8, 17 + 3 + kTop)).to_be_false();  // span 0's centre: outlined
		expect(r.lit(0, 17 + 3 + kTop)).to_be_true();   // span 0's outline
	});

	it("boxes the pads' window dotted and inverts the playhead", _ {
		uitest::Rig48 r;
		overview(r.frame, {0, 17, 128, 24}, 8, 64, {}, 16, 16, 20);
		expect(r.lit(32, 17 + kTop)).to_be_true();
		expect(r.lit(40, 30 + kTop)).to_be_true();
	});

	it("draws only the window box for no spans", _ {
		uitest::Rig48 r;
		uitest::Rig48 box;
		overview(r.frame, {0, 17, 128, 24}, 8, 64, {}, 16, 16, -1);
		window_marks(box.frame, {0, 17, 128, 24}, 32, 32, -1);
		expect(r.pbm()).to_equal(box.pbm());
	});

	it("clips spans past units", _ {
		uitest::Rig48 r;
		const std::array<LaneSpan, 1> one{{{.lane = 0, .start = 60, .length = 20, .muted = false}}};
		overview(r.frame, {0, 17, 100, 8}, 1, 64, one, 0, 0, -1);
		for (int32_t x = 100; x < 128; x++) {
			for (int32_t y = 0; y < 48; y++) {
				expect(r.lit(x, y)).to_be_false();
			}
		}
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		overview(r48.frame, {0, 17, 128, 24}, 8, 64, kSong, 16, 16, 21);
		expect_golden(self, "ui/widgets/overview_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		overview(r64.frame, {0, 17, 128, 40}, 8, 64, kSong, 16, 16, 21);
		expect_golden(self, "ui/widgets/overview_64.pbm", r64.pbm());

		const std::array<LaneSpan, 4> lane{{{0, 0, 8, false}, {0, 16, 16, false, true}, {0, 40, 8, false},
		                                     {0, 52, 12, false}}};
		uitest::Rig48 s48;
		overview(s48.frame, {0, 30, 128, 7}, 1, 64, lane, 0, 0, -1);
		expect_golden(self, "ui/widgets/overview_selected_48.pbm", s48.pbm());
		uitest::Rig64 s64;
		overview(s64.frame, {0, 40, 128, 10}, 1, 64, lane, 0, 0, -1);
		expect_golden(self, "ui/widgets/overview_selected_64.pbm", s64.pbm());
	});
});

describe note_strip_w("ui::widgets::oled::note_strip", $ {
	it("places a note by time and pitch", _ {
		uitest::Rig48 r;
		const std::array<StripNote, 1> one{{{.start = 96, .length = 96, .pitch = 60}}};
		note_strip(r.frame, {0, 30, 128, 11}, 384, one, 50, 70, 0, 0, -1);
		// x = 96 * 128 / 384 = 32 .. 63; y = 30 + (70 - 60) * 10 / 20 = 35
		expect(r.lit(32, 35 + kTop)).to_be_true();
		expect(r.lit(63, 35 + kTop)).to_be_true();
		expect(r.lit(64, 35 + kTop)).to_be_false();
	});

	it("draws a zero-length note as one pixel", _ {
		uitest::Rig48 r;
		const std::array<StripNote, 1> one{{{.start = 96, .length = 0, .pitch = 70}}};
		note_strip(r.frame, {0, 30, 128, 11}, 384, one, 50, 70, 0, 0, -1);
		expect(r.lit(32, 30 + kTop)).to_be_true();
		expect(lit_count(r)).to_equal(1);
	});

	it("draws only the window box for no notes", _ {
		uitest::Rig48 r;
		uitest::Rig48 box;
		note_strip(r.frame, {0, 30, 128, 11}, 384, {}, 50, 70, 96, 96, -1);
		window_marks(box.frame, {0, 30, 128, 11}, 32, 32, -1);
		expect(r.pbm()).to_equal(box.pbm());
	});

	it("matches its goldens", _ {
		const std::vector<StripNote> clip = two_octave_clip();
		const auto span = std::span<const StripNote>{clip};
		uitest::Rig48 r48;
		note_strip(r48.frame, {0, 32, 128, 11}, 64 * 24, span, 48, 72, 16 * 24, 16 * 24, 18 * 24);
		expect_golden(self, "ui/widgets/note_strip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		note_strip(r64.frame, {0, 40, 128, 20}, 64 * 24, span, 48, 72, 16 * 24, 16 * 24, 18 * 24);
		expect_golden(self, "ui/widgets/note_strip_64.pbm", r64.pbm());
	});
});
describe quantize_graph_w("ui::widgets::oled::quantize_graph", $ {
	// r = {0, 15, 128, 27}: ticks on the top fifth (rows 15..19), blocks on the bottom quarter (rows 35..41)
	const Rect area{0, 15, 128, 27};

	it("leaves blocks under their ticks at amount 0", _ {
		uitest::Rig48 r;
		const std::array<int32_t, 1> note{{30 * 24}}; // column 60
		quantize_graph(r.frame, area, 64 * 24, 8 * 24, note, 0.f);
		expect(r.lit(60, 15 + kTop)).to_be_true();
		expect(r.lit(60, 41 + kTop)).to_be_true();
		expect(r.lit(59, 41 + kTop)).to_be_true();
		expect(r.lit(61, 41 + kTop)).to_be_true();
	});

	it("moves blocks onto the grid at amount 1", _ {
		uitest::Rig48 r;
		const std::array<int32_t, 1> note{{30 * 24}}; // snaps to 32: column 64
		quantize_graph(r.frame, area, 64 * 24, 8 * 24, note, 1.f);
		expect(r.lit(64, 41 + kTop)).to_be_true();
		expect(r.lit(65, 40 + kTop)).to_be_true();
		expect(r.lit(60, 15 + kTop)).to_be_true(); // the tick stays where the note was
	});

	it("rounds a tie to the later grid line", _ {
		uitest::Rig48 r;
		const std::array<int32_t, 1> note{{28 * 24}}; // half way between 24 and 32
		quantize_graph(r.frame, area, 64 * 24, 8 * 24, note, 1.f);
		expect(r.lit(65, 40 + kTop)).to_be_true();
	});

	it("draws only the grid for no notes", _ {
		uitest::Rig48 r;
		quantize_graph(r.frame, area, 64 * 24, 8 * 24, {}, 0.6f);
		for (int32_t y = 15; y < 35; y++) {
			for (int32_t x = 0; x < 128; x++) {
				expect(r.lit(x, y + kTop)).to_be_false();
			}
		}
		expect(r.lit(16, 35 + kTop)).to_be_true(); // a grid line's first dot
	});

	it("matches its goldens", _ {
		std::array<int32_t, 8> notes{};
		const float was[] = {3.0f, 13.5f, 20.0f, 29.0f, 35.5f, 45.0f, 52.5f, 61.0f};
		for (size_t i = 0; i < notes.size(); i++) {
			notes[i] = static_cast<int32_t>(was[i] * 24.f);
		}
		uitest::Rig48 r48;
		quantize_graph(r48.frame, area, 64 * 24, 8 * 24, notes, 0.6f);
		expect_golden(self, "ui/widgets/quantize_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		quantize_graph(r64.frame, {0, 16, 128, 46}, 64 * 24, 8 * 24, notes, 0.6f);
		expect_golden(self, "ui/widgets/quantize_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(timeline_strips_spec, window_marks_w, overview_w, note_strip_w, quantize_graph_w)
