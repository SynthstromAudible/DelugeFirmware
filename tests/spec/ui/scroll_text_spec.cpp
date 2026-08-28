#include "../gfx/support/golden.h"
#include "support/frames.h"
#include "ui/core/metrics.h"
#include "ui/oled/scroll_text.h"

#include "../support/spec_main.h"
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
		expect(st.dir).to_equal(int8_t(1));
		expect(r.gate.next_deadline().has_value()).to_be_false();
	});
	it("resets a stale mid-scroll offset/direction when the text now fits (review C1)", _ {
		uitest::Rig48 r; ScrollText st; st.offset = 20; st.dir = -1;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "SHORT", FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(0));
		expect(st.dir).to_equal(int8_t(1));
		expect(r.gate.next_deadline().has_value()).to_be_false();
	});
	it("arms the legacy kScrollTime pause before the first pixel step, then steps one pixel per interval and ping-pongs (review I1)", _ {
		uitest::Rig48 r; ScrollText st;
		const char* s = "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL";
		r.input.now_ms = 0;            scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);   // arms the initial pause, legacy oled.cpp:913-914
		expect(*r.gate.next_deadline()).to_equal(uint32_t(kScrollTime));
		expect(st.offset).to_equal(int16_t(0));                   // still sitting still during the pause
		r.input.now_ms = kScrollTime;  scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);   // pause elapsed: first pixel step
		expect(st.offset).to_equal(int16_t(1));
		for (int t = kScrollTime + kScrollStepForwardMs; st.dir > 0; t += kScrollStepForwardMs) {
			r.input.now_ms = t; scroll_text(r.frame, Rect{6, 14, 122, 9}, s, FontId::Apple7, st);
		}
		expect(st.dir).to_equal(int8_t(-1));                       // reached the end and paused
		expect(st.next_step_ms - r.input.now_ms).to_equal(uint32_t(kScrollTime));
	});
	it("re-arms the initial pause when the text or rect changes underneath it (review I2)", _ {
		uitest::Rig48 r; ScrollText st;
		const char* s1 = "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL";
		r.input.now_ms = 0;             scroll_text(r.frame, Rect{6, 14, 122, 9}, s1, FontId::Apple7, st);
		r.input.now_ms = kScrollTime;   scroll_text(r.frame, Rect{6, 14, 122, 9}, s1, FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(1));                    // genuinely mid-scroll now

		// A different label, one character shorter (Apple7 is monospaced, so its pixel width
		// differs) -- called well past any due step, so only the content change can be why it
		// re-arms rather than keeps stepping.
		const char* s2 = "A DIFFERENT VERY LONG LABEL THAT ALSO OVERFLOWS";
		r.input.now_ms = kScrollTime + 1000;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, s2, FontId::Apple7, st);
		expect(st.offset).to_equal(int16_t(0));
		expect(st.dir).to_equal(int8_t(1));
		// Checked against state.next_step_ms directly, not r.gate.next_deadline(): the gate
		// keeps the *earliest* pending deadline until it's consumed by needs_frame() (never
		// called in this spec), so the very first call's 400ms request from above is still the
		// one it reports -- next_step_ms is the widget's own rearm, unaffected by that.
		expect(st.next_step_ms - r.input.now_ms).to_equal(uint32_t(kScrollTime));
	});
	it("resets to the start when motion is not allowed", _ {
		uitest::Rig48 r; ScrollText st; st.offset = 20; st.dir = -1;
		scroll_text(r.frame, Rect{6, 14, 122, 9}, "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL", FontId::Apple7, st, false);
		expect(st.offset).to_equal(int16_t(0));
		expect(st.dir).to_equal(int8_t(1));
		expect_golden(self, "../ui/widgets/scroll_text_start_48.pbm", r.pbm());
	});
	it("clips a scrolled string against a right edge interior to the canvas (review M3)", _ {
		uitest::Rig48 r; ScrollText st;
		const char* s = "A VERY LONG LABEL THAT DOES NOT FIT ON THE PANEL";
		Rect rect{6, 14, 100, 9}; // right() = 106, strictly inside the 128px canvas
		// Mirrors every scroll_text call onto a second canvas via the same raw Canvas calls
		// scroll_text itself makes (clear_area_exact + draw_string), rather than one isolated
		// reference draw at the end: draw_string's end_x is only a *bail-out* bound -- the last
		// visible character's glyph can overshoot it by up to one more character width
		// (canvas.h:159-160) -- and a fixed-size clear_area_exact never reaches that overshoot
		// band, so residual ink from an earlier call's overshoot at a different scroll_pos can
		// persist there across steps. That ghosting is a real, legacy-identical property of
		// repeatedly re-rendering a scroller with Canvas::draw_string/clear_area_exact, not a
		// port bug, so the reference has to accumulate the same call history to compare fairly.
		auto mirror = [&] {
			r.reference.clear_area_exact(rect.x, rect.y + 5, rect.right() - 1, rect.bottom() - 1 + 5);
			r.reference.draw_string(s, rect.x, rect.y + 5, 6, 9, st.offset, rect.right());
		};
		r.input.now_ms = 0;            scroll_text(r.frame, rect, s, FontId::Apple7, st); mirror();
		r.input.now_ms = kScrollTime;  scroll_text(r.frame, rect, s, FontId::Apple7, st); mirror(); // offset -> 1
		for (int i = 0; i < 9; i++) {
			r.input.now_ms += kScrollStepForwardMs;
			scroll_text(r.frame, rect, s, FontId::Apple7, st);
			mirror();
		}
		expect(st.offset).to_equal(int16_t(10));
		// Unclipped, the still-scrolling label would keep inking columns well past rect.right()
		// (the string is far longer than the 100px box); confirm nothing lands there, at a
		// column safely past even draw_string's documented end_x + text_width overshoot
		// allowance (canvas.h:159-160: up to one more character width, 6px here).
		for (int y = 19; y <= 27; y++) {
			expect(r.lit(120, y)).to_be_false();
		}
		expect(r.pbm()).to_equal(r.reference_pbm());
		expect_golden(self, "../ui/widgets/scroll_text_mid_48.pbm", r.pbm());
	});
	it("widens only the cleared rect for an explicit clear_pad_x, per legacy's clear-only +4px (review I3, oled.cpp:984-992)", _ {
		uitest::Rig48 r; ScrollText st;
		Rect rect{6, 14, 100, 9};
		// Simulate stray ink left just past the rect, as if a submenu icon area still held a
		// previous frame's pixels -- the case legacy's clear-only padding exists to erase.
		r.frame.pixel(rect.right() + 2, rect.y + 2);
		expect(r.lit(rect.right() + 2, rect.y + 2 + 5)).to_be_true();
		scroll_text(r.frame, rect, "SHORT", FontId::Apple7, st, true, false, 4);
		expect(r.lit(rect.right() + 2, rect.y + 2 + 5)).to_be_false(); // erased by the wider clear
		expect(st.offset).to_equal(int16_t(0));                       // the pad never touches the text/scroll math
	});
});
DELUGE_SPEC(ui_scroll_text_spec, scroll_text_w)
