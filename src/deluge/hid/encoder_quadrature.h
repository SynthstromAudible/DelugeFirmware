#pragma once

#include <cstdint>

namespace deluge::hid::encoders {

/// Decode the two out-of-phase digital signals produced by a rotary encoder.
///
/// A complete positive cycle follows 00 -> 01 -> 11 -> 10 -> 00; reversing that
/// sequence produces negative movement. Each valid one-bit transition is one signed
/// edge, giving four edges per cycle. Gold encoders consume every edge as a movement
/// tick, while black encoders accumulate four edges per physical detent.
///
/// Glitch rejection (as in the 1.x `Encoder::read()`): a pin change is only treated as
/// movement once the *other* pin has also changed. A single pin bouncing or flickering
/// around its threshold therefore never reports movement, at the cost of each edge being
/// confirmed one edge late.
///
/// The firmware polls both phases to catch slow B-only transitions, but only A is
/// connected to a hardware interrupt. `observeAEdge()` can therefore reconstruct an
/// adjacent B+A pair when fast movement changes both bits between observations.
class QuadratureDecoder {
public:
	/// Record the initial electrical state without reporting it as movement.
	void initialize(bool a, bool b) { committed_ = sampled_ = encode(a, b); }

	/// Process a regularly polled A/B sample.
	/// Returns the signed number of edges confirmed by this sample. If both bits changed
	/// since the previous sample the order is ambiguous, so the sample is ignored: A has
	/// changed, so its interrupt is pending and `observeAEdge()` will resolve the order.
	int8_t observe(bool a, bool b, bool invert = false) {
		const uint8_t next = encode(a, b);
		if ((sampled_ ^ next) == 0b11) {
			return 0;
		}
		const int8_t movement = step(next);
		return invert ? -movement : movement;
	}

	/// Process a sample taken in response to an A-edge interrupt.
	/// If both bits changed since the previous sample, the known A interrupt implies the
	/// missed B transition came first, so both transitions are replayed in that order.
	int8_t observeAEdge(bool a, bool b, bool invert = false) {
		const uint8_t next = encode(a, b);
		int8_t movement = 0;
		if ((sampled_ ^ next) == 0b11) {
			movement += step(sampled_ ^ 0b01);
		}
		movement += step(next);
		return invert ? -movement : movement;
	}

private:
	/// Advance by one sample that is at most one bit away from `sampled_`.
	/// Returns +1 or -1 when the sample confirms the previous edge, otherwise zero.
	int8_t step(uint8_t next) {
		if ((committed_ ^ next) != 0b11) {
			// Either back where we were last confirmed (a bounce), or only one pin away from it
			// (unconfirmed). Neither reports movement.
			sampled_ = next;
			return 0;
		}
		// Both pins now differ from the confirmed state, so the edge to `sampled_` was real.
		const int8_t movement = transition(committed_, sampled_);
		committed_ = sampled_;
		sampled_ = next;
		return movement;
	}

	/// Pack the two Boolean pin levels into the lookup-table state order `AB`.
	static constexpr uint8_t encode(bool a, bool b) { return (static_cast<uint8_t>(a) << 1) | b; }

	/// Map every previous/current state pair to positive, negative, or no movement.
	static constexpr int8_t transition(uint8_t from, uint8_t to) {
		constexpr int8_t table[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
		return table[(from << 2) | to];
	}

	/// Last state whose edge has been confirmed and reported.
	uint8_t committed_ = 0;

	/// Most recently observed packed A/B state.
	uint8_t sampled_ = 0;
};

} // namespace deluge::hid::encoders
