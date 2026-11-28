#include "ui/views/oled/detail/layout.h"
#include "ui/views/oled/editing.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;
using widgets::oled::Column;
using widgets::oled::ListRow;

namespace {

constexpr ZoomHeld kZoom{.title = "ZOOM", .length = "4 BARS", .zoom = "1/16", .total = 64, .start = 16, .len = 16};

constexpr std::array<int32_t, 8> kWas{3 * 24, 324, 20 * 24, 29 * 24, 852, 45 * 24, 1260, 61 * 24};
QuantizeHeld quantize(std::span<const int32_t> notes) {
	return {.title = "QUANTIZE",
	        .amount_text = "60%",
	        .length_ticks = 64 * 24,
	        .grid_ticks = 8 * 24,
	        .notes = notes,
	        .amount = 0.6f};
}

AutomationMoment automation(std::span<const LaneNode> nodes) {
	return {.param = "LPF FREQ",
	        .note = "AUTO",
	        .length_ticks = 128 * 24,
	        .nodes = nodes,
	        .empty_value = 64,
	        .window_start = 32 * 24,
	        .window_len = 32 * 24,
	        .playhead = 40 * 24};
}

std::vector<int32_t> slice_cols() {
	std::vector<int32_t> cols;
	for (int32_t s = 1; s < 16; s++) {
		cols.push_back(s * 8);
	}
	return cols;
}

Slicer slicer_data(std::span<const widgets::core::PeakCol> peaks, std::span<const int32_t> cols) {
	return {.title = "SLICE 5",
	        .count = "16",
	        .start = "0.460S",
	        .length = "0.115S",
	        .peaks = peaks,
	        .slice_cols = cols,
	        .focused = 4};
}

constexpr std::array<ListRow, 3> kDragRows{{{.label = "SNARE"}, {.label = "HAT CL"}, {.label = "KICK 909"}}};
constexpr std::array<ListRow, 3> kRandRows{{{.label = "KICK", .value = "KICK 14"},
                                            {.label = "SNARE", .value = "CLAP 03"},
                                            {.label = "HAT", .value = "HAT 22"}}};

constexpr Undo kUndo{.title = "UNDO",
                     .left = "3 LEFT",
                     .what = "MOVED 4 NOTES",
                     .where = "LEAD 2",
                     .redo_label = "REDO",
                     .redo_hint = "SHIFT+UNDO"};

} // namespace

