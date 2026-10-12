#pragma once
#include "snake.hpp"
namespace snake {
// Fixed storage: no heap work in the transport callback. Same-pitch retriggers
// release the previous note first, since a MIDI note-off cannot identify voices.
struct HeldNotes {
	struct Note {
		uint8_t pitch = 0;
		uint64_t offAt = 0;
		bool active = false;
	};
	std::array<Note, 16> notes{};
	int latest = -1;
	template <class Off>
	void stop(Off off) {
		for (auto& n : notes)
			if (n.active) {
				off(n.pitch);
				n.active = false;
			}
		latest = -1;
	}
	template <class Off>
	void releaseDue(uint64_t tick, Off off, bool keepLatest = false) {
		for (unsigned i = 0; i < notes.size(); ++i)
			if (notes[i].active && notes[i].offAt <= tick && !(keepLatest && int(i) == latest)) {
				off(notes[i].pitch);
				notes[i].active = false;
			}
	}
	template <class On, class Off>
	void advance(const Frame& frame, uint64_t tick, On on, Off off) {
		const bool tie =
		    frame.trigger && frame.tie && latest >= 0 && notes[latest].active && notes[latest].offAt >= tick;
		releaseDue(tick, off, tie);
		if (!frame.trigger)
			return;
		if (tie) {
			notes[latest].offAt = tick + frame.duration + frame.overlap;
			return;
		}
		if (!frame.overlap)
			stop(off);
		// Protect repeated pitches from a delayed note-off killing the new trigger.
		for (auto& n : notes)
			if (n.active && n.pitch == frame.note) {
				off(n.pitch);
				n.active = false;
			}
		int slot = -1;
		for (unsigned i = 0; i < notes.size(); ++i)
			if (!notes[i].active) {
				slot = i;
				break;
			}
		if (slot < 0) {
			slot = 0;
			for (unsigned i = 1; i < notes.size(); ++i)
				if (notes[i].offAt < notes[slot].offAt)
					slot = i;
			off(notes[slot].pitch);
		}
		on(frame.note, frame.velocity);
		notes[slot] = {frame.note, tick + frame.duration + frame.overlap, true};
		latest = slot;
	}
	bool active() const {
		for (const auto& n : notes)
			if (n.active)
				return true;
		return false;
	}
};
} // namespace snake
