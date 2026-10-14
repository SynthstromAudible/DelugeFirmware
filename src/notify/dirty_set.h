#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace deluge::notify {

/// @brief Independently raisable UI/model "something changed" facts.
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

/// @brief Fixed-size bitset of session clip indices, one bit per clip up to kMaxSessionClips.
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

/// @brief Pointer-free, [any]-safe accumulator of dirty facts, doubling as the drained snapshot type.
///
/// The same type is both the live accumulator (atomic raises) and the value drain() returns (plain
/// reads thereafter). A drained result has its own, separate accumulator state that nothing will
/// ever drain again, so calling raise()/raise_clip() on it is meaningless -- only clips()/has()/
/// any() are meant to be called on one.
class DirtySet {
public:
	DirtySet() = default;
	DirtySet(const DirtySet&); // snapshot copy (relaxed loads)
	DirtySet& operator=(const DirtySet&);

	/// @brief Raise dirty fact @p d. [any]
	/// @param d The fact to raise.
	void raise(Dirty d);

	/// @brief Raise dirty fact @p d for one session clip. [any]
	/// @param d Dirty::ClipContent or Dirty::NoteRows; other values just raise the coarse fact.
	/// @param session_clip_index Clip index to mark; ignored if out of range.
	void raise_clip(Dirty d, uint16_t session_clip_index);

	/// @brief Atomically take every dirty fact raised since the last drain(). [task]
	/// @return A snapshot DirtySet holding what was pending; the accumulator is left cleared.
	DirtySet drain();

	/// @brief Test whether @p d is set in this accumulator or snapshot.
	///
	/// @note has(ClipContent)/has(NoteRows) can be true with an empty clips() set: drain() derives
	/// those two flags from the clip words it just took, but a flag can also come from a plain
	/// raise(d) with no clip index, or from a coarse raise() (inside raise_clip()) landing in the
	/// *next* drain after this one already took its word. Either way, treat a set flag with an
	/// empty clip set as "which clips is unknown -- treat as all dirty": drain() never lets a clip
	/// word appear in a snapshot without its flag, so the flag is always a superset of what the
	/// clip bits show, never a subset. Over-invalidation, never loss.
	/// @param d The fact to test.
	/// @return Whether @p d is set.
	bool has(Dirty d) const { return (bits_.load(std::memory_order_relaxed) >> std::to_underlying(d)) & 1u; }

	/// @brief Whether any fact at all is set.
	/// @return true if any Dirty fact is set.
	bool any() const { return bits_.load(std::memory_order_relaxed) != 0; }

	/// @brief The clip bits recorded for @p d.
	/// @param d Dirty::ClipContent or Dirty::NoteRows.
	/// @return The recorded clip bits, or an empty set for any other @p d.
	const ClipBits& clips(Dirty d) const;

private:
	static constexpr int clip_slot(Dirty d) { return d == Dirty::ClipContent ? 0 : d == Dirty::NoteRows ? 1 : -1; }
	std::atomic<uint32_t> bits_{0};
	std::atomic<uint32_t> clip_words_[2][kMaxSessionClips / 32] = {};
	ClipBits snapshot_[2]; // filled by drain() for the returned copy
};

/// @brief The process-global DirtySet accumulator.
/// @return The global DirtySet.
DirtySet& dirty();

/// @brief Raise dirty fact @p d on the global accumulator. [any]
/// @param d The fact to raise.
inline void raise(Dirty d) {
	dirty().raise(d);
}

/// @brief Raise dirty fact @p d for one session clip on the global accumulator. [any]
/// @param d Dirty::ClipContent or Dirty::NoteRows; other values just raise the coarse fact.
/// @param i Clip index to mark; ignored if out of range.
inline void raise_clip(Dirty d, uint16_t i) {
	dirty().raise_clip(d, i);
}

} // namespace deluge::notify
