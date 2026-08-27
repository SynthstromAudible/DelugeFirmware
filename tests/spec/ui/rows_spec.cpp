#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/grid/rows.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::grid;
using deluge::gfx::grid::Rect;
namespace uitest = deluge::ui::test;

// `row` and `clip_row` (spec B section 7), the two components every pad view's `renderRow` is made
// of. `clip_row` is a direct port of `NoteRow::renderRow`'s colour rules
// (model/note/note_row.cpp:1873-2013), so the colours below are spelled as the legacy expressions
// -- `colour.adjustFractional((65 + v + v / 2) << 8, 255 << 8)` and the two derived colours -- and
// never as literals a port could drift from silently.

namespace {
// The row's three colours, as a view derives them before calling `NoteRow::renderRow`: the base
// colour plus `RGB::forTail()` / `RGB::forBlur()` (gfx/grid/rgb.h:107-129).
constexpr RGB kBase = RGB{200, 100, 50};
const RGB kTail = kBase.forTail();
const RGB kBlur = kBase.forBlur();

/// note_row.cpp:1960, transcribed here so the spec asserts against legacy's arithmetic rather
/// than against the port's own.
RGB velocity_colour(RGB colour, uint8_t velocity) {
	return colour.adjustFractional(static_cast<uint16_t>((65 + velocity + (velocity / 2)) << 8),
	                               static_cast<uint16_t>(255 << 8));
}
} // namespace

