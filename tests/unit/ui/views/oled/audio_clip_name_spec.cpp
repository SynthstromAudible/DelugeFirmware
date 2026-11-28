#include "ui/views/oled/audio_clip.h"
#include "unit/ui/gfx/support/golden.h"
#include "unit/ui/views/oled/support/fixtures.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::ui;
using namespace deluge::ui::views::oled;
using namespace deluge::ui::views::oled::test;
using deluge::ui::widgets::core::ScrollText;

namespace {

constexpr AudioClipName kNamed{.type_text = "AUDIO TRACK", .name = "VOX", .info = "1: VERSE"};
constexpr AudioClipName kUnnamed{.type_text = "AUDIO TRACK", .name = "", .info = "Section 1"};
constexpr AudioClipName kLongInfo{
    .type_text = "AUDIO TRACK", .name = "VOX", .info = "2: A VERY LONG CLIP NAME THAT CANNOT FIT ON THE PANEL"};

void draw(widgets::oled::OledFrame& f, const AudioClipName& d) {
	ScrollText name_scroll{};
	ScrollText info_scroll{};
	audio_clip_name(f, d, name_scroll, info_scroll);
}
void draw(uitest::Rig48& r, const AudioClipName& d) {
	draw(r.frame, d);
}
void draw(uitest::Rig64& r, const AudioClipName& d) {
	draw(r.frame, d);
}

} // namespace

// clang-format off
describe audio_clip_name_v("ui::views::oled::audio_clip_name", $ {
	it("draws the clip's line under a named output", _ {
		uitest::Rig48 named;
		draw(named, kNamed);
		uitest::Rig48 other;
		draw(other, {.type_text = "AUDIO TRACK", .name = "VOX", .info = "3: CHORUS"});
		expect(named.pbm() == other.pbm()).to_equal(false);
	});

	it("draws only the type over an unnamed output, whatever its clip line", _ {
		uitest::Rig48 unnamed;
		draw(unnamed, kUnnamed);
		uitest::Rig48 other;
		draw(other, {.type_text = "AUDIO TRACK", .name = "", .info = "Section 9"});
		expect(unnamed.pbm()).to_equal(other.pbm());
		expect(lit_count(unnamed)).to_be_greater_than(0);
	});

	it("side-scrolls a clip line too wide for the panel, and arms the panel's gate for its next step", _ {
		uitest::Rig48 r;
		ScrollText name_scroll{};
		ScrollText info_scroll{};
		r.begin_tick(0);
		audio_clip_name(r.frame, kLongInfo, name_scroll, info_scroll);
		expect(r.gate.next_deadline().has_value()).to_be_true();
		const std::string at0 = r.pbm();
		r.canvas.clear();
		r.begin_tick(2000);
		audio_clip_name(r.frame, kLongInfo, name_scroll, info_scroll);
		expect(r.pbm() == at0).to_equal(false);
	});

	it("matches its goldens", _ {
		uitest::Rig48 n48;
		draw(n48, kNamed);
		expect_golden(self, "ui/views/oled/audio_clip_name_named_48.pbm", n48.pbm());
		uitest::Rig64 n64;
		draw(n64, kNamed);
		expect_golden(self, "ui/views/oled/audio_clip_name_named_64.pbm", n64.pbm());
		uitest::Rig48 u48;
		draw(u48, kUnnamed);
		expect_golden(self, "ui/views/oled/audio_clip_name_unnamed_48.pbm", u48.pbm());
		uitest::Rig64 u64;
		draw(u64, kUnnamed);
		expect_golden(self, "ui/views/oled/audio_clip_name_unnamed_64.pbm", u64.pbm());
	});
});
// clang-format on

DELUGE_SPEC(audio_clip_name_spec, audio_clip_name_v)
