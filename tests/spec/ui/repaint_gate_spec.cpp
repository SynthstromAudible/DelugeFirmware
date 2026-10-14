#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "support/frames.h"
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
		// Later call is the earlier deadline: a last-write-wins implementation would also land
		// on 120, so this ordering alone can't distinguish min-wins from last-wins.
		RepaintGate g1; g1.needs_frame(0, false, false);
		g1.request_repaint_after(300, 0); g1.request_repaint_after(120, 0);
		expect(*g1.next_deadline()).to_equal(120u);

		// Later call is the LARGER deadline: a last-write-wins implementation would overwrite
		// the pending 120 with 300 here, so this is the discriminating case.
		RepaintGate g2; g2.needs_frame(0, false, false);
		g2.request_repaint_after(120, 0); g2.request_repaint_after(300, 0);
		expect(*g2.next_deadline()).to_equal(120u);   // the later, larger request does not push it out
	});
	it("null_input starts every field at zero/false (keeps support/frames.h compiled)", _ {
		auto in = deluge::ui::test::null_input(42);
		expect(in.now_ms).to_equal(uint32_t(42));
		expect(in.turn).to_equal(int8_t(0));
		expect(in.select_pressed).to_be_false();
		expect(in.pads.empty()).to_be_true();
	});
});
DELUGE_SPEC(ui_repaint_gate_spec, repaint_gate)
