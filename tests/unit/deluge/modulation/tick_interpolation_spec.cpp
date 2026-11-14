#include "modulation/params/tick_interpolation.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe tick_interpolation_group("tick interpolation", ${
	it("every tick at normal tempos", _ {
		// 120 BPM at the default magnitude is ~230 samples per tick
		expect(ticksTilNextTickInterpolation(230)).to_equal(0);
		expect(ticksTilNextTickInterpolation(kMinSamplesBetweenTickInterpolations)).to_equal(0);
	});

	it("spaces out updates at fast tempos", _ {
		// "FAST" (10000 BPM) is under 3 samples per tick - updating every tick there renders audio a few samples at a time
		int32_t ticks = ticksTilNextTickInterpolation(3);
		expect(ticks * 3).not_().to_be_less_than(kMinSamplesBetweenTickInterpolations);
		expect((ticks - 1) * 3).to_be_less_than(kMinSamplesBetweenTickInterpolations);
	});

	it("handles sub sample ticks", _ {
		// Above ~27500 BPM a tick rounds down to 0 samples
		expect(ticksTilNextTickInterpolation(0)).to_equal(kMinSamplesBetweenTickInterpolations);
		expect(ticksTilNextTickInterpolation(1)).to_equal(kMinSamplesBetweenTickInterpolations);
	});
});

DELUGE_SPEC(tick_interpolation_spec, tick_interpolation_group)
