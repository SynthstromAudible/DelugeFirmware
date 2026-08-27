#include "cppspec.hpp"
#include "ui/core/repaint_gate.h"

using deluge::ui::RepaintGate;

// clang-format off
describe repaint_gate("ui::RepaintGate", $ {
	it("fires on the first frame, then only when asked", _ {
		RepaintGate g;
		expect(g.needs_frame(0, false, false)).to_be_true();     // first frame
		expect(g.needs_frame(10, false, false)).to_be_false();   // clean
		expect(g.needs_frame(20, true, false)).to_be_true();     // input
		expect(g.needs_frame(30, false, true)).to_be_true();     // model dirty
		g.request_repaint();
		expect(g.needs_frame(40, false, false)).to_be_true();
		expect(g.needs_frame(41, false, false)).to_be_false();   // consumed
	});
	it("honours a timed request and reports the deadline", _ {
		RepaintGate g; g.needs_frame(0, false, false);
		g.request_repaint_after(100, 0);
		expect(g.next_deadline().has_value()).to_be_true();
		expect(*g.next_deadline()).to_equal(100u);
		expect(g.needs_frame(50, false, false)).to_be_false();
		expect(g.needs_frame(100, false, false)).to_be_true();
		expect(g.next_deadline().has_value()).to_be_false();
	});
	it("keeps the earliest of several timed requests", _ {
		RepaintGate g; g.needs_frame(0, false, false);
		g.request_repaint_after(300, 0); g.request_repaint_after(120, 0);
		expect(*g.next_deadline()).to_equal(120u);
	});
});
// clang-format on

// Not CPPSPEC_SPEC(repaint_gate): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e. `ui_repaint_gate_spec`
// for ui/repaint_gate_spec.cpp, which does not match the `repaint_gate` describe-block name the
// macro would otherwise assume. Spelled out by hand so the exported symbol matches what the
// generated driver (all_specs.cpp) declares `extern` (see ui/rect_spec.cpp for the same pattern).
int ui_repaint_gate_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(repaint_gate).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
