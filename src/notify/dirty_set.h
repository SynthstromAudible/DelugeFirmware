#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

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
static_assert(static_cast<int>(Dirty::Count) <= 32);

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
/// and the drained snapshot (plain reads after drain()).
class DirtySet {
public:
	DirtySet() = default;
	DirtySet(const DirtySet&); // snapshot copy (relaxed loads)
	DirtySet& operator=(const DirtySet&);

	void raise(Dirty d);                                   // [any]
	void raise_clip(Dirty d, uint16_t session_clip_index); // [any]
	DirtySet drain();                                      // [task]

	bool has(Dirty d) const { return (bits_.load(std::memory_order_relaxed) >> static_cast<unsigned>(d)) & 1u; }
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
