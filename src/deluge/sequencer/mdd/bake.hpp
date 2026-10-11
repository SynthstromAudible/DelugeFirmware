#pragma once
#include "snake.hpp"
namespace snake {
// Render one stock clip loop at native tick resolution without touching live lane state.
// Emit monophonic notes, cutting the previous gate at the next trigger and loop end.
template <class Emit, class Service = decltype([] {})>
bool bake(Core core, uint32_t length, Emit emit, Service service = {}) {
	if (!length)
		return false;
	const auto& lane = core.lanes[0];
	const uint64_t numerator = std::max<uint32_t>(1, lane.ticks);
	const uint64_t denominator = std::max<unsigned>(1, lane.tickDenominator);
	bool pending = false;
	Frame previous;
	uint32_t start = 0;
	uint64_t tick = 0, step = 0;
	uint32_t iterations = 0;
	while (tick < length) {
		// Bound work even when every step is muted and emit is never called.
		if (iterations >= 1048576)
			return false;
		// Let the firmware service audio during an expensive offline WRITE.
		if ((iterations++ & 31u) == 0)
			service();
		auto frame = core.advance(tick);
		if (frame.trigger) {
			if (pending && !emit(start, std::min<uint32_t>(previous.duration, tick - start), previous))
				return false;
			previous = frame;
			start = tick;
			pending = true;
		}
		step = tick * denominator / numerator + 1;
		tick = std::max<uint64_t>(tick + 1, (step * numerator + denominator - 1) / denominator);
	}
	return !pending || emit(start, std::min(previous.duration, length - start), previous);
}
} // namespace snake
