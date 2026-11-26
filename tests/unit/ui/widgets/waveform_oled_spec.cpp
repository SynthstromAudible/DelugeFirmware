#include "support/frames.h"
#include "ui/widgets/core/peaks.h"
#include "ui/widgets/oled/waveform.h"
#include "unit/ui/gfx/support/golden.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>
#include <vector>

using namespace deluge::ui;
using namespace deluge::ui::widgets::oled;
using deluge::ui::gfx::oled::Rect;
using deluge::ui::widgets::core::PeakCol;
namespace uitest = deluge::ui::test;

namespace {

constexpr int32_t kOne = 1 << 24;

/// A burst: silence, a rising swell to full scale, an asymmetric tail, then uninvestigated columns.
std::vector<PeakCol> burst(int32_t n) {
	std::vector<PeakCol> cols(static_cast<size_t>(n));
	for (int32_t i = 0; i < n; i++) {
		PeakCol& c = cols[static_cast<size_t>(i)];
		if (i < 8) {
			c = {.min24 = 0, .max24 = 0, .valid = true};
		}
		else if (i < 40) {
			const int32_t a = (i - 8) * (kOne / 32);
			c = {.min24 = -a, .max24 = a, .valid = true};
		}
		else if (i < 100) {
			c = {.min24 = -kOne / 4, .max24 = kOne / 2, .valid = true};
		}
		else {
			c = {.valid = false};
		}
	}
	return cols;
}

} // namespace

// clang-format off
describe waveform_oled_w("ui::widgets::oled::waveform", $ {
	it("draws a valid column from its max row to its min row about the centre", _ {
		uitest::Rig48 r;
		const std::array<PeakCol, 1> one{{{.min24 = -kOne / 2, .max24 = kOne, .valid = true}}};
		waveform(r.frame, Rect{10, 10, 1, 21}, one); // centre row 20, half 10
		for (int32_t y = 10; y <= 25; y++) {
			expect(r.lit(10, y + 5)).to_be_true();
		}
		expect(r.lit(10, 9 + 5)).to_be_false();
		expect(r.lit(10, 26 + 5)).to_be_false();
	});

	it("draws silence as one pixel on the centre row, and an uninvestigated column as a dotted centre", _ {
		uitest::Rig48 r;
		const std::array<PeakCol, 4> cols{{{.valid = true}, {.valid = true}, {.valid = false}, {.valid = false}}};
		waveform(r.frame, Rect{0, 10, 4, 21}, cols);
		expect(r.lit(0, 20 + 5)).to_be_true();
		expect(r.lit(1, 20 + 5)).to_be_true();
		expect(r.lit(2, 20 + 5)).to_be_true();  // even x
		expect(r.lit(3, 20 + 5)).to_be_false(); // odd x
	});

	it("clamps beyond full scale to the rect", _ {
		uitest::Rig48 r;
		const std::array<PeakCol, 1> one{{{.min24 = -4 * kOne, .max24 = 4 * kOne, .valid = true}}};
		waveform(r.frame, Rect{10, 10, 1, 21}, one);
		expect(r.lit(10, 9 + 5)).to_be_false();
		expect(r.lit(10, 31 + 5)).to_be_false();
		expect(r.lit(10, 10 + 5)).to_be_true();
		expect(r.lit(10, 30 + 5)).to_be_true();
	});

	it("draws no more columns than the rect is wide", _ {
		uitest::Rig48 r;
		const std::vector<PeakCol> cols(20, PeakCol{.min24 = -kOne, .max24 = kOne, .valid = true});
		waveform(r.frame, Rect{0, 10, 10, 21}, cols);
		expect(r.lit(9, 20 + 5)).to_be_true();
		expect(r.lit(10, 20 + 5)).to_be_false();
	});

	it("draws a burst on both panels", _ {
		const std::vector<PeakCol> cols = burst(128);
		uitest::Rig48 r48;
		waveform(r48.frame, Rect{0, 13, 128, 29}, cols);
		expect_golden(self, "ui/widgets/waveform_oled_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		waveform(r64.frame, Rect{0, 13, 128, 49}, cols);
		expect_golden(self, "ui/widgets/waveform_oled_64.pbm", r64.pbm());
	});

	it("draws loop markers as the loop-point column does, beside a waveform", _ {
		const std::vector<PeakCol> cols = burst(128);
		uitest::Rig48 r48;
		looped_waveform(r48.frame, Rect{0, 13, 128, 29}, cols, 30, 96);
		// The start's icon begins 4 right of its line; the end's icon ends 4 left of its line.
		expect(r48.lit(30, 13 + 5)).to_be_true();  // the start line's first dot
		expect(r48.lit(30, 14 + 5)).to_be_false(); // dotted every 2
		expect_golden(self, "ui/widgets/waveform_oled_looped_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		looped_waveform(r64.frame, Rect{0, 13, 128, 49}, cols, 30, 96);
		expect_golden(self, "ui/widgets/waveform_oled_looped_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(waveform_oled_spec, waveform_oled_w)
