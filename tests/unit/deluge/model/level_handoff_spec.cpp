// The recorder's level hand-off from the render to the screen: a take finds the loudest means and the samples counted
// since the last take, leaves nothing behind, and what the render puts after it waits for the next one.

#include "model/sample/level_handoff.h"

#include "cppspec.hpp"
#include <cstdint>

// clang-format off
describe level_handoff("LevelHandoff", ${
	it("finds the loudest means and every sample put since the last take", _ {
		LevelHandoff handoff;
		handoff.put(10.f, 2.f, 128);
		handoff.put(4.f, 7.f, 128);
		handoff.put(6.f, 1.f, 64);
		const LevelHandoff::Taken taken = handoff.take();
		expect(taken.mean_l).to_equal(10.f);
		expect(taken.mean_r).to_equal(7.f);
		expect(taken.samples).to_equal(uint32_t{320});
	});
	it("leaves nothing for the next take when nothing more is put", _ {
		LevelHandoff handoff;
		handoff.put(10.f, 2.f, 128);
		(void)handoff.take();
		const LevelHandoff::Taken taken = handoff.take();
		expect(taken.samples).to_equal(uint32_t{0});
		expect(taken.mean_l).to_equal(0.f);
		expect(taken.mean_r).to_equal(0.f);
	});
	it("keeps what is put after a take for the take after, in either slot", _ {
		LevelHandoff handoff;
		for (int round = 0; round < 4; round++) {
			handoff.put(static_cast<float>(round + 1), 1.f, 100);
			const LevelHandoff::Taken taken = handoff.take();
			expect(taken.mean_l).to_equal(static_cast<float>(round + 1));
			expect(taken.samples).to_equal(uint32_t{100});
		}
	});
	it("keeps a block put while a take is under way for the next take", _ {
		LevelHandoff handoff;
		handoff.put(5.f, 5.f, 128);
		const uint32_t left = handoff.begin_take();
		handoff.put(9.f, 9.f, 64); // the render, preempting the screen between the take's halves
		const LevelHandoff::Taken first = handoff.finish_take(left);
		expect(first.mean_l).to_equal(5.f);
		expect(first.samples).to_equal(uint32_t{128});
		const LevelHandoff::Taken second = handoff.take();
		expect(second.mean_l).to_equal(9.f);
		expect(second.samples).to_equal(uint32_t{64});
	});
	it("starts empty after a clear", _ {
		LevelHandoff handoff;
		handoff.put(3.f, 3.f, 10);
		(void)handoff.take();
		handoff.put(3.f, 3.f, 10); // into the other slot
		handoff.clear();
		expect(handoff.take().samples).to_equal(uint32_t{0});
		expect(handoff.take().samples).to_equal(uint32_t{0});
	});
});
// clang-format on

CPPSPEC_SPEC(level_handoff)
