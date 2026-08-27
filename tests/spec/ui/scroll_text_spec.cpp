#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/oled/scroll_text.h"

#include "cppspec.hpp"

using namespace deluge::ui;
using namespace deluge::ui::oled;
using deluge::gfx::oled::FontId;
using deluge::gfx::oled::Rect;
namespace uitest = deluge::ui::test;

// clang-format off
describe scroll_text_w("ui::oled::scroll_text", $ {
	it("does not move when the text fits", _ {
		uitest::Rig48 r; ScrollText st;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "SHORT", FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(0));
		expect(r.gate.next_deadline().has_value()).to_be_false();
	});
	it("steps one pixel per interval and ping-pongs", _ {
		uitest::Rig48 r; ScrollText st;
		const char* s = "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL";
		r.input.now_ms = 0;   scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);   // arms
		expect(*r.gate.next_deadline()).to_equal(uint32_t(kScrollStepForwardMs));
		r.input.now_ms = 15;  scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(1));
		for (int t = 30; st.dir > 0; t += 15) { r.input.now_ms = t; scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st); }
		expect(st.dir).to_equal(int8_t(-1));                       // reached the end and paused
		expect(st.next_step_ms - r.input.now_ms).to_equal(uint32_t(kScrollTime));
	});
	it("resets to the start when motion is not allowed", _ {
		uitest::Rig48 r; ScrollText st; st.offset = 20;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL", FontId::Apple7, st, false);
		expect(st.offset).to_equal(int16_t(0));
		expect_golden(self, "../ui/widgets/scroll_text_start_48.pbm", r.pbm());
	});
});
// clang-format on

// Not CPPSPEC_SPEC(scroll_text_w): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e. `ui_scroll_text_spec`
// for ui/scroll_text_spec.cpp, which does not match the `scroll_text_w` describe-block name the
// macro would otherwise assume. Spelled out by hand, as ui/text_spec.cpp and
// ui/led_frame_spec.cpp do, so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern`.
int ui_scroll_text_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(scroll_text_w).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
