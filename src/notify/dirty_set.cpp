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
	bits_.store(o.bits_.load(std::memory_order_relaxed), std::memory_order_relaxed);
	for (int s = 0; s < 2; s++) {
		snapshot_[s] = o.snapshot_[s];
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			clip_words_[s][w].store(o.clip_words_[s][w].load(std::memory_order_relaxed), std::memory_order_relaxed);
		}
	}
	return *this;
}

void DirtySet::raise(Dirty d) {
	bits_.fetch_or(1u << static_cast<unsigned>(d), std::memory_order_release);
}

void DirtySet::raise_clip(Dirty d, uint16_t i) {
	const int s = clip_slot(d);
	if (s >= 0 && i < kMaxSessionClips) {
		clip_words_[s][i >> 5].fetch_or(1u << (i & 31), std::memory_order_relaxed);
	}
	raise(d); // release: publishes the clip word above
}

DirtySet DirtySet::drain() {
	DirtySet out;
	// Clip words first, coarse bits second -- the reverse of raise_clip()'s write order (word,
	// then coarse raise() with release). This is what makes the split impossible to observe:
	// whichever half of a racing raise_clip() this drain catches, it can only ever be "word
	// taken, flag not yet derived" (fixed up below) or "word taken AND flag observed" -- never
	// "flag observed, word still pending" (that would need the coarse bit read to happen
	// before the word exchange, which we no longer do).
	for (int s = 0; s < 2; s++) {
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			out.snapshot_[s].words[w] = clip_words_[s][w].exchange(0, std::memory_order_relaxed);
		}
	}
	uint32_t bits = bits_.exchange(0, std::memory_order_acq_rel);
	// Derive ClipContent/NoteRows from the words just taken, instead of trusting whatever the
	// accumulator's coarse bit happened to say: a word can now never appear in a snapshot
	// without its flag, even if raise_clip()'s trailing raise(d) for that exact word hasn't
	// landed in bits_ yet (it will just re-set an already-set bit here, or -- if it lands after
	// this exchange -- surface as a flag-with-empty-clips on the *next* drain, which callers
	// must treat as "unknown, assume all clips dirty" per the comment on has() in dirty_set.h).
	if (out.snapshot_[0].any()) {
		bits |= 1u << static_cast<unsigned>(Dirty::ClipContent);
	}
	if (out.snapshot_[1].any()) {
		bits |= 1u << static_cast<unsigned>(Dirty::NoteRows);
	}
	out.bits_.store(bits, std::memory_order_relaxed);
	return out;
}

const ClipBits& DirtySet::clips(Dirty d) const {
	const int s = clip_slot(d);
	return s >= 0 ? snapshot_[s] : kNoClips;
}

} // namespace deluge::notify
