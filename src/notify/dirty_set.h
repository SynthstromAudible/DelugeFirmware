#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace deluge::notify {

enum class Dirty : uint8_t {
	GridMain,
	GridSidebar,
	Oled,
	ClipContent,
	NoteRows,
	SampleReady,
	PlaybackState,
	SongSwapped,
	OutputChanged,
	ParamAutomation,
	ModTarget,
	SelectedDrum,
	MidiLearn,
	Count
};
static_assert(std::to_underlying(Dirty::Count) <= 32);

constexpr size_t kMaxSessionClips = 256;

struct ClipBits {
	uint32_t words[kMaxSessionClips / 32] = {};
	bool test(uint16_t i) const { return i < kMaxSessionClips && (words[i >> 5] >> (i & 31)) & 1u; }
	bool any() const {
		for (auto w : words)
			if (w)
				return true;
		return false;
	}
};

/// Pointer-free, [any]-safe dirty facts. The same type is the live accumulator (atomics)
/// and the drained snapshot (plain reads after drain()) — drain()'s return value is a
/// snapshot: calling raise*() on it is meaningless (it has its own, separate accumulator
/// state that nothing will ever drain again). Only clips()/has()/any() are meant to be
/// called on a drained result.
class DirtySet {
public:
	DirtySet() = default;
	DirtySet(const DirtySet&); // snapshot copy (relaxed loads)
	DirtySet& operator=(const DirtySet&);

	void raise(Dirty d);                                   // [any]
	void raise_clip(Dirty d, uint16_t session_clip_index); // [any]
	DirtySet drain();                                      // [task]

	// has(ClipContent)/has(NoteRows) can be set with an empty clips() set: drain() derives
	// these two flags from the clip words it just took, but a flag can also be produced by a
	// plain raise(d) with no clip index, or by a coarse raise() from raise_clip() landing in
	// the *next* drain after this one already took its word. Either way, treat a set flag with
	// an empty clip set as "which clips is unknown -- treat as all dirty": drain() never lets
	// a clip word appear in a snapshot without its flag, so the flag is only ever a superset of
	// what the clip bits show, never a subset. Over-invalidation, never loss.
	bool has(Dirty d) const { return (bits_.load(std::memory_order_relaxed) >> std::to_underlying(d)) & 1u; }
	bool any() const { return bits_.load(std::memory_order_relaxed) != 0; }
	const ClipBits& clips(Dirty d) const; // ClipContent or NoteRows; empty set otherwise

private:
	static constexpr int clip_slot(Dirty d) { return d == Dirty::ClipContent ? 0 : d == Dirty::NoteRows ? 1 : -1; }
	std::atomic<uint32_t> bits_{0};
	std::atomic<uint32_t> clip_words_[2][kMaxSessionClips / 32] = {};
	ClipBits snapshot_[2]; // filled by drain() for the returned copy
};

DirtySet& dirty();
inline void raise(Dirty d) {
	dirty().raise(d);
}
inline void raise_clip(Dirty d, uint16_t i) {
	dirty().raise_clip(d, i);
}

} // namespace deluge::notify
