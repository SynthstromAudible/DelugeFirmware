#pragma once

#include "modulation/params/param_value.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::tempo {
// Tempo automation uses hundredths of a BPM. Relative curves may cross zero;
// limit only the clock-facing result, never the stored scalar or curve.
constexpr int32_t clockValue(int32_t value) {
	return std::max(value, int32_t{1});
}
// A tempo undo transaction restores the independent scalar and timer period
// together. Coalesced encoder detents replace only the after-state.
class Change {
public:
	Change(uint64_t beforeTicks, uint64_t afterTicks, int64_t beforeScalar, int64_t afterScalar)
	    : ticks_{beforeTicks, afterTicks}, scalars_{beforeScalar, afterScalar} {}
	void updateAfter(uint64_t ticks, int64_t scalar) {
		ticks_.back() = ticks;
		scalars_.back() = scalar;
	}
	uint64_t restore(bool after, ClipParamValue& scalar) const {
		scalar.setUnclampedValue(scalars_.at(after ? 1 : 0));
		return ticks_.at(after ? 1 : 0);
	}

private:
	std::array<uint64_t, 2> ticks_;
	std::array<int64_t, 2> scalars_;
};
} // namespace deluge::tempo
