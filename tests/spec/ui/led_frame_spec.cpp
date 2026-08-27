#include "cppspec.hpp"
#include "ui/core/led_frame.h"

using namespace deluge::ui;

// clang-format off
describe led_frame("ui::LedFrame", $ {
	it("records entries in order and lets a later set win", _ {
		LedFrame f;
		f.set(LedId{3}, LedState{LedState::Kind::On, 0, 0});
		f.set(LedId{3}, LedState{LedState::Kind::Blink, 200, 0});
		f.ring(RingId::Lower, KnobRing{{1, 2, 3, 0}});
		expect(f.entries().size()).to_equal(size_t(1));
		expect(f.entries()[0].second.kind == LedState::Kind::Blink).to_be_true();
		expect(f.ring_or_null(RingId::Lower)->levels[2]).to_equal(uint8_t(3));
		expect(f.ring_or_null(RingId::Upper) == nullptr).to_be_true();
	});
});
// clang-format on

// Not CPPSPEC_SPEC(led_frame): create_test_sourcelist derives the extern function name from this
// file's path relative to tests/spec (slashes become underscores), i.e. `ui_led_frame_spec` for
// ui/led_frame_spec.cpp, which does not match the `led_frame` describe-block name the macro would
// otherwise assume. Spelled out by hand so the exported symbol matches what the generated driver
// (all_specs.cpp) declares `extern` (see ui/rect_spec.cpp for the same pattern).
int ui_led_frame_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(led_frame).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
