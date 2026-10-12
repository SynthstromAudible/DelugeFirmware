#include "sequencer/mdd/bake.hpp"
#include "sequencer/mdd/codec.hpp"
#include "sequencer/mdd/held_notes.hpp"
#include <cassert>
#include <iostream>
#include <vector>
struct Event {
	uint64_t tick;
	int pitch;
	bool on;
};
int main() {
	snake::Core c;
	for (auto& l : c.lanes)
		l.ticks = 6;
	c.lanes[0].values[0] = 60;
	c.lanes[0].values[1] = 62;
	c.lanes[0].values[2] = 64;
	c.ties = 1u << 1;
	snake::HeldNotes held;
	std::vector<Event> events;
	uint64_t now = 0;
	auto on = [&](uint8_t p, uint8_t) { events.push_back({now, p, true}); };
	auto off = [&](uint8_t p) { events.push_back({now, p, false}); };
	for (now = 0; now < 18; ++now)
		held.advance(c.advance(now), now, on, off);
	assert(events[0].on && events[0].pitch == 60);
	for (auto e : events)
		assert(!(e.on && e.tick == 6));
	bool extended = false;
	for (auto e : events)
		if (!e.on && e.pitch == 60 && e.tick == 9)
			extended = true;
	assert(extended);
	held.stop(off);
	assert(!held.active());
	c.ties = 0;
	c.overlap = 65535;
	events.clear();
	held = {};
	for (now = 0; now < 13; ++now)
		held.advance(c.advance(now), now, on, off);
	int onset = -1, release = -1;
	for (unsigned i = 0; i < events.size(); ++i) {
		if (events[i].on && events[i].pitch == 62)
			onset = i;
		if (!events[i].on && events[i].pitch == 60)
			release = i;
	}
	assert(onset >= 0 && release > onset);
	held.stop(off);
	assert(!held.active());
	// Legato must overlap even with a short gate and zero percentage.
	c.overlap = 0;
	c.legato = true;
	events.clear();
	held = {};
	for (now = 0; now < 8; ++now)
		held.advance(c.advance(now), now, on, off);
	onset = release = -1;
	for (unsigned i = 0; i < events.size(); ++i) {
		if (events[i].on && events[i].pitch == 62)
			onset = i;
		if (!events[i].on && events[i].pitch == 60)
			release = i;
	}
	assert(onset >= 0 && release > onset);
	held.stop(off);
	c.legato = false;
	c.overlap = 65535;
	// Same-pitch overlap must not leave a delayed note-off for the new note.
	c.lanes[0].values.fill(60);
	events.clear();
	held = {};
	for (now = 0; now < 20; ++now)
		held.advance(c.advance(now), now, on, off);
	assert(held.active());
	held.stop(off);
	// Cross-loop tie is one wrapping normal-grid note, not two retriggers.
	c.overlap = 0;
	c.ties = 1;
	c.lanes[0].values[0] = 60;
	c.lanes[0].values[1] = 62;
	c.lanes[0].length = 2;
	std::vector<std::array<uint32_t, 3>> notes;
	assert(snake::bake(c, 12, [&](uint32_t p, uint32_t n, const snake::Frame& f) {
		notes.push_back({p, n, f.note});
		return true;
	}));
	assert(notes.size() == 1 && notes[0][0] == 6 && notes[0][1] == 9 && notes[0][2] == 62);
	c.ties = 0xffff;
	notes.clear();
	assert(snake::bake(c, 12, [&](uint32_t p, uint32_t n, const snake::Frame& f) {
		notes.push_back({p, n, f.note});
		return true;
	}));
	assert(notes.size() == 1 && notes[0][1] == 12);
	// Legato WRITE has an actual one-tick overlap despite 50% gate.
	c.ties = 0;
	c.overlap = 0;
	c.legato = true;
	c.lanes[0].length = 4;
	c.lanes[0].values[0] = 60;
	c.lanes[0].values[1] = 62;
	c.lanes[0].values[2] = 64;
	c.lanes[0].values[3] = 65;
	notes.clear();
	assert(snake::bake(c, 24, [&](uint32_t p, uint32_t n, const snake::Frame& f) {
		notes.push_back({p, n, f.note});
		return true;
	}));
	assert(notes.size() == 4);
	for (auto n : notes)
		assert(n[1] == 7);
	// Exercise all directions/shapes with changes while notes are held.
	std::array<bool, 128> active{};
	held = {};
	uint32_t seed = 1;
	for (now = 0; now < 20000; ++now) {
		seed = seed * 1664525u + 1013904223u;
		if (now % 17 == 0) {
			c.ties = seed >> 16;
			c.overlap = seed;
			c.legato = seed & 1;
			c.lanes[0].direction = snake::Direction(seed % 4);
			c.lanes[0].shape = snake::Shape((seed >> 8) % 5);
			c.lanes[0].ticks = 1 + seed % 12;
		}
		held.advance(
		    c.advance(now), now,
		    [&](uint8_t p, uint8_t) {
			    assert(!active[p]);
			    active[p] = true;
		    },
		    [&](uint8_t p) {
			    assert(active[p]);
			    active[p] = false;
		    });
	}
	held.stop([&](uint8_t p) {
		assert(active[p]);
		active[p] = false;
	});
	for (bool n : active)
		assert(!n);
	// V1 still loads, V2 preserves tie and overlap fields.
	snake::Controller ui;
	ui.core.ties = 0x1234;
	ui.core.overlap = 54321;
	ui.core.legato = true;
	auto text = snake::Codec::encode(ui, false, 0);
	snake::Controller decoded;
	bool enabled = false;
	uint64_t tick = 0;
	assert(snake::Codec::decode(text.data(), decoded, enabled, tick, 128));
	assert(decoded.core.ties == 0x1234 && decoded.core.overlap == 54321 && decoded.core.legato);
	assert(snake::Codec::decode(std::string_view(text.data(), snake::Codec::legacyBytes * 2), decoded, enabled, tick,
	                            128));
	assert(!decoded.core.ties && !decoded.core.overlap && !decoded.core.legato);
	std::cout << "PASS true tie, overlap event ordering, same-pitch cleanup, wrapping WRITE and V1/V2 codec\n";
}
