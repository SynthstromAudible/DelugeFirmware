#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace deluge::ui {

// LED declarations (surface-agnostic data; composed by the shell).
enum class LedId : uint8_t; // defined by the board profile (spec D)
enum class RingId : uint8_t { Upper, Lower };
struct LedState {
	enum class Kind : uint8_t { Off, On, Blink } kind;
	uint16_t period_ms;
	uint32_t until_ms;
};
struct KnobRing {
	uint8_t levels[4];
};

class LedFrame {
public:
	static constexpr size_t kMaxEntries = 64;

	/// Declare `id`'s state for this frame, replacing any previous declaration of the same id.
	/// Returns false when the frame is full (kMaxEntries distinct ids) and the declaration was
	/// dropped -- the same reasoning as NoticeQueue::push: a dropped LED is indistinguishable
	/// from an LED nobody declared, so the fact is returned rather than swallowed.
	[[nodiscard]] bool set(LedId id, LedState s);

	/// Returns false when `r` is out of range and the ring was not stored.
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
	std::span<const std::pair<LedId, LedState>> entries() const { return {entries_, count_}; }
	const KnobRing* ring_or_null(RingId r) const {
		return ring_in_range(r) && has_ring_[std::to_underlying(r)] ? &rings_[std::to_underlying(r)] : nullptr;
	}
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
