#include "dsp/plaits/memory_pool.h"
#include "dsp/plaits/voice.h"
#include "processing/plaits/engine_parameter.h"
#include "sequencer/mdd/controller.hpp"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <random>

int main() {
	using namespace deluge::plaits_control;
	std::mt19937 random(12345);
	for (int i = 0; i < 100000; ++i) {
		int32_t value = static_cast<int32_t>(random());
		for (int base = 0; base < 3; ++base) {
			assert(modelFromFinal(base, value) == std::clamp(base + int(std::lround(value / 536870912.0)), 0, 2));
			assert(modelFromPreset(base, value) == std::clamp(base + int(std::lround(value / 1073741824.0)), 0, 2));
		}
	}
	for (int base = 0; base < 3; ++base)
		for (int current = 0; current < 3; ++current)
			for (int target = 0; target < 3; ++target)
				assert(stableModelFromFinal(base, (target - base) * 536870912, current) == target);
	int current = 0;
	for (int i = 0; i < 1000; ++i) {
		current = stableModelFromFinal(0, 268435456 + (i % 2 ? 100 : -100), current);
		assert(current == 0);
	}
	assert(stableModelFromFinal(0, 270000000, 0) == 1);
	assert(stableModelFromFinal(0, 267000000, 1) == 0);

	for (unsigned model = 0; model < 3; ++model) {
		deluge::dsp::PlaitsVoice deferred(0, true), direct(model);
		deferred.setModelFromFinal(0, model * 536870912);
		float a[257], b[257];
		deferred.render(42852281, .4f, .5f, .6f, a, 257);
		direct.render(42852281, .4f, .5f, .6f, b, 257);
		for (int i = 0; i < 257; ++i)
			assert(a[i] == b[i]);
	}
	{ deluge::dsp::PlaitsVoice neverRendered(0, true); }

	deluge::dsp::PlaitsMemoryPool<8> pool;
	int allocations = 0, frees = 0;
	auto allocate = [&] {
		++allocations;
		return std::malloc(32);
	};
	auto free = [&](void* p) {
		++frees;
		std::free(p);
	};
	for (int i = 0; i < 1000; ++i)
		pool.release(pool.acquire(allocate), false, free);
	assert(allocations == 1 && frees == 0);
	void* blocks[10];
	for (auto& p : blocks)
		p = pool.acquire(allocate);
	for (auto p : blocks)
		pool.release(p, false, free);
	assert(allocations == 10 && frees == 2);
	pool.release(nullptr, true, free);
	assert(frees == allocations);
	pool.release(pool.acquire(allocate), true, free);
	assert(frees == allocations);

	snake::Controller c;
	c.choose(3);
	for (int slot = 0; slot < 4; ++slot) {
		c.routes[0] = {};
		c.routes[0][slot].target = 42;
		assert(c.engineValues(42, 43));
		c.routes[0][(slot + 1) % 4].target = 17;
		assert(!c.engineValues(42, 43));
	}
	c.routes[0] = {};
	assert(!c.engineValues(42, 43));
	for (unsigned quarter : {24u, 96u, 192u}) {
		c.syncClock(quarter);
		double previous = 1e10;
		unsigned seen = 0;
		for (int index : snake::Controller::speedMenuOrder) {
			assert(!(seen & (1u << index)));
			seen |= 1u << index;
			c.setSpeed(c.core.lanes[0], index);
			assert(c.speedIndex(c.core.lanes[0]) == index);
			assert(snake::Controller::speedNames.at(index)[0] == '1');
			double duration = double(c.core.lanes[0].ticks) / c.core.lanes[0].tickDenominator;
			assert(duration < previous);
			previous = duration;
		}
		assert(seen == 32767);
	}
	std::cout << "PASS RC2: rounding, model hysteresis, deferred output, bounded pool, mixed maps and all 15 speeds\n";
}
