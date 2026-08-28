#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace deluge::ui {

// LED declarations (surface-agnostic data; composed by the shell).
/// Defined by the board profile.
enum class LedId : uint8_t;
/// One of the two physical knob LED rings.
enum class RingId : uint8_t { Upper, Lower };
/// Declared state of one LED: off, on, or blinking with a period and a deadline.
struct LedState {
	enum class Kind : uint8_t { Off, On, Blink } kind;
	uint16_t period_ms;
	uint32_t until_ms;
};
/// Per-segment LED levels of one knob's indicator ring.
struct KnobRing {
	uint8_t levels[4];
};

/// @brief Accumulates one frame's worth of LED and knob-ring declarations, to be read back by the
///        shell and rendered to the hardware.
class LedFrame {
public:
	static constexpr size_t kMaxEntries = 64;

	/// @brief Declare @p id's state for this frame, replacing any previous declaration of the same
	///        id.
	///
	/// Returns false when the frame is full (kMaxEntries distinct ids) and the declaration was
	/// dropped -- the same reasoning as NoticeQueue::push: a dropped LED is indistinguishable
	/// from an LED nobody declared, so the fact is returned rather than swallowed.
	/// @param id LED to declare state for.
	/// @param s  State to declare.
	/// @return false if the frame was already full and the declaration was dropped.
	[[nodiscard]] bool set(LedId id, LedState s);

	/// @brief Declare ring @p r's LED levels for this frame.
	/// @param r Ring to set.
	/// @param k Levels to store.
	/// @return false when @p r is out of range and the ring was not stored.
	[[nodiscard]] bool ring(RingId r, KnobRing k) {
		// RingId has a fixed underlying type, so an out-of-range value (RingId{2}) is
		// constructible; guard rather than index rings_/has_ring_ unchecked (same house style as
		// gfx::grid::PadGrid::set's in_range guard for caller-supplied coordinates).
		if (!ring_in_range(r)) {
			return false;
		}
		rings_[std::to_underlying(r)] = k;
		has_ring_[std::to_underlying(r)] = true;
		return true;
	}
	/// @brief All LED declarations made so far this frame.
	/// @return A view over the (id, state) pairs declared via set().
	std::span<const std::pair<LedId, LedState>> entries() const { return {entries_, count_}; }
	/// @brief Ring @p r's declared levels.
	/// @param r Ring to look up.
	/// @return Pointer to the stored levels, or nullptr if @p r is out of range or was not
	///         declared this frame.
	const KnobRing* ring_or_null(RingId r) const {
		return ring_in_range(r) && has_ring_[std::to_underlying(r)] ? &rings_[std::to_underlying(r)] : nullptr;
	}
	/// @brief Reset the frame to empty (no LED or ring declarations).
	void clear() {
		count_ = 0;
		has_ring_[0] = has_ring_[1] = false;
	}

private:
	static constexpr bool ring_in_range(RingId r) { return std::to_underlying(r) < 2; }

	std::pair<LedId, LedState> entries_[kMaxEntries];
	size_t count_ = 0;
	KnobRing rings_[2]{};
	bool has_ring_[2]{};
};

} // namespace deluge::ui