// clang-format off
describe zoom_held_v("ui::views::oled::zoom_held", $ {
	it("puts the position bar on the last seven rows", _ {
		uitest::Rig48 r;
		zoom_held(r.frame, kZoom);
		expect(r.lit(40, 39 + kTop)).to_be_true();  // the window, units 16..32 of 64: columns 32..63
		expect(lit_in(r, 0, 34, 128, 2)).to_equal(0); // between the digits and the bar
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		zoom_held(r48.frame, kZoom);
		expect_golden(self, "ui/views/oled/zoom_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		zoom_held(r64.frame, kZoom);
		expect_golden(self, "ui/views/oled/zoom_held_64.pbm", r64.pbm());
	});
});

describe quantize_held_v("ui::views::oled::quantize_held", $ {
	it("draws the grid with no notes", _ {
		uitest::Rig48 r;
		quantize_held(r.frame, quantize({}));
		const auto b = detail::body_rect(r.frame, 0);
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		quantize_held(r48.frame, quantize(kWas));
		expect_golden(self, "ui/views/oled/quantize_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		quantize_held(r64.frame, quantize(kWas));
		expect_golden(self, "ui/views/oled/quantize_held_64.pbm", r64.pbm());
	});
});

describe step_held_v("ui::views::oled::step_held", $ {
	it("reads no input", _ {
		widgets::oled::MeterColumn vel{.hint = widgets::oled::Hint::Bar, .norm = 0.8f, .raw = 100};
		std::array<Column, 1> cols{{{.label = "VEL", .render = widgets::oled::meter_column, .ctx = &vel}}};
		uitest::Rig48 a;
		a.input.turn = 2;
		step_held(a.frame, {.title = "C3 STEP 5", .columns = cols});
		uitest::Rig48 b;
		step_held(b.frame, {.title = "C3 STEP 5", .columns = cols});
		expect(a.ctx.claims.any()).to_be_false();
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		widgets::oled::MeterColumn vel{.hint = widgets::oled::Hint::Bar, .norm = 0.8f, .raw = 100};
		widgets::oled::MeterColumn prob{.hint = widgets::oled::Hint::Percent, .norm = 0.75f, .raw = 37}; // the hint doubles raw
		widgets::oled::TextColumn len{.value = "2"};
		widgets::oled::TextColumn iter{.value = "1:2"};
		const std::array<Column, 4> cols{{
		    {.label = "VEL", .render = widgets::oled::meter_column, .ctx = &vel},
		    {.label = "PROB", .render = widgets::oled::meter_column, .ctx = &prob},
		    {.label = "LEN", .render = widgets::oled::text_column, .ctx = &len},
		    {.label = "ITER", .render = widgets::oled::text_column, .ctx = &iter},
		}};
		uitest::Rig48 r48;
		step_held(r48.frame, {.title = "C3 STEP 5", .columns = cols});
		expect_golden(self, "ui/views/oled/step_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		step_held(r64.frame, {.title = "C3 STEP 5", .columns = cols});
		expect_golden(self, "ui/views/oled/step_held_64.pbm", r64.pbm());
	});
});

describe automation_moment_v("ui::views::oled::automation_moment", $ {
	it("draws the empty value's line with no nodes", _ {
		uitest::Rig48 r;
		AutomationMoment d = automation({});
		d.window_len = 0;
		d.playhead = -1;
		automation_moment(r.frame, d);
		const auto b = detail::body_rect(r.frame, 0);
		expect(lit_in(r, b.x, b.y, b.w, b.h)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		const auto nodes = swell_lane();
		uitest::Rig48 r48;
		automation_moment(r48.frame, automation(nodes));
		expect_golden(self, "ui/views/oled/automation_moment_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		automation_moment(r64.frame, automation(nodes));
		expect_golden(self, "ui/views/oled/automation_moment_64.pbm", r64.pbm());
	});
});

describe slicer_v("ui::views::oled::slicer", $ {
	it("draws the title with no peaks", _ {
		uitest::Rig48 r;
		Slicer d = slicer_data({}, {});
		d.focused = -1;
		slicer(r.frame, d);
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		expect(lit_in(r, 0, detail::last_row_y(r.frame), 128, 8)).to_be_greater_than(0);
	});

	it("keeps the waveform off the last row", _ {
		const auto peaks = break_peaks(128);
		const auto cols = slice_cols();
		uitest::Rig48 r;
		slicer(r.frame, slicer_data(peaks, cols));
		expect(lit_in(r, 0, detail::last_row_y(r.frame) - 1, 128, 1)).to_equal(0);
	});

	it("matches its goldens", _ {
		const auto peaks = break_peaks(128);
		const auto cols = slice_cols();
		uitest::Rig48 r48;
		slicer(r48.frame, slicer_data(peaks, cols));
		expect_golden(self, "ui/views/oled/slicer_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		slicer(r64.frame, slicer_data(peaks, cols));
		expect_golden(self, "ui/views/oled/slicer_64.pbm", r64.pbm());
	});
});

describe list_moments_v("ui::views::oled::moving_kit_row and randomized_drums", $ {
	it("selects the current row", _ {
		uitest::Rig48 a;
		moving_kit_row(a.frame, {.title = "MOVE ROW", .note = "3>5", .rows = kDragRows, .current = 1});
		uitest::Rig48 b;
		moving_kit_row(b.frame, {.title = "MOVE ROW", .note = "3>5", .rows = kDragRows, .current = 2});
		expect(a.pbm()).not_().to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		const ListMoment drag{.title = "MOVE ROW", .note = "3>5", .rows = kDragRows, .current = 2};
		const ListMoment rand{.title = "RANDOMIZED", .note = "", .rows = kRandRows, .current = 1};
		uitest::Rig48 d48;
		moving_kit_row(d48.frame, drag);
		expect_golden(self, "ui/views/oled/moving_kit_row_48.pbm", d48.pbm());
		uitest::Rig64 d64;
		moving_kit_row(d64.frame, drag);
		expect_golden(self, "ui/views/oled/moving_kit_row_64.pbm", d64.pbm());
		uitest::Rig48 r48;
		randomized_drums(r48.frame, rand);
		expect_golden(self, "ui/views/oled/randomized_drums_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		randomized_drums(r64.frame, rand);
		expect_golden(self, "ui/views/oled/randomized_drums_64.pbm", r64.pbm());
	});
});

describe undo_v("ui::views::oled::undo", $ {
	it("draws with empty text", _ {
		uitest::Rig48 r;
		undo(r.frame, {});
		expect(lit_in(r, 0, r.style.title_height + 1, 128, 43 - r.style.title_height - 1)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		undo(r48.frame, kUndo);
		expect_golden(self, "ui/views/oled/undo_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		undo(r64.frame, kUndo);
		expect_golden(self, "ui/views/oled/undo_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(editing_spec, zoom_held_v, quantize_held_v, step_held_v, automation_moment_v, slicer_v, list_moments_v,
            undo_v)
