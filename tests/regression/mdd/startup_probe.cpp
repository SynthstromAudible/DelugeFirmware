#include "sequencer/mdd/codec.hpp"
#include <cassert>
#include <iostream>
int main() {
	for (unsigned quarter : {6, 12, 24, 48, 96, 192, 384, 768, 1536}) {
		snake::Controller fresh;
		fresh.syncClock(quarter);
		for (auto& l : fresh.core.lanes) {
			assert(fresh.speedIndex(l) == 2);
			assert(uint64_t(l.ticks) * 4 == uint64_t(quarter) * l.tickDenominator);
		}
		if (quarter % 4 == 0) {
			assert(fresh.core.advance(0).trigger);
			assert(!fresh.core.advance(quarter / 4 - 1).trigger);
			assert(fresh.core.advance(quarter / 4).trigger);
		}
		auto text = snake::Codec::encode(fresh, true, 0);
		snake::Controller loaded;
		bool enabled = false;
		uint64_t tick = 0;
		assert(snake::Codec::decode(text.data(), loaded, enabled, tick, 64));
		assert(loaded.quarterTicks == quarter);
		loaded.syncClock(quarter);
		for (auto& l : loaded.core.lanes)
			assert(loaded.speedIndex(l) == 2);
		// First activation resets stored/legacy speeds to real 1/16. Later activations retain edits.
		for(auto& l : loaded.core.lanes) loaded.setSpeed(l,5);
		loaded.activateClock(quarter);
		for(auto& l : loaded.core.lanes) assert(loaded.speedIndex(l)==2);
		loaded.setSpeed(loaded.core.lanes[3],7);loaded.activateClock(quarter);
		assert(loaded.speedIndex(loaded.core.lanes[3])==7);
		// Older saved native timings remain unchanged when the song resolution is known.
		text[22] = text[23] = '0';
		snake::Controller legacy;
		legacy.syncClock(quarter);
		assert(snake::Codec::decode(text.data(), legacy, enabled, tick, 64));
		for (auto& l : legacy.core.lanes)
			assert(legacy.speedIndex(l) == 2);
		for (int speed = 0; speed < int(snake::Controller::speeds.size()); ++speed) {
			for (auto& l : fresh.core.lanes)
				fresh.setSpeed(l, speed);
			fresh.syncClock(24);
			fresh.syncClock(quarter);
			for (auto& l : fresh.core.lanes)
				assert(fresh.speedIndex(l) == speed);
		}
	}
	std::cout << "PASS startup: all seven lanes default to actual 1/16 at nine clock resolutions, saved timing "
	             "roundtrip, legacy native timing and all 15 plain/triplet/dotted speed rebindings and first-activation normalization\n";
}