// clang-format off
describe rows("ui::grid rows", $ {
	it("paints a note's start, tail and blur exactly as NoteRow::renderRow does", _ {
		uitest::GridRig r;
		// The task 11 brief's case: a three-column note starting on column 0 at full velocity, and
		// a one-column note starting *within* column 5.
		const NoteCell notes[] = {
		    {.start_col = 0, .len_cols = 3, .velocity = 127, .starts_within = false, .tail = true},
		    {.start_col = 5, .len_cols = 1, .velocity = 64, .starts_within = true, .tail = true},
		};
		ClipRowData data{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes};
		clip_row(r.frame, 2, data);

		expect(r.grid.at(0, 2)).to_equal(velocity_colour(kBase, 127));   // starts exactly on the square
		expect(r.grid.at(1, 2)).to_equal(kTail);                         // continuing
		expect(r.grid.at(2, 2)).to_equal(kTail);
		expect(r.grid.at(3, 2)).to_equal(RGB{0, 0, 0});                  // past the note: cleared
		expect(r.grid.at(4, 2)).to_equal(RGB{0, 0, 0});
		expect(r.grid.at(5, 2)).to_equal(kBlur);                         // starts within the square
		// At full velocity the fraction is 255 << 8 over 255 << 8, i.e. the row colour itself.
		expect(r.grid.at(0, 2)).to_equal(kBase);
	});
	it("scales the start colour linearly with velocity, from a quarter brightness up", _ {
		uitest::GridRig r;
		const NoteCell quiet[] = {{.start_col = 1, .len_cols = 1, .velocity = 0}};
		const NoteCell mid[] = {{.start_col = 2, .len_cols = 1, .velocity = 64}};
		clip_row(r.frame, 0, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = quiet});
		clip_row(r.frame, 1, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = mid});
		expect(r.grid.at(1, 0)).to_equal(velocity_colour(kBase, 0));
		expect(r.grid.at(2, 1)).to_equal(velocity_colour(kBase, 64));
		// "linearly from 0.25 to max brightness with velocity" (note_row.cpp:1957-1958): a zero
		// velocity is a quarter-ish of the row colour, never black, and never the full colour.
		expect(int(r.grid.at(1, 0).r)).to_equal(int(kBase.r) * ((65 + 0) << 8) / (255 << 8));
		expect(r.grid.at(1, 0) == kBase).to_be_false();
		expect(r.grid.at(1, 0) == RGB{0, 0, 0}).to_be_false();
	});
	it("draws no tail when the note does not allow one", _ {
		uitest::GridRig r;
		// legacy `allowNoteTails == false` (note_row.cpp:1977): the continuing squares stay clear.
		const NoteCell notes[] = {{.start_col = 4, .len_cols = 4, .velocity = 127, .starts_within = false, .tail = false}};
		clip_row(r.frame, 3, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes});
		expect(r.grid.at(4, 3)).to_equal(velocity_colour(kBase, 127));
		expect(r.grid.at(5, 3)).to_equal(RGB{0, 0, 0});
		expect(r.grid.at(6, 3)).to_equal(RGB{0, 0, 0});
		expect(r.grid.at(7, 3)).to_equal(RGB{0, 0, 0});
	});
	it("clears an empty row, and clears one that had been painted", _ {
		uitest::GridRig r;
		r.frame.pad(3, 6, RGB{9, 9, 9});
		const NoteCell none[] = {};
		clip_row(r.frame, 6, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur,
		                                 .notes = std::span<const NoteCell>(none, 0)});
		expect(r.grid.at(3, 6)).to_equal(RGB{0, 0, 0});
		expect(r.state()).to_equal(std::string(""));
	});
	it("paints the playhead column over the notes, in the driver's flash colour", _ {
		uitest::GridRig r;
		const NoteCell notes[] = {{.start_col = 0, .len_cols = 4, .velocity = 127, .starts_within = false, .tail = true}};
		ClipRowData data{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes, .playhead_col = 2};
		clip_row(r.frame, 4, data);
		// PadLEDs::flashColours[0] (hid/led/pad_leds.cpp:277-278).
		expect(r.grid.at(2, 4)).to_equal(RGB{130, 120, 130});
		expect(kPlayheadColour).to_equal(RGB{130, 120, 130});
		expect(r.grid.at(1, 4)).to_equal(kTail);      // its neighbours are untouched
		expect(r.grid.at(3, 4)).to_equal(kTail);
		// A playhead on a column with no note still lights.
		ClipRowData empty{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .playhead_col = 9};
		clip_row(r.frame, 5, empty);
		expect(r.grid.at(9, 5)).to_equal(RGB{130, 120, 130});
	});
	it("lets a note's start win the column another note's tail crosses", _ {
		uitest::GridRig r;
		const NoteCell notes[] = {
		    {.start_col = 0, .len_cols = 8, .velocity = 100, .starts_within = false, .tail = true},
		    {.start_col = 4, .len_cols = 1, .velocity = 127, .starts_within = false, .tail = true},
		};
		clip_row(r.frame, 0, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes});
		expect(r.grid.at(3, 0)).to_equal(kTail);
		expect(r.grid.at(4, 0)).to_equal(velocity_colour(kBase, 127));  // the start, not the tail
		expect(r.grid.at(5, 0)).to_equal(kTail);
	});
	it("paints only the frame it is given, so a region keeps the sidebar out of it", _ {
		uitest::GridRig r;
		r.frame.pad(16, 1, RGB{5, 5, 5});
		r.frame.pad(17, 1, RGB{6, 6, 6});
		const NoteCell notes[] = {{.start_col = 0, .len_cols = 18, .velocity = 127, .starts_within = false, .tail = true}};
		r.frame.region(Rect{0, 0, 16, 8}, [&](GridFrame& main) {
			clip_row(main, 1, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes});
		});
		expect(r.grid.at(15, 1)).to_equal(kTail);
		expect(r.grid.at(16, 1)).to_equal(RGB{5, 5, 5});   // untouched by a 16-wide frame
		expect(r.grid.at(17, 1)).to_equal(RGB{6, 6, 6});
	});
	it("reports the pressed column in the response, as x + 1", _ {
		uitest::GridRig r;
		const std::array<RGB, 4> cells{RGB{1, 1, 1}, RGB{2, 2, 2}, RGB{3, 3, 3}, RGB{4, 4, 4}};
		PadEvent ev[] = {{2, 3, 90, true}};
		r.input.pads = ev;
		const Response pressed = row(r.frame, 3, cells);
		expect(pressed.pressed).to_be_true();
		expect(int(pressed.velocity)).to_equal(3);          // column 2, encoded as x + 1
		expect(r.grid.at(2, 3)).to_equal(RGB{3, 3, 3});
		// A row with no press reports 0, and a different row does not see this one's event.
		const Response quiet = row(r.frame, 4, cells);
		expect(quiet.pressed).to_be_false();
		expect(int(quiet.velocity)).to_equal(0);
	});
	it("reports the leftmost press, and ORs release and held across the row", _ {
		uitest::GridRig r;
		const std::array<RGB, 8> cells{};
		PadEvent ev[] = {{5, 2, 40, true}, {1, 2, 90, true}, {7, 2, 0, false}};
		r.input.pads = ev;
		r.input.held_pads.set(3, 2, true);
		const Response resp = row(r.frame, 2, cells);
		expect(int(resp.velocity)).to_equal(2);   // column 1, not column 5
		expect(resp.released).to_be_true();
		expect(resp.held).to_be_true();
	});
	it("stops at the frame's width and at the span's end", _ {
		uitest::GridRig r;
		const std::array<RGB, 20> wide{[] { std::array<RGB, 20> a{}; for (auto& c : a) { c = RGB{7, 7, 7}; } return a; }()};
		r.frame.region(Rect{0, 0, 4, 8}, [&](GridFrame& f) { row(f, 0, wide); });
		expect(r.grid.at(3, 0)).to_equal(RGB{7, 7, 7});
		expect(r.grid.at(4, 0)).to_equal(RGB{0, 0, 0});    // past the region
		const std::array<RGB, 2> few{RGB{8, 8, 8}, RGB{8, 8, 8}};
		r.frame.pad(2, 7, RGB{3, 3, 3});
		row(r.frame, 7, few);
		expect(r.grid.at(1, 7)).to_equal(RGB{8, 8, 8});
		expect(r.grid.at(2, 7)).to_equal(RGB{3, 3, 3});    // a short span leaves the rest alone
	});
	it("matches the recorded pad state", _ {
		uitest::GridRig r;
		const NoteCell notes[] = {
		    {.start_col = 0, .len_cols = 3, .velocity = 127, .starts_within = false, .tail = true},
		    {.start_col = 5, .len_cols = 1, .velocity = 64, .starts_within = true, .tail = true},
		    {.start_col = 8, .len_cols = 4, .velocity = 32, .starts_within = false, .tail = true},
		    {.start_col = 13, .len_cols = 2, .velocity = 100, .starts_within = false, .tail = false},
		};
		r.frame.region(Rect{0, 0, 16, 8}, [&](GridFrame& main) {
			clip_row(main, 0, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur, .notes = notes});
			clip_row(main, 1, ClipRowData{.colour = kBase, .tail_colour = kTail, .blur_colour = kBlur,
			                              .notes = notes, .playhead_col = 6});
			const std::array<RGB, 4> cells{RGB{10, 0, 0}, RGB{0, 10, 0}, RGB{0, 0, 10}, RGB{10, 10, 10}};
			row(main, 3, cells);
		});
		expect_golden(self, "../ui/widgets/clip_row_notes.state", r.state());
	});
});
// clang-format on

// Not CPPSPEC_SPEC(rows): create_test_sourcelist derives the extern function name from this file's
// path relative to tests/spec (slashes become underscores), i.e. `ui_rows_spec`, which does not
// match the `rows` describe-block name the macro would assume. Spelled out by hand, as the other
// ui specs do, so the exported symbol matches what the generated driver declares `extern`.
int ui_rows_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(rows).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
