#include "cppspec.hpp"
#include "harness/snapshot.h"
#include "harness/ui_script.h"

#include <string>
using namespace deluge::harness;
// clang-format off
describe ui_script("harness::ui_script", $ {
	it("parses every command with its arguments", _ {
		auto r = parse_script("0 board 128x48+5,128x64+0\n0 snapshot boot   # comment\n500 button Clip down\n520 button Clip up\n"
		                      "600 encoder Select +1\n700 pad 3 5 down 100\n750 pad 3 5 up\n800 released\n900 model ClipContent 3\n"
		                      "950 notice Error 248\n1000 snapshot after-clip\n1200 quit\n");
		expect(r.error.has_value()).to_be_false();
		expect(r.steps.size()).to_equal(size_t(12));
		expect(r.steps[2].kind).to_equal(StepKind::Button); expect(std::string_view(r.steps[2].name)).to_equal("Clip"); expect(r.steps[2].down).to_be_true();
		expect(r.steps[4].a).to_equal(1); expect(r.steps[5].a).to_equal(3); expect(r.steps[5].b).to_equal(5); expect(r.steps[5].c).to_equal(100);
		expect(r.steps[6].c).to_equal(0);                       // pad up -> velocity 0
		expect(r.steps[8].a).to_equal(3); expect(r.steps[9].a).to_equal(248);
		expect(r.steps[11].kind).to_equal(StepKind::Quit); expect(r.steps[11].at_ms).to_equal(uint32_t(1200));
	});
	it("defaults pad velocity to 255 and rejects unknown commands with the line number", _ {
		expect(parse_script("0 pad 1 1 down\n").steps[0].c).to_equal(255);
		auto bad = parse_script("0 snapshot a\n10 frobnicate\n");
		expect(bad.error.has_value()).to_be_true(); expect(bad.error->line).to_equal(uint16_t(2));
	});
	it("rejects non-monotonic times", _ { expect(parse_script("10 released\n5 released\n").error.has_value()).to_be_true(); });
	it("formats state lines sorted with scenes first", _ {
		StateLines s; add_pad_line(s, 4, 3, 10, 20, 30); add_pad_line(s, 0, 0, 0, 0, 0); s.add("scene Session"); s.add("scene Popup/General");
		add_led_line(s, "Play", false, 0); add_led_line(s, "Record", true, 500); uint8_t lv[4] = {3, 3, 2, 0}; add_indicator_line(s, true, lv);
		expect(s.sorted_text()).to_equal("scene Session\nscene Popup/General\nled Play on\nled Record blink 500\npad 4 3 10 20 30\nindicator upper 3 3 2 0\n");
	});
	it("keeps a full-length notice line intact", _ {
		// The device really does produce popup text this long -- `midi_follow.cpp:648-657` builds
		// "Can't control: \n<param name>" -- and a truncated `.state` line is a golden that has
		// silently stopped telling two screens apart. 60 characters of text plus the widest
		// realistic prefix must survive `add` untouched.
		const std::string text(60, 'x');
		StateLines s; s.add("notice Text " + text);
		expect(s.sorted_text()).to_equal("notice Text " + text + "\n");
		expect(std::string("notice Text " + text).size()).to_be_less_than(kStateLineBytes);
	});
	it("orders indicator/lock/notice by fixed category, ASCII-sorted within category", _ {
		StateLines s; uint8_t lv[4] = {1, 1, 1, 1};
		add_indicator_line(s, false, lv); add_indicator_line(s, true, lv);   // added lower-then-upper
		s.add("notice Error 248"); s.add("lock loading");                   // added notice-then-lock
		expect(s.sorted_text()).to_equal("indicator lower 1 1 1 1\nindicator upper 1 1 1 1\nlock loading\nnotice Error 248\n");
	});
});
// clang-format on
int harness_ui_script_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(ui_script).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
