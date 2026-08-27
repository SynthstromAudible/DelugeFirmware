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
	out.bits_.store(bits_.exchange(0, std::memory_order_acq_rel), std::memory_order_relaxed);
	for (int s = 0; s < 2; s++) {
		for (size_t w = 0; w < kMaxSessionClips / 32; w++) {
			out.snapshot_[s].words[w] = clip_words_[s][w].exchange(0, std::memory_order_relaxed);
		}
	}
	return out;
}

const ClipBits& DirtySet::clips(Dirty d) const {
	const int s = clip_slot(d);
	return s >= 0 ? snapshot_[s] : kNoClips;
}

} // namespace deluge::notify
