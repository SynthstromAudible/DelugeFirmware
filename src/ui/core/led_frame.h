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

	void set(LedId id, LedState s);
	void ring(RingId r, KnobRing k) {
		rings_[static_cast<int>(r)] = k;
		has_ring_[static_cast<int>(r)] = true;
	}
	std::span<const std::pair<LedId, LedState>> entries() const { return {entries_, count_}; }
	const KnobRing* ring_or_null(RingId r) const {
		return has_ring_[static_cast<int>(r)] ? &rings_[static_cast<int>(r)] : nullptr;
	}
	void clear() {
		count_ = 0;
		has_ring_[0] = has_ring_[1] = false;
	}

private:
	std::pair<LedId, LedState> entries_[kMaxEntries];
	size_t count_ = 0;
	KnobRing rings_[2]{};
	bool has_ring_[2]{};
};

} // namespace deluge::ui
