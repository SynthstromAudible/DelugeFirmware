#include "ui/views/oled/detail/layout.h"
#include "ui/views/oled/recording.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"
#include <array>
#include <cstdint>

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;

namespace {

constexpr std::array<RowText, 3> kSources{{{"LEAD 2", "MIDI"}, {"VOX", "INPUT L"}, {"BREAK 04", "MIX"}}};
constexpr CountIn kCountIn{.title = "COUNT-IN", .note = "REC", .beat = "3", .beats = 4, .beats_done = 1};
constexpr Overdub kOverdub{.title = "OVERDUB", .pass = "PASS 3", .position = "2.3", .progress = 0.55f};
constexpr Resampling kResampling{
    .title = "RESAMPLING", .elapsed = "0:12.4", .stop_label = "LOOP END", .level_l = 0.64f, .level_r = 0.61f};
constexpr StemExport kStems{.title = "EXPORT STEMS",
                            .of = "3/8",
                            .stem = "BASS",
                            .time = "0:42",
                            .next_label = "NEXT",
                            .next = "MIXDOWN",
                            .done = 5,
                            .total = 12};

} // namespace

// clang-format off
describe record_arming_v("ui::views::oled::record_arming", $ {
	it("draws arming with no sources", _ {
		uitest::Rig48 r;
		record_arming(r.frame, {.title = "ARMED", .count = "0", .sources = {}});
		expect(lit_in(r, 0, 0, 128, r.style.title_height)).to_be_greater_than(0);
		expect(lit_in(r, 0, r.style.title_height + 1, 128, 43 - r.style.title_height - 1)).to_equal(0);
	});

	it("matches its goldens", _ {
		const RecordArming d{.title = "ARMED", .count = "3", .sources = kSources};
		uitest::Rig48 r48;
		record_arming(r48.frame, d);
		expect_golden(self, "ui/views/oled/record_arming_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		record_arming(r64.frame, d);
		expect_golden(self, "ui/views/oled/record_arming_64.pbm", r64.pbm());
	});
});

describe count_in_v("ui::views::oled::count_in", $ {
	it("clamps beats done to the beat count", _ {
		uitest::Rig48 a;
		CountIn d = kCountIn;
		d.beats_done = 9;
		count_in(a.frame, d);
		uitest::Rig48 b;
		d.beats_done = 4;
		count_in(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("puts the beats on the last five rows", _ {
		uitest::Rig48 r;
		count_in(r.frame, kCountIn);
		expect(r.lit(44, 40 + kTop)).to_be_true();  // inside the first box (filled), x 40..47
		expect(r.lit(57, 40 + kTop)).to_be_false(); // inside the second (outlined), x 53..60
		expect(lit_in(r, 0, 38, 128, 5)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		count_in(r48.frame, kCountIn);
		expect_golden(self, "ui/views/oled/count_in_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		count_in(r64.frame, kCountIn);
		expect_golden(self, "ui/views/oled/count_in_64.pbm", r64.pbm());
	});
});

describe overdub_v("ui::views::oled::overdub", $ {
	it("clamps a progress past 1", _ {
		uitest::Rig48 a;
		Overdub d = kOverdub;
		d.progress = 2.f;
		overdub(a.frame, d);
		uitest::Rig48 b;
		d.progress = 1.f;
		overdub(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		overdub(r48.frame, kOverdub);
		expect_golden(self, "ui/views/oled/overdub_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		overdub(r64.frame, kOverdub);
		expect_golden(self, "ui/views/oled/overdub_64.pbm", r64.pbm());
	});
});

describe resampling_v("ui::views::oled::resampling", $ {
	it("stops the elapsed time short of a long stop label", _ {
		uitest::Rig48 r;
		Resampling d = kResampling;
		d.stop_label = "STOP AT 4 BARS"; // 14 characters: starts at column 128 - 84 = 44
		resampling(r.frame, d);
		const int32_t top = detail::row_y(r.frame, 0);
		expect(lit_in(r, 44 - 4, top, 4, 16)).to_equal(0);
		expect(lit_in(r, 0, top, 40, 16)).to_be_greater_than(0);
	});

	it("keeps a long title off the record mark", _ {
		uitest::Rig48 r;
		Resampling d = kResampling;
		d.title = "RESAMPLING THE MASTER";
		resampling(r.frame, d);
		expect(lit_in(r, 128 - 7 - 4, 0, 4, r.style.title_height)).to_equal(0);
		expect(r.lit(128 - 5, 5 + kTop)).to_be_true(); // the mark
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		resampling(r48.frame, kResampling);
		expect_golden(self, "ui/views/oled/resampling_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		resampling(r64.frame, kResampling);
		expect_golden(self, "ui/views/oled/resampling_64.pbm", r64.pbm());
	});
});

describe stem_export_v("ui::views::oled::stem_export", $ {
	it("draws a full bar for done past total", _ {
		uitest::Rig48 a;
		StemExport d = kStems;
		d.done = 20;
		stem_export(a.frame, d);
		uitest::Rig48 b;
		d.done = 12;
		stem_export(b.frame, d);
		expect(a.pbm()).to_equal(b.pbm());
	});

	it("draws an outline for no stems", _ {
		uitest::Rig48 r;
		StemExport d = kStems;
		d.done = 0;
		d.total = 0;
		stem_export(r.frame, d);
		expect(lit_count(r)).to_be_greater_than(0);
	});

	it("matches its goldens", _ {
		uitest::Rig48 r48;
		stem_export(r48.frame, kStems);
		expect_golden(self, "ui/views/oled/stem_export_48.pbm", r48.pbm());
		uitest::Rig64 r64;
		stem_export(r64.frame, kStems);
		expect_golden(self, "ui/views/oled/stem_export_64.pbm", r64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(recording_spec, record_arming_v, count_in_v, overdub_v, resampling_v, stem_export_v)
