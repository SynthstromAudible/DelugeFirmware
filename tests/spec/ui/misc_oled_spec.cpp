#include "../gfx/support/golden.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/core/style.h"
#include "ui/oled/misc.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Canvas;
using deluge::gfx::oled::Point;
using deluge::gfx::oled::Rect;
namespace icons = deluge::gfx::oled::icons;
namespace uitest = deluge::ui::test;

// The four "everything else" OLED widgets, pinned the same three ways the other ui specs use.
//
// Byte for byte against legacy where legacy exists: `namespace legacy` below re-transcribes
// `View::drawOutputNameFromDetails`' drawing half and `OLED::renderEmulated7Seg` (as it existed
// at 6a4ccefbc^, the commit before the emulated-display feature was deleted) against the raw gfx
// `Canvas` in canvas-absolute coordinates, with every magic number spelled as a literal (3/12 and
// 17/30 for the two y pairs, 6/9 and 9/10 for the two faces, 33 and 17 for the digit pitch and
// row pitch) rather than borrowed from the constants the port lays out with -- otherwise the
// 64-row comparison would be circular.
//
// As individual coordinates for a handful of checks worth pinning explicitly: the type text's y
// on both panels, the digit shapes, and the segment pitch.
//
// And as golden PBMs, at both shipped geometries.
//
// Points worth calling out explicitly:
//
//  - `output_name` draws nothing for `edited`. `drawOutputNameFromDetails` takes `editedByUser`
//    and its body never reads it; the only renderer that did was the 7-segment one, gone with
//    commit 6a4ccefbc. There is no "edited" indicator left to reproduce.
//  - `output_name` does not arm a side-scroller for a name too wide to fit. That is
//    `scroll_text`'s job and needs state this widget does not own; the name is left-aligned,
//    which is what legacy draws on the first frame. Pinned below.
//  - `segment_digits`' constants are canvas-absolute -- `renderEmulated7Seg` laid out against
//    `main` with no `OLED_MAIN_TOPMOST_PIXEL` term -- so the port converts them by subtracting
//    the frame's origin, as `decimal_value` does for its rows 20 and 41. On the 48-row panel
//    that reproduces legacy exactly; on a flush 64-row panel the digits sit at legacy's rows
//    with a topmost pixel of 0, which is the same routine.
//  - `vu` and `envelope` have no legacy at all -- there is no OLED VU anywhere under
//    `hid/display` (the VU meter is `View::renderVUMeter`, RGB pads) and no combined ADSR
//    renderer. Golden-only, per the widget docs in misc.h.

namespace {

namespace legacy {

/// `View::drawOutputNameFromDetails`, drawing half only, with `height_pixels` and `topmost`
/// standing in for the two macros.
void drawOutputNameFromDetails(Canvas& canvas, const char* outputTypeText, const char* nameToDraw, int32_t width_pixels,
                               int32_t height_pixels, int32_t topmost) {
	{
		const int32_t yPos = (height_pixels == 64) ? topmost + 12 : topmost + 3;
		canvas.draw_string_centred(outputTypeText, yPos, 6, 9); // kTextSpacingX, kTextSpacingY
	}
	{
		const int32_t yPos = (height_pixels == 64) ? topmost + 30 : topmost + 17;
		const int32_t stringLengthPixels = deluge::gfx::oled::string_width_px(nameToDraw, 10); // kTextTitleSizeY
		if (stringLengthPixels <= width_pixels) {
			canvas.draw_string_centred(nameToDraw, yPos, 9, 10); // kTextTitleSpacingX
		}
		else {
			canvas.draw_string(nameToDraw, 0, yPos, 9, 10);
		}
	}
}

/// `OLED::renderEmulated7Seg` (as it existed at 6a4ccefbc^), minus its clearMainImage.
void renderEmulated7Seg(Canvas& main, const std::array<uint8_t, 4>& display) {
	for (int i = 0; i < 4; i++) {
		int ix = 33 * i + 1;
		const int dy = 17;

		const int horz[] = {6, 0, 3};
		for (int y = 0; y < 3; y++) {
			if (display[i] & (1 << horz[y])) {
				int ybase = 7 + dy * y;
				main.invert_area(ix + 3, 15, ybase + 0, ybase + 0);
				main.invert_area(ix + 2, 17, ybase + 1, ybase + 1);
				main.invert_area(ix + 3, 15, ybase + 2, ybase + 2);
			}
		}

		const int vert[] = {1, 2, 5, 4};
		for (int x = 0; x < 2; x++) {
			int xside = x * 2 - 1;
			for (int y = 0; y < 2; y++) {
				if (display[i] & (1 << vert[2 * x + y])) {
					int xbase = ix + 18 * x + 1;
					int ybase = 10 + dy * y;
					int yside = y * -2 + 1;
					main.invert_area(xbase + xside, 1, ybase + yside, ybase + 13 + yside);
					main.invert_area(xbase, 1, ybase + 0, ybase + 13);
					main.invert_area(xbase - xside, 1, ybase + 1, ybase + 12);
				}
			}
		}

		if (display[i] & (1 << 7)) {
			main.invert_area(ix + 21, 3, 41, 43);
		}
	}
}

} // namespace legacy

/// The *textbook* `gfedcba` codes for "0123". Not what the Deluge's panel actually sends: in the
/// Deluge's own bit order (bit 0 middle, 1/2 the left uprights, 3 bottom, 4/5 the right uprights,
/// 6 top, 7 the point) 0x3F is an eight missing its top bar. Used for the byte-compare and the
/// golden.
constexpr std::array<uint8_t, 4> kBriefBytes{0x3F, 0x06, 0x5B, 0x4F};

/// "0123" as the panel actually encodes it: `numberSegments[10] = {0x7E, 0x30, 0x6D, 0x79, ...}`
/// (as it existed at 6a4ccefbc^). Used for the shape assertions, which only mean anything
/// against real digits.
constexpr std::array<uint8_t, 4> kDigits0123{0x7E, 0x30, 0x6D, 0x79};

} // namespace

