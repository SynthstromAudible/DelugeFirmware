#include "ui/views/oled/detail/layout.h"
#include "ui/views/oled/playing.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;
using widgets::oled::ListRow;

namespace {

AuditioningRow audition(std::span<const widgets::core::PeakCol> peaks) {
	return {.drum = "KICK 909", .row = "ROW 3", .velocity = "VEL 100", .length = "0.42S", .peaks = peaks};
}

constexpr StutterHeld kStutter{.title = "STUTTER", .rate = "1/16", .repeats = 16, .repeats_done = 5};
constexpr TapTempo kTap{.title = "TAP TEMPO", .tempo = "122.4", .taps = 4, .taps_done = 3};
constexpr MidiLearn kLearn{
    .title = "LEARN", .target = "LPF FREQ", .value = "74", .cc = "CC 74", .channel = "CH 1", .level = 0.58f};
constexpr std::array<ListRow, 3> kPatched{
    {{.label = "LPF FREQ", .value = "+32"}, {.label = "PITCH", .value = "-12"}, {.label = "OSC2 PW", .value = "+50"}}};
constexpr LoadingSong kLoading{
    .title = "LOADING", .song = "SONG 08", .label = "SAMPLES", .count = "12/40", .done = 12, .total = 40};

} // namespace

// clang-format off
describe auditioning_row_v("ui::views::oled::auditioning_row", $ {
	it("draws the title with no peaks", _ {
		uitest::Rig48 r;
		auditioning_row(r.frame, audition({}));
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		const int32_t top = detail::row_y(r.frame, 0);
		expect(lit_in(r, 0, top, 128, detail::last_row_y(r.frame) - top)).to_equal(0);
	});

	it("matches its goldens", _ {
		const auto peaks = break_peaks(128, 4.f, 0.f);
		uitest::Rig48 r48;
		auditioning_row(r48.frame, audition(peaks));
		expect_golden(self, "ui/views/oled/auditioning_row_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		auditioning_row(r64.frame, audition(peaks));
		expect_golden(self, "ui/views/oled/auditioning_row_64.pbm", r64.pbm());
	});
});

describe stutter_held_v("ui::views::oled::stutter_held", $ {
	it("fits sixteen repeats", _ {
		uitest::Rig48 r;
		stutter_held(r.frame, kStutter);
		// 16 boxes 6 wide and 2 apart: 126 columns from column 1, the first five filled
		expect(r.lit(3, 40 + kTop)).to_be_true();      // inside box 0
		expect(r.lit(4 + 15 * 8, 40 + kTop)).to_be_false(); // inside box 15, outlined
		expect(r.lit(1 + 15 * 8 + 5, 40 + kTop)).to_be_true();  // box 15's right edge
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		stutter_held(r48.frame, kStutter);
		expect_golden(self, "ui/views/oled/stutter_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		stutter_held(r64.frame, kStutter);
		expect_golden(self, "ui/views/oled/stutter_held_64.pbm", r64.pbm());
	});
});

describe tap_tempo_v("ui::views::oled::tap_tempo", $ {
	it("clamps taps done to the taps", _ {
		uitest::Rig48 a;
		TapTempo d = kTap;
		d.taps_done = 10;
		tap_tempo(a.frame, d);
		uitest::Rig48 b;
		d.taps_done = 4;
		tap_tempo(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		tap_tempo(r48.frame, kTap);
		expect_golden(self, "ui/views/oled/tap_tempo_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		tap_tempo(r64.frame, kTap);
		expect_golden(self, "ui/views/oled/tap_tempo_64.pbm", r64.pbm());
	});
});

describe midi_learn_v("ui::views::oled::midi_learn", $ {
	it("clamps a level past 1", _ {
		uitest::Rig48 a;
		MidiLearn d = kLearn;
		d.level = 4.f;
		midi_learn(a.frame, d);
		uitest::Rig48 b;
		d.level = 1.f;
		midi_learn(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		midi_learn(r48.frame, kLearn);
		expect_golden(self, "ui/views/oled/midi_learn_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		midi_learn(r64.frame, kLearn);
		expect_golden(self, "ui/views/oled/midi_learn_64.pbm", r64.pbm());
	});
});

describe mod_source_held_v("ui::views::oled::mod_source_held", $ {
	it("draws with no rows", _ {
		uitest::Rig48 r;
		mod_source_held(r.frame, {.title = "LFO 1 TO", .rows = {}});
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		expect(lit_in(r, 0, r.style.title_height + 1, 128, 43 - r.style.title_height - 1)).to_equal(0);
	});

	it("selects no row", _ {
		uitest::Rig48 r;
		mod_source_held(r.frame, {.title = "LFO 1 TO", .rows = kPatched});
		const int32_t top = r.style.title_height + 1;
		// no row is inverted: the far left of every row is dark
		expect(lit_in(r, 0, top, 1, 43 - top)).to_equal(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		mod_source_held(r48.frame, {.title = "LFO 1 TO", .rows = kPatched});
		expect_golden(self, "ui/views/oled/mod_source_held_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		mod_source_held(r64.frame, {.title = "LFO 1 TO", .rows = kPatched});
		expect_golden(self, "ui/views/oled/mod_source_held_64.pbm", r64.pbm());
	});
});

describe loading_song_v("ui::views::oled::loading_song", $ {
	it("draws an outline for nothing to load", _ {
		uitest::Rig48 r;
		LoadingSong d = kLoading;
		d.done = 0;
		d.total = 0;
		loading_song(r.frame, d);
		expect(lit_count(r)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		loading_song(r48.frame, kLoading);
		expect_golden(self, "ui/views/oled/loading_song_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		loading_song(r64.frame, kLoading);
		expect_golden(self, "ui/views/oled/loading_song_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(playing_spec, auditioning_row_v, stutter_held_v, tap_tempo_v, midi_learn_v, mod_source_held_v,
            loading_song_v)
