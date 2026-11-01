#include "notify/dirty_set.h"

namespace deluge::notify {

namespace {
DirtySet g_dirty;
const ClipBits kNoClips{};
} // namespace

DirtySet& dirty() {
	return g_dirty;
}

DirtySet::DirtySet(const DirtySet& o) {
	*this = o;
}

DirtySet& DirtySet::operator=(const DirtySet& o) {
	if (this == &o) { // the copies below are element-wise, so self-assignment is benign -- but this
		return *this; // is the one class in the tree doing hand-rolled atomics, so it is spelled out
	}
	for (size_t c = 0; c < kSlots; c++) {
		slots_[c].bits.store(o.slots_[c].bits.load(std::memory_order_relaxed), std::memory_order_relaxed);
		for (int s = 0; s < 2; s++) {
			for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
				slots_[c].clip_words[s][w].store(o.slots_[c].clip_words[s][w].load(std::memory_order_relaxed),
				                                 std::memory_order_relaxed);
			}
		}
	}
	for (int s = 0; s < 2; s++) {
		snapshot_[s] = o.snapshot_[s];
	}
	return *this;
}

void DirtySet::raise(Dirty d) {
	for (Slot& slot : slots_) {
		slot.bits.fetch_or(1u << std::to_underlying(d), std::memory_order_release);
	}
}

void DirtySet::raise_clip(Dirty d, uint16_t session_clip_index) {
	const int s = clip_slot(d);
	if (s >= 0 && session_clip_index < kMaxSessionClips) {
		for (Slot& slot : slots_) {
			slot.clip_words[s][session_clip_index >> 5].fetch_or(1u << (session_clip_index & 31),
			                                                     std::memory_order_relaxed);
		}
	}
	raise(d); // release: publishes the clip words above
}

void DirtySet::take(Slot& from, DirtySet& out) {
	// Clip words first, coarse bits second -- the reverse of raise_clip()'s write order (word,
	// then coarse raise() with release). This is what makes the split impossible to observe:
	// whichever half of a racing raise_clip() this drain catches, it can only ever be "word
	// taken, flag not yet derived" (fixed up below) or "word taken AND flag observed" -- never
	// "flag observed, word still pending" (that would need the coarse bit read to happen
	// before the word exchange, which we no longer do).
	ClipBits taken[2];
	for (int s = 0; s < 2; s++) {
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			taken[s].words[w] = from.clip_words[s][w].exchange(0, std::memory_order_relaxed);
			out.snapshot_[s].words[w] |= taken[s].words[w];
		}
	}
	uint32_t bits = from.bits.exchange(0, std::memory_order_acq_rel);
	// Derive ClipContent/NoteRows from the words just taken, instead of trusting whatever the
	// accumulator's coarse bit happened to say: a word can now never appear in a snapshot
	// without its flag, even if raise_clip()'s trailing raise(d) for that exact word hasn't
	// landed in the slot's bits yet (it will just re-set an already-set bit here, or -- if it lands
	// after this exchange -- surface as a flag-with-empty-clips on the *next* drain, which callers
	// must treat as "unknown, assume all clips dirty" per the comment on has() in dirty_set.h).
	if (taken[0].any()) {
		bits |= 1u << std::to_underlying(Dirty::ClipContent);
	}
	if (taken[1].any()) {
		bits |= 1u << std::to_underlying(Dirty::NoteRows);
	}
	out.slots_[0].bits.fetch_or(bits, std::memory_order_relaxed);
}

DirtySet DirtySet::drain(DirtyConsumer who) {
	DirtySet out;
	take(slots_[std::to_underlying(who)], out);
	return out;
}

DirtySet DirtySet::drain() {
	DirtySet out;
	for (Slot& slot : slots_) {
		take(slot, out);
	}
	return out;
}

const ClipBits& DirtySet::clips(Dirty d) const {
	const int s = clip_slot(d);
	return s >= 0 ? snapshot_[s] : kNoClips;
}

} // namespace deluge::notify