// clang-format off
describe misc_oled_w("ui::oled::misc", $ {
	it("draws the output type and name where drawOutputNameFromDetails does, at 48", _ {
		uitest::Rig48 r;
		const OutputNameData data{.type_text = "SYNTH",
		                          .name = "BASS 1",
		                          .slot = 12,
		                          .sub_slot = -1,
		                          .name_empty = false,
		                          .edited = false};
		output_name(r.frame, r.frame.bounds(), data);
		legacy::drawOutputNameFromDetails(r.reference, "SYNTH", "BASS 1", 128, 48, 5);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// The two y positions, spelled out: OLED_MAIN_TOPMOST_PIXEL + 3 and + 17.
		expect(r.frame.origin.y + kOutputTypeY48).to_equal(8);
		expect(r.frame.origin.y + kOutputNameY48).to_equal(22);
		expect_golden(self, "../ui/widgets/output_name_synth_48.pbm", r.pbm());
	});

	it("uses the 64-row panel's own y positions", _ {
		uitest::Rig64 r;
		const OutputNameData data{.type_text = "SYNTH",
		                          .name = "BASS 1",
		                          .slot = 12,
		                          .sub_slot = -1,
		                          .name_empty = false,
		                          .edited = false};
		output_name(r.frame, r.frame.bounds(), data);
		legacy::drawOutputNameFromDetails(r.reference, "SYNTH", "BASS 1", 128, 64, 0);
		expect(r.pbm()).to_equal(r.reference_pbm());

		expect(kOutputTypeY64).to_equal(12);
		expect(kOutputNameY64).to_equal(30);
		expect_golden(self, "../ui/widgets/output_name_synth_64.pbm", r.pbm());

		// The two panels really do lay the type text out differently.
		uitest::Rig48 r48;
		output_name(r48.frame, r48.frame.bounds(), data);
		expect(r.pbm() == r48.pbm()).to_be_false();
	});

	it("prints the slot when the name is empty, as slotToString does", _ {
		auto both_ways = [&](int32_t slot, int32_t sub_slot, const char* expected) {
			uitest::Rig48 r;
			const OutputNameData data{.type_text = "MIDI",
			                          .name = {},
			                          .slot = slot,
			                          .sub_slot = sub_slot,
			                          .name_empty = true,
			                          .edited = false};
			output_name(r.frame, r.frame.bounds(), data);
			legacy::drawOutputNameFromDetails(r.reference, "MIDI", expected, 128, 48, 5);
			expect(r.pbm()).to_equal(r.reference_pbm());
		};
		both_ways(12, -1, "12");  // intToString(channel + 1, buffer) -- the CV arm
		both_ways(12, 1, "12B");  // slotToString: 'A' + subSlot
		both_ways(1, 0, "1A");
		both_ways(16, 25, "16Z");
	});

	it("draws nothing extra for `edited`", _ {
		// editedByUser is a parameter legacy's OLED body never reads.
		uitest::Rig48 plain;
		uitest::Rig48 edited;
		OutputNameData data{.type_text = "SYNTH",
		                    .name = "BASS 1",
		                    .slot = 0,
		                    .sub_slot = -1,
		                    .name_empty = false,
		                    .edited = false};
		output_name(plain.frame, plain.frame.bounds(), data);
		data.edited = true;
		output_name(edited.frame, edited.frame.bounds(), data);
		expect(plain.pbm()).to_equal(edited.pbm());
	});

	it("left-aligns a name too wide to centre", _ {
		// Legacy centres the name when it fits, and draws it from x 0 when it does not (and then
		// armed a side scroller, which is scroll_text's job now).
		uitest::Rig48 r;
		const char* wide = "A VERY LONG PRESET NAME INDEED";
		const OutputNameData data{.type_text = "SYNTH",
		                          .name = wide,
		                          .slot = 0,
		                          .sub_slot = -1,
		                          .name_empty = false,
		                          .edited = false};
		output_name(r.frame, r.frame.bounds(), data);
		legacy::drawOutputNameFromDetails(r.reference, "SYNTH", wide, 128, 48, 5);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect(deluge::gfx::oled::string_width_px(wide, 10) > 128).to_be_true();
	});

	it("draws the seven-segment digits exactly as renderEmulated7Seg did", _ {
		uitest::Rig48 r;
		segment_digits(r.frame, r.frame.bounds(), std::span<const uint8_t, 4>{kBriefBytes});
		legacy::renderEmulated7Seg(r.reference, kBriefBytes);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/segment_digits_0123_48.pbm", r.pbm());

		// The shapes, against the panel's own "0123" codes. Digit n starts at 33n + 1; a top bar
		// is a 15/17/15 stack from row 7, the middle bar the same stack from row 24 and the
		// bottom bar from row 41; the uprights hang from rows 10 and 27.
		uitest::Rig48 d;
		segment_digits(d.frame, d.frame.bounds(), std::span<const uint8_t, 4>{kDigits0123});
		legacy::renderEmulated7Seg(d.reference, kDigits0123);
		expect(d.pbm()).to_equal(d.reference_pbm());

		// '0' (0x7E): every segment but the middle bar.
		expect(d.lit(1 + 3, 7)).to_be_true();    // top bar
		expect(d.lit(1 + 2, 8)).to_be_true();    // its 17-wide row
		expect(d.lit(1 + 1, 8)).to_be_false();   // and nothing either side of it
		expect(d.lit(1 + 19, 8)).to_be_false();
		expect(d.lit(1 + 3, 24)).to_be_false();  // no middle bar
		expect(d.lit(1 + 3, 41)).to_be_true();   // bottom bar
		expect(d.lit(1 + 1, 15)).to_be_true();   // upper-left upright, at ix + 1
		expect(d.lit(1 + 19, 15)).to_be_true();  // upper-right upright, at ix + 19

		// '1' (0x30): the two right uprights and nothing else.
		const int32_t ix1 = 33 + 1;
		expect(d.lit(ix1 + 3, 7)).to_be_false();  // no top bar
		expect(d.lit(ix1 + 3, 41)).to_be_false(); // no bottom bar
		expect(d.lit(ix1 + 1, 15)).to_be_false(); // no left uprights
		expect(d.lit(ix1 + 19, 15)).to_be_true(); // upper-right
		expect(d.lit(ix1 + 19, 32)).to_be_true(); // lower-right

		// '3' (0x79): all three bars, both right uprights, neither left one.
		const int32_t ix3 = 99 + 1;
		expect(d.lit(ix3 + 3, 7)).to_be_true();
		expect(d.lit(ix3 + 3, 24)).to_be_true();
		expect(d.lit(ix3 + 3, 41)).to_be_true();
		expect(d.lit(ix3 + 1, 32)).to_be_false();
		expect(d.lit(ix3 + 19, 32)).to_be_true();

		// And no decimal point anywhere: none of the four bytes sets bit 7.
		expect(d.lit(22, 42)).to_be_false();
	});

	it("draws the decimal point when bit 7 is set, and puts the digits at the same canvas rows on a 64-row panel", _ {
		constexpr std::array<uint8_t, 4> dotted{0x7E | 0x80, 0x30, 0x6D, 0x79 | 0x80};
		uitest::Rig48 r;
		segment_digits(r.frame, r.frame.bounds(), std::span<const uint8_t, 4>{dotted});
		legacy::renderEmulated7Seg(r.reference, dotted);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect(r.lit(22, 42)).to_be_true(); // invert_area(ix + 21, 3, 41, 43)

		// On a flush panel the same constants land on the same canvas rows, which is legacy with
		// OLED_MAIN_TOPMOST_PIXEL == 0.
		uitest::Rig64 r64;
		segment_digits(r64.frame, r64.frame.bounds(), std::span<const uint8_t, 4>{kBriefBytes});
		legacy::renderEmulated7Seg(r64.reference, kBriefBytes);
		expect(r64.pbm()).to_equal(r64.reference_pbm());
		expect_golden(self, "../ui/widgets/segment_digits_0123_64.pbm", r64.pbm());
	});

	it("lights the VU meter one segment at a time", _ {
		// No legacy pixels; the eight steps are getMaxYDisplayForVUMeter's eight grid rows.
		uitest::Rig48 silent;
		vu(silent.frame, Rect{4, 4, 120, 18}, 0, 0);
		bool any = false;
		for (int32_t y = 0; y < 48; y++) {
			for (int32_t x = 0; x < 128; x++) {
				any = any || silent.lit(x, y);
			}
		}
		expect(any).to_be_false();

		uitest::Rig48 half;
		vu(half.frame, Rect{4, 4, 120, 18}, 127, 255);
		const int32_t top = half.frame.origin.y;
		// Left channel at 127: four of eight segments lit. Each segment is `rect.w / 8` = 15
		// columns wide *less one for the gap*, so segment i inks columns 4 + 15i .. 4 + 15i + 13
		// and column 4 + 15i + 14 is dark. Without that gap the four segments would be one
		// unbroken 60-column bar, indistinguishable from a `unipolar_bar`.
		expect(half.lit(4, top + 4)).to_be_true();       // segment 0 starts
		expect(half.lit(4 + 13, top + 4)).to_be_true();  // and ends 14 columns later
		expect(half.lit(4 + 14, top + 4)).to_be_false(); // the gap
		expect(half.lit(4 + 15, top + 4)).to_be_true();  // segment 1 starts
		expect(half.lit(4 + 29, top + 4)).to_be_false(); // the gap after segment 1
		expect(half.lit(4 + 44, top + 4)).to_be_false(); // and after segment 2
		expect(half.lit(4 + 58, top + 4)).to_be_true();  // segment 3's last column
		expect(half.lit(4 + 59, top + 4)).to_be_false(); // nothing past four segments
		// Every gap column, and only the gap columns, dark across the lit run.
		for (int32_t i = 0; i < 4; i++) {
			expect(half.lit(4 + 15 * i + 14, top + 4)).to_be_false();
		}
		// Right channel at 255: all eight, last segment ending at 4 + 120 - 2.
		expect(half.lit(4 + 118, top + 13)).to_be_true();
		expect(half.lit(4 + 119, top + 13)).to_be_false();
		expect(half.lit(4 + 104, top + 13)).to_be_false(); // the gap before segment 7
		expect_golden(self, "../ui/widgets/vu_half_full_48.pbm", half.pbm());

		uitest::Rig64 r64;
		vu(r64.frame, Rect{4, 4, 120, 18}, 200, 60);
		expect_golden(self, "../ui/widgets/vu_64.pbm", r64.pbm());
	});

	it("draws an ADSR outline", _ {
		// Golden only; a/d/s/r = 0.2/0.3/0.7/0.4.
		uitest::Rig48 r;
		envelope(r.frame, Rect{4, 4, 120, 30}, 0.2f, 0.3f, 0.7f, 0.4f);
		expect_golden(self, "../ui/widgets/envelope_48.pbm", r.pbm());

		const int32_t top = r.frame.origin.y;
		expect(r.lit(4, top + 33)).to_be_true(); // the ramp starts at the rect's bottom-left

		uitest::Rig64 r64;
		envelope(r64.frame, Rect{4, 4, 120, 30}, 0.2f, 0.3f, 0.7f, 0.4f);
		expect_golden(self, "../ui/widgets/envelope_64.pbm", r64.pbm());

		// A different envelope is a different picture.
		uitest::Rig48 other;
		envelope(other.frame, Rect{4, 4, 120, 30}, 0.9f, 0.1f, 0.2f, 0.9f);
		expect(r.pbm() == other.pbm()).to_be_false();
	});

	it("passes a graphic and an icon straight through to the canvas", _ {
		uitest::Rig48 r;
		icon(r.frame, Point{10, 10}, icons::knobArc);
		r.reference.draw_icon(icons::knobArc, 10, 15); // + the frame's origin
		expect(r.pbm()).to_equal(r.reference_pbm());

		uitest::Rig48 g;
		const Rect at{10, 10, icons::knobArc.width, icons::knobArc.height};
		graphic(g.frame, at, icons::knobArc.data, icons::knobArc.width, icons::knobArc.height,
		        icons::knobArc.num_bytes_tall);
		g.reference.draw_graphic_multi_line(icons::knobArc.data, 10, 15, icons::knobArc.width, icons::knobArc.height,
		                                    icons::knobArc.num_bytes_tall);
		expect(g.pbm()).to_equal(g.reference_pbm());
		expect(g.pbm()).to_equal(r.pbm()); // draw_icon is draw_graphic_multi_line
	});
});
DELUGE_SPEC(ui_misc_oled_spec, misc_oled_w)
