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
/// @brief Who drains the facts. Each raise reaches every consumer, and each drains only its own
///        copy, so the legacy UI's drain cannot take a fact the shell has not seen.
enum class DirtyConsumer : uint8_t { Shell, Legacy, Count };

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

	/// @brief Atomically take every fact raised since @p who last drained. [task]
	/// @param who The consumer draining; the others keep their copies.
	/// @return A snapshot DirtySet holding what was pending for @p who.
	DirtySet drain(DirtyConsumer who);

	/// @brief Atomically take every consumer's pending facts at once, merged. [task]
	///
	/// For a caller that owns no consumer slot and wants the accumulator clear: a test between
	/// examples, mostly.
	/// @return A snapshot DirtySet holding everything any consumer had pending.
	DirtySet drain();

	/// @brief Has @p who anything to drain?
	/// @param who The consumer.
	/// @return True if a fact was raised since @p who last drained.
	bool pending(DirtyConsumer who) const {
		return slots_[std::to_underlying(who)].bits.load(std::memory_order_relaxed) != 0;
	}

	/// @brief Test whether @p d is set in this snapshot, or pending for any consumer of this accumulator.
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
	bool has(Dirty d) const { return (all_bits() >> std::to_underlying(d)) & 1u; }

	/// @brief Whether any fact at all is set, for any consumer.
	/// @return true if any Dirty fact is set.
	bool any() const { return all_bits() != 0; }

	/// @brief The clip bits recorded for @p d.
	/// @param d Dirty::ClipContent or Dirty::NoteRows.
	/// @return The recorded clip bits, or an empty set for any other @p d.
	const ClipBits& clips(Dirty d) const;

private:
	static constexpr int clip_slot(Dirty d) { return d == Dirty::ClipContent ? 0 : d == Dirty::NoteRows ? 1 : -1; }
	/// One consumer's pending facts. A drained snapshot holds its facts in slot 0.
	struct Slot {
		std::atomic<uint32_t> bits{0};
		std::atomic<uint32_t> clip_words[2][kMaxSessionClips / 32] = {};
	};
	static constexpr size_t kSlots = std::to_underlying(DirtyConsumer::Count);
	uint32_t all_bits() const {
		uint32_t b = 0;
		for (const Slot& slot : slots_) {
			b |= slot.bits.load(std::memory_order_relaxed);
		}
		return b;
	}
	/// @brief Take one slot's facts into @p out, merged with what it already holds.
	static void take(Slot& from, DirtySet& out);
	Slot slots_[kSlots];
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
