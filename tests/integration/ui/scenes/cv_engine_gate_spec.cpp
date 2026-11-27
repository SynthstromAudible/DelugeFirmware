/// @file
/// @brief `CVEngine`'s output on stamps (libdeluge/timed_output.h): a note's CV goes before its gate, a gate-off goes
///        at once, and a gate-on waits the minimum off time after its off. Read from the host output service's log
///        (`support/cv_gate_out_tap.h`).

#include "processing/engines/cv_engine.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "cppspec.hpp"
#include "support/cv_gate_out_tap.h"
#include "support/spec_main.h"

namespace cv_gate_out = deluge::ui::scenes::spec::cv_gate_out;

namespace {

/// Restores gate 0 and CV channel 0 as the case found them.
struct KeepChannelZero {
	GateChannel gate = cvEngine.gateChannels[0];
	CVChannel cv = cvEngine.cvChannels[0];
	~KeepChannelZero() {
		cvEngine.gateChannels[0] = gate;
		cvEngine.cvChannels[0] = cv;
		(void)cv_gate_out::take();
	}
};

} // namespace

// clang-format off
describe cv_engine_gate_body("CVEngine on stamps", ${
	it("sends a note's CV before its gate", _ {
		KeepChannelZero keep;
		cvEngine.gateChannels[0].mode = GateType::V_TRIG;
		cvEngine.gateChannels[0].on = false;
		cvEngine.gateChannels[0].offStamp = DELUGE_STAMP_NOW;
		(void)cv_gate_out::take();
		cvEngine.sendNote(true, 0, 60);
		const std::string out = cv_gate_out::take();
		expect(cv_gate_out::count(out, "cv0=")).to_equal(size_t{1});
		expect(cv_gate_out::count(out, "gate0=")).to_equal(size_t{1});
		const bool cv_first = out.find("cv0=") < out.find("gate0=");
		expect(cv_first).to_be_true();
	});

	it("switches a gate off at once, dropping an edge still waiting for it", _ {
		KeepChannelZero keep;
		cvEngine.gateChannels[0].mode = GateType::V_TRIG;
		cvEngine.gateChannels[0].on = false;
		cvEngine.gateChannels[0].offStamp = DELUGE_STAMP_NOW;
		(void)cv_gate_out::take();
		cvEngine.sendNote(true, 0, 60);
		cvEngine.sendNote(false, 0, 60);
		// The on was still queued (nothing fires without a render), so the off replaced it: one edge, the off.
		const std::string out = cv_gate_out::take();
		expect(cv_gate_out::count(out, "gate0=")).to_equal(size_t{1});
		expect(cvEngine.gateChannels[0].on).to_be_false();
	});

	it("holds a gate-on until the minimum off time after its off", _ {
		// 10 is 1 ms: 44 frames.
		expect(CVEngine::onStampAfterOff(1000, 990, 10)).to_equal(990u + 44u);
		expect(CVEngine::onStampAfterOff(2000, 990, 10)).to_equal(2000u);
		expect(CVEngine::onStampAfterOff(DELUGE_STAMP_NOW, 990, 10)).to_equal(990u + 44u);
	});

	it("sends a gate-on as asked when its gate has no timed off", _ {
		expect(CVEngine::onStampAfterOff(DELUGE_STAMP_NOW, DELUGE_STAMP_NOW, 10)).to_equal(DELUGE_STAMP_NOW);
		expect(CVEngine::onStampAfterOff(1000, DELUGE_STAMP_NOW, 10)).to_equal(1000u);
	});

	it("counts the off time across the timeline's wrap", _ {
		expect(CVEngine::onStampAfterOff(10, UINT32_MAX - 20, 10)).to_equal(UINT32_MAX - 20 + 44);
	});
});
// clang-format on

DELUGE_SPEC(cv_engine_gate_spec, cv_engine_gate_body)
