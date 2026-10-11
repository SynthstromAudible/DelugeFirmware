#include "sequencer/mdd/bake.hpp"
#include <cassert>
#include <iostream>
#include <vector>
struct Event {
	uint32_t pos, duration;
	snake::Frame frame;
};
int main() {
	for (int shape = 0; shape < 5; ++shape)
		for (int dir = 0; dir < 4; ++dir)
			for (int length = 1; length <= 16; ++length)
				for (int den = 1; den <= 4; ++den) {
					snake::Core core;
					for (int i = 0; i < 7; ++i) {
						auto& l = core.lanes[i];
						l.shape = snake::Shape(shape);
						l.direction = snake::Direction(dir);
						l.length = length;
						l.ticks = 7 + i;
						l.tickDenominator = den;
						l.shift = i;
						l.muted = 1u << (i + 2);
					}
					for (int i = 0; i < 16; ++i) {
						core.lanes[0].values[i] = 60 + i;
						core.lanes[1].values[i] = i * 4369;
						core.lanes[2].values[i] = 80 + i;
					}
					std::vector<Event> actual, expected;
					assert(snake::bake(core, 192, [&](auto p, auto d, auto f) {
						actual.push_back({p, d, f});
						return true;
					}));
					for (uint32_t t = 0; t < 192; ++t) {
						auto f = core.advance(t);
						if (f.trigger) {
							if (!expected.empty())
								expected.back().duration = std::min(expected.back().duration, t - expected.back().pos);
							expected.push_back({t, std::min(f.duration, 192 - t), f});
						}
					}
					assert(actual.size() == expected.size());
					for (unsigned i = 0; i < actual.size(); ++i) {
						auto& a = actual[i];
						auto& e = expected[i];
						assert(a.pos == e.pos && a.duration == e.duration && a.frame.note == e.frame.note
						       && a.frame.velocity == e.frame.velocity);
					}
				}
	snake::Core core;
	int calls = 0;
	assert(!snake::bake(core, 192, [&](auto, auto, auto) { return ++calls < 3; }));
	assert(calls == 3);
	assert(!snake::bake(core, 0, [](auto, auto, auto) { return true; }));
	core.lanes[0].muted = 65535;
	assert(snake::bake(core, 192, [](auto, auto, auto) {
		assert(false);
		return true;
	}));
	std::cout << "PASS bake: playback-equivalent notes/gates/velocity, 1280 shape/direction/length/speed combinations, "
	             "empty loop and allocation failure\n";
}
