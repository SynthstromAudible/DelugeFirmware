#include "io/midi/midi_queue.h"

#include <cstdint>

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe output_time_group("output time", ${
	it("window start is when the playhead reaches it", _ {
		// 441 samples is 10ms
		expect(outputTimeAtWindowOffset(1000, 441, 0)).to_equal(1000u + 10'000u);
	});

	it("offsets within the window are later by sample count", _ {
		OutputTime start = outputTimeAtWindowOffset(5000, 128, 0);
		OutputTime later = outputTimeAtWindowOffset(5000, 128, 441);
		// 441 samples later is 10ms, give or take the Q8 rounding of a sample's length
		expect(static_cast<int32_t>(later - start)).to_be_within(10).of(10'000);
	});

	it("never goes backwards across a window", _ {
		OutputTime previous = outputTimeAtWindowOffset(0, 64, 0);
		for (int32_t offset = 1; offset <= 128; offset++) {
			OutputTime next = outputTimeAtWindowOffset(0, 64, offset);
			expect(static_cast<int32_t>(next - previous)).not_().to_be_less_than(0);
			previous = next;
		}
	});
});

DELUGE_SPEC(midi_queue_spec, output_time_group)
