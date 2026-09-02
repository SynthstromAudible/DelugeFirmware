#include "../gfx/support/golden.h"
#include "gfx/oled/font.h"
#include "gfx/oled/icons.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/oled/sample_position.h"
#include "ui/oled/text.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
#include <cstdint>
#include <cstring>
#include <string>

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// `SampleMarkerEditor::renderOLED`'s body (gui/ui/sample_marker_editor.cpp:1135-1222) as a
// component. Every geometry assertion is compared against a transcription of legacy's own body,
// drawn into the rig's second canvas -- so a port that merely agrees with itself fails.

namespace {

constexpr int32_t kSpacingX = deluge::gfx::oled::kTextSpacingX; // legacy's smallTextSpacingX
constexpr int32_t kSpacingY = deluge::gfx::oled::kTextSpacingY; // legacy's smallTextSizeY

// `intToString(number, buffer, minNumDigits)` (foundation/string_format.c:35), for the reference
// render only.
void int_to_string(uint32_t number, char* buffer, int32_t min_digits) {
	int32_t digits = 1;
	for (uint32_t probe = number; probe >= 10; probe /= 10) {
		digits++;
	}
	if (digits < min_digits) {
		digits = min_digits;
	}
	buffer[digits] = 0;
	for (int32_t pos = digits; pos-- > 0;) {
		buffer[pos] = static_cast<char>('0' + (number % 10));
		number /= 10;
	}
}

// --- SampleMarkerEditor::renderOLED's body, transcribed with its gotos unrolled -----------------
//
// `fix_unit_letter_x` selects between legacy's literal behaviour and the port's. Legacy passes
// `smallTextSpacingX` where the x coordinate belongs (:1158, :1167); the port passes the running
// cursor. Both are reproduced here so the divergence is pinned by a passing comparison on each
// side, rather than asserted in prose.
template <typename Canvas>
void legacy_render_body(Canvas& canvas, int32_t y_first, uint32_t markerPosSamples, uint32_t sampleRate,
                        bool fix_unit_letter_x) {
	int32_t xPixel = 1;
	int32_t yPixel = y_first;

	uint32_t hours = 0;
	uint32_t minutes = 0;
	uint64_t hundredmilliseconds = static_cast<uint64_t>(markerPosSamples) * 100000 / sampleRate;

	bool printSeconds = true;
	if (hundredmilliseconds >= 6000000) {
		minutes = static_cast<uint32_t>(hundredmilliseconds / 6000000);
		hundredmilliseconds -= static_cast<uint64_t>(minutes) * 6000000;

		if (minutes >= 60) {
			hours = minutes / 60;
			minutes -= hours * 60;

			char buffer[12];
			int_to_string(hours, buffer, 1);
			canvas.draw_string(buffer, xPixel, yPixel, kSpacingX, kSpacingY);
			xPixel += static_cast<int32_t>(strlen(buffer)) * kSpacingX;

			canvas.draw_char('h', fix_unit_letter_x ? xPixel : kSpacingX, yPixel, kSpacingX, kSpacingY);
			xPixel += kSpacingX * 2;
		}

		char buffer[12];
		int_to_string(minutes, buffer, 1);
		canvas.draw_string(buffer, xPixel, yPixel, kSpacingX, kSpacingY);
		xPixel += static_cast<int32_t>(strlen(buffer)) * kSpacingX;

		canvas.draw_char('m', fix_unit_letter_x ? xPixel : kSpacingX, yPixel, kSpacingX, kSpacingY);
		xPixel += kSpacingX * 2;

		printSeconds = hundredmilliseconds != 0;
	}

	if (printSeconds) {
		int32_t numDecimalPlaces;
		if (hours != 0 || minutes != 0 || hundredmilliseconds >= 100000) {
			hundredmilliseconds /= 100;
			numDecimalPlaces = 3;
		}
		else {
			numDecimalPlaces = 2;
		}

		char buffer[14];
		int_to_string(static_cast<uint32_t>(hundredmilliseconds), buffer, numDecimalPlaces + 1);
		int32_t length = static_cast<int32_t>(strlen(buffer));
		memmove(&buffer[length - numDecimalPlaces + 1], &buffer[length - numDecimalPlaces],
		        static_cast<size_t>(numDecimalPlaces) + 1);
		buffer[length - numDecimalPlaces] = '.';

		canvas.draw_string(buffer, xPixel, yPixel, kSpacingX, kSpacingY);
		xPixel += (length + 1) * kSpacingX;

		if (hours != 0 || minutes != 0) {
			canvas.draw_char('s', xPixel, yPixel, kSpacingX, kSpacingY);
		}
		else {
			xPixel += kSpacingX;
			canvas.draw_string(numDecimalPlaces == 2 ? "msec" : "sec", xPixel, yPixel, kSpacingX, kSpacingY);
		}
	}

	yPixel += 11;
	xPixel = 1;

	canvas.draw_char('(', xPixel, yPixel, kSpacingX, kSpacingY);
	xPixel += kSpacingX;

	char buffer[12];
	int_to_string(markerPosSamples, buffer, 1);
	canvas.draw_string(buffer, xPixel, yPixel, kSpacingX, kSpacingY);
	xPixel += kSpacingX * (static_cast<int32_t>(strlen(buffer)) + 1);

	canvas.draw_string("smpl)", xPixel, yPixel, kSpacingX, kSpacingY);
}

// The frame's origin already accounts for `visible_top`, so a frame-local y of 17 is legacy's
// `OLED_MAIN_TOPMOST_PIXEL + 17`; the reference canvas is absolute and needs the bezel added.
constexpr int32_t kBodyY = 17;

// Is there ink anywhere in the nine rows starting at canvas row @p y?
template <typename Rig>
bool any_ink_on_line(const Rig& r, int32_t y) {
	for (int32_t x = 0; x < 128; x++) {
		for (int32_t dy = 0; dy < 9; dy++) {
			if (r.lit(x, y + dy)) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

// clang-format off
describe sample_position_widget("ui::oled sample_position & title_badge", $ {

	it("draws milliseconds with two decimal places under a tenth of a second", _ {
		uitest::Rig48 r;
		// 4410 samples at 44100 Hz is 100 ms exactly -- 10000 in legacy's 10-microsecond unit,
		// which is under 100000 and so takes the two-decimal "msec" arm.
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, 4410, 44100);
		legacy_render_body(r.reference, kBodyY + 5, 4410, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		// Both lines carry ink: the duration on the first, "(4410 smpl)" eleven rows below it.
		expect(any_ink_on_line(r, kBodyY + 5)).to_be_true();
		expect(any_ink_on_line(r, kBodyY + 5 + kSamplePositionLineGap)).to_be_true();
		expect_golden(self, "../ui/widgets/sample_position_msec_48.pbm", r.pbm());
	});

	it("switches to three decimal places and 'sec' past a tenth of a second", _ {
		uitest::Rig48 r;
		// 220500 samples at 44100 Hz is 5 s: 500000 in legacy's unit, >= 100000, so /100 and three
		// decimals -- "5.000 sec".
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, 220500, 44100);
		legacy_render_body(r.reference, kBodyY + 5, 220500, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/sample_position_sec_48.pbm", r.pbm());
	});

	it("draws 0.00 msec at sample zero rather than nothing", _ {
		// Legacy's `goto printSeconds` skips the `if (hundredmilliseconds)` test, so a position of
		// zero still gets a figure. Dropping the goto and keeping the `if` -- the obvious
		// restructuring -- would print the sample line alone.
		uitest::Rig48 r;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, 0, 44100);
		legacy_render_body(r.reference, kBodyY + 5, 0, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		// Ink on the duration row, not just on the sample-count row.
		expect(any_ink_on_line(r, kBodyY + 5)).to_be_true();
	});

	it("spells minutes, and puts the unit letter after the digits where legacy puts it at x = 6", _ {
		uitest::Rig48 r;
		// 90 s at 44100 Hz: 1 minute plus 30 s -- the minutes arm, and the three-decimal 's' tail.
		const uint32_t samples = 90 * 44100;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, samples, 44100);

		// Against legacy AS WRITTEN, the two must differ: that is the divergence, asserted rather
		// than described.
		legacy_render_body(r.reference, kBodyY + 5, samples, 44100, false);
		expect(r.pbm() == r.reference_pbm()).to_be_false();

		// Against legacy with only the x coordinate corrected, they are identical -- so nothing
		// else about the minutes arm was changed while fixing it.
		r.reference.clear();
		legacy_render_body(r.reference, kBodyY + 5, samples, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/sample_position_minutes_48.pbm", r.pbm());
	});

	it("omits the seconds past a minute when they are exactly zero", _ {
		uitest::Rig48 r;
		const uint32_t samples = 60 * 44100;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, samples, 44100);
		legacy_render_body(r.reference, kBodyY + 5, samples, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
	});

	it("spells hours, minutes and seconds together", _ {
		uitest::Rig48 r;
		const uint32_t samples = (3600u + 23u * 60u + 45u) * 44100u;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, samples, 44100);
		legacy_render_body(r.reference, kBodyY + 5, samples, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/sample_position_hours_48.pbm", r.pbm());
	});

	it("draws only the sample count when the rate is zero", _ {
		// The one guard legacy has no need of. Without it this is a divide by zero.
		uitest::Rig48 r;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, 1234, 0);
		expect(any_ink_on_line(r, kBodyY + 5)).to_be_false();
		expect(any_ink_on_line(r, kBodyY + 5 + kSamplePositionLineGap)).to_be_true(); // "(1234 smpl)"
	});

	it("lays the same two lines out on a 64-row panel", _ {
		uitest::Rig64 r;
		sample_position(r.frame, Rect{1, kBodyY, 127, 2 * kSamplePositionLineGap}, 220500, 44100);
		// The 64-row geometry has no bezel, so the body starts eleven rows higher on the panel --
		// the SAME frame-local y, which is the point.
		legacy_render_body(r.reference, kBodyY, 220500, 44100, true);
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/sample_position_sec_64.pbm", r.pbm());
	});

	// --- title_badge --------------------------------------------------------------------------

	it("draws the lock badge at the panel's right edge with its ninth row filled", _ {
		uitest::Rig48 r;
		title(r.frame, "LOOP START");
		title_badge(r.frame, deluge::gfx::oled::icons::markerLock);

		r.reference.draw_screen_title("LOOP START");
		// `sample_marker_editor.cpp:1131-1132`, with OLED_MAIN_TOPMOST_PIXEL = 5 on this geometry.
		r.reference.draw_icon(deluge::gfx::oled::icons::markerLock, 128 - 10, 5 + 1);
		r.reference.invert_area(128 - 10, 7, 5 + 9, 5 + 9);
		expect(r.pbm()).to_equal(r.reference_pbm());

		// The filled ninth row: lit under the icon, and only for its seven columns.
		expect(r.lit(128 - 10, 5 + 9)).to_be_true();
		expect(r.lit(128 - 4, 5 + 9)).to_be_true();
		expect(r.lit(128 - 11, 5 + 9)).to_be_false();
		expect(r.lit(128 - 3, 5 + 9)).to_be_false();
		expect_golden(self, "../ui/widgets/title_badge_lock_48.pbm", r.pbm());
	});

	it("right-aligns the badge on a wider panel rather than at OLED_MAIN_WIDTH_PIXELS", _ {
		// Legacy hard-codes `OLED_MAIN_WIDTH_PIXELS - 10`; this reads the frame. Both shipped
		// panels are 128 wide, so only a narrowed frame can tell the two apart.
		uitest::Rig48 r;
		r.frame.region(Rect{0, 0, 100, 48}, [](OledFrame& f) {
			title_badge(f, deluge::gfx::oled::icons::markerLock);
		});
		expect(r.lit(100 - 10, 5 + 9)).to_be_true();
		expect(r.lit(128 - 10, 5 + 9)).to_be_false();
	});
});
// clang-format on
DELUGE_SPEC(ui_sample_position_spec, sample_position_widget)
