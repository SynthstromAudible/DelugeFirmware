#include "ui/views/oled/clip.h"
#include "ui/views/oled/detail/layout.h"
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

namespace {

InstrumentClipResting instrument() {
	return {
	    .name =
	        {.type_text = "SYNTH", .name = "LEAD 2", .slot = 0, .sub_slot = -1, .name_empty = false, .edited = false},
	    .length_ticks = kClipTicks,
	    .notes = kClip,
	    .low = kLow,
	    .high = kHigh,
	    .window_start = 16 * kStep,
	    .window_len = 16 * kStep,
	    .playhead = 18 * kStep};
}

AudioClipResting audio(std::span<const widgets::core::PeakCol> peaks) {
	return {.name = "BREAK 04",
	        .length = "1.84S",
	        .pitch = "C#3",
	        .peaks = peaks,
	        .window_x = 63,
	        .window_w = 34,
	        .playhead_x = 70};
}

constexpr std::array<widgets::oled::ListRow, 3> kKitRows{
    {{.label = "HAT CLOSED", .value = "16"}, {.label = "SNARE 2", .value = "4"}, {.label = "KICK 09", .value = "7"}}};

constexpr CvClipResting kCv{
    .title = "CV 1", .gate_label = "GATE 1", .voltage = "2.00V", .note = "C3", .gate_state = "GATE ON"};

} // namespace

// clang-format off
describe instrument_clip_v("ui::views::oled::instrument_clip_resting", $ {
	it("draws with no notes", _ {
		uitest::Rig48 r;
		InstrumentClipResting d = instrument();
		d.notes = {};
		d.window_len = 0;
		d.playhead = -1;
		instrument_clip_resting(r.frame, d);
		expect(lit_count(r)).to_be_greater_than(0);       // the name
		expect(lit_in(r, 0, 33, 128, 10)).to_equal(0);  // an empty strip
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		instrument_clip_resting(r48.frame, instrument());
		expect_golden(self, "ui/views/oled/instrument_clip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		instrument_clip_resting(r64.frame, instrument());
		expect_golden(self, "ui/views/oled/instrument_clip_64.pbm", r64.pbm());
	});
});

describe audio_clip_v("ui::views::oled::audio_clip_resting", $ {
	it("draws the title with no peaks", _ {
		uitest::Rig48 r;
		AudioClipResting d = audio({});
		d.window_w = 0;
		d.playhead_x = -1;
		audio_clip_resting(r.frame, d);
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		expect(lit_in(r, 0, 15, 128, 19)).to_equal(0);
	});

	it("keeps the length and pitch on the last row", _ {
		uitest::Rig48 r;
		const auto peaks = break_peaks(128);
		audio_clip_resting(r.frame, audio(peaks));
		const int32_t y = detail::last_row_y(r.frame);
		expect(lit_in(r, 0, y, 30, 8)).to_be_greater_than(0);
		expect(lit_in(r, 0, y - 1, 128, 1)).to_equal(0); // the waveform stops above it
	});

	it("matches its goldens", _ {
		const auto peaks = break_peaks(128);
		uitest::Rig48 r48;
		audio_clip_resting(r48.frame, audio(peaks));
		expect_golden(self, "ui/views/oled/audio_clip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		audio_clip_resting(r64.frame, audio(peaks));
		expect_golden(self, "ui/views/oled/audio_clip_64.pbm", r64.pbm());
	});
});

describe kit_clip_v("ui::views::oled::kit_clip_resting", $ {
	it("selects the current kit row", _ {
		uitest::Rig48 a;
		kit_clip_resting(a.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 1});
		uitest::Rig48 b;
		kit_clip_resting(b.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 2});
		expect(a.pbm()).not_().to_equal(b.pbm());
	});

	it("consumes no input", _ {
		uitest::Rig48 r;
		r.input.turn = 1;
		kit_clip_resting(r.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 1});
		uitest::Rig48 still;
		kit_clip_resting(still.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 1});
		expect(r.pbm()).to_equal(still.pbm());
	});

	it("draws the title with no rows", _ {
		uitest::Rig48 r;
		kit_clip_resting(r.frame, {.kit = "KIT 03", .rows = {}, .current = 0});
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		kit_clip_resting(r48.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 1});
		expect_golden(self, "ui/views/oled/kit_clip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		kit_clip_resting(r64.frame, {.kit = "KIT 03", .rows = kKitRows, .current = 1});
		expect_golden(self, "ui/views/oled/kit_clip_64.pbm", r64.pbm());
	});
});

describe cv_clip_v("ui::views::oled::cv_clip_resting", $ {
	it("draws with empty text", _ {
		uitest::Rig48 r;
		cv_clip_resting(r.frame, {});
		expect(lit_in(r, 0, r.style.title_height + 1, 128, 43 - r.style.title_height - 1)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		cv_clip_resting(r48.frame, kCv);
		expect_golden(self, "ui/views/oled/cv_clip_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		cv_clip_resting(r64.frame, kCv);
		expect_golden(self, "ui/views/oled/cv_clip_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(clip_spec, instrument_clip_v, audio_clip_v, kit_clip_v, cv_clip_v)
