#include "ui/views/oled/recorder.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr RecorderStatus kWord{.mode = RecorderStatus::Mode::Word, .word = "WAITING", .meter = {}};
constexpr RecorderStatus kMeter{
    .mode = RecorderStatus::Mode::Meter,
    .word = {},
    .meter = {.level_end_x = {40, 52}, .threshold_x = 30, .recording = false, .blink_on = true}};

} // namespace

// clang-format off
describe recorder_status_v("ui::views::oled::recorder_status", $ {
	it("draws the word centred on its row and nothing above it", _ {
		uitest::Rig48 r;
		recorder_status(r.frame, kWord);
		expect(lit_in(r, 0, kStatusY, 128, 13)).to_be_greater_than(0);
		expect(lit_in(r, 0, 0, 128, kStatusY)).to_equal(0);
	});

	it("draws the meter instead of the word", _ {
		uitest::Rig48 w;
		recorder_status(w.frame, kWord);
		uitest::Rig48 m;
		recorder_status(m.frame, kMeter);
		expect(m.pbm() == w.pbm()).to_equal(false);
		expect(lit_in(m, 0, 0, 128, 43)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 w48;
		recorder_status(w48.frame, kWord);
		expect_golden(self, "ui/views/oled/recorder_word_48.pbm", w48.pbm());
		uitest::Rig64 w64;
		recorder_status(w64.frame, kWord);
		expect_golden(self, "ui/views/oled/recorder_word_64.pbm", w64.pbm());
		uitest::Rig48 m48;
		recorder_status(m48.frame, kMeter);
		expect_golden(self, "ui/views/oled/recorder_meter_48.pbm", m48.pbm());
		uitest::Rig64 m64;
		recorder_status(m64.frame, kMeter);
		expect_golden(self, "ui/views/oled/recorder_meter_64.pbm", m64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(recorder_spec, recorder_status_v)
