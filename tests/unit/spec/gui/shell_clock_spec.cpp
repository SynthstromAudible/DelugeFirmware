// The shell's millisecond clock over legacy's audio-sample counter: 44 samples a millisecond, as legacy's UI timers
// count them, the remainder carried to the next reading, and the count carried on across the counter's wrap.

#include "gui/ui/shell_clock.h"

#include "../support/spec_main.h"
#include "cppspec.hpp"
#include <cstdint>

using deluge::gui::ui::SampleClockMs;

// clang-format off
describe shell_clock("deluge::gui::ui::SampleClockMs", ${
	it("counts a millisecond per 44 samples", _ {
		SampleClockMs clock;
		expect(clock.advance(44)).to_equal(uint32_t{1});
		expect(clock.advance(4400)).to_equal(uint32_t{100});
	});
	it("carries the samples short of a millisecond to the next reading", _ {
		SampleClockMs clock;
		expect(clock.advance(43)).to_equal(uint32_t{0});
		expect(clock.advance(44)).to_equal(uint32_t{1});
		expect(clock.advance(87)).to_equal(uint32_t{1});
		expect(clock.advance(88)).to_equal(uint32_t{2});
	});
	it("keeps counting up across the counter's wrap", _ {
		SampleClockMs clock;
		const uint32_t before = clock.advance(0xFFFFFFF0u);
		const uint32_t after = clock.advance(0x10u + 44);
		// 0x10 samples to the wrap, 0x10 + 44 past it: 76 samples, one millisecond and the remainder before it
		expect(after - before).to_equal((uint32_t{0xFFFFFFF0u} % 44 + 0x20 + 44) / 44);
		expect(after > before).to_be_true();
	});
	it("does not move when the counter has not", _ {
		SampleClockMs clock;
		expect(clock.advance(1000)).to_equal(uint32_t{22});
		expect(clock.advance(1000)).to_equal(uint32_t{22});
	});
});
// clang-format on

DELUGE_SPEC(gui_shell_clock_spec, shell_clock)
