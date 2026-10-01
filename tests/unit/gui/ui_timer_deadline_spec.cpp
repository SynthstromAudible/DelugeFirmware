// tests/unit/gui/ui_timer_deadline_spec.cpp
#include "gui/ui_timer_manager.h"

#include "cppspec.hpp"

using deluge::runtime::Micros;

// clang-format off
describe ui_timer_deadline("uiTimerRunDelayUs", $ {
	it("waits at least 1 ms for a timer due sooner", _ {
		expect(uiTimerRunDelayUs(44)).to_equal(Micros{1'000});
	});

	it("waits until a timer's own time", _ {
		expect(uiTimerRunDelayUs(441)).to_equal(Micros{10'000});
	});

	it("waits at most 100 ms, however far off the next timer is", _ {
		expect(uiTimerRunDelayUs(44'100)).to_equal(Micros{100'000});
	});

	it("waits 1 ms for a timer already past its time", _ {
		expect(uiTimerRunDelayUs(-10)).to_equal(Micros{1'000});
	});
});

CPPSPEC_SPEC(ui_timer_deadline)
