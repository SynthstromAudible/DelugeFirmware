#pragma once

#include <cstdint>

namespace deluge::hid::encoders {

/// Determine signed movement from the A and B levels sampled by an A-edge interrupt.
/// Encoders whose physical wiring reverses the phase relationship use `invert` to retain
/// the same logical direction as the other encoders.
constexpr int8_t directionFromAEdge(bool a, bool b, bool invert) {
	const bool clockwise = invert ? (a != b) : (a == b);
	return clockwise ? 1 : -1;
}

/// Decode the two out-of-phase digital signals produced by a rotary encoder.
///
/// A complete positive cycle follows 00 -> 01 -> 11 -> 10 -> 00; reversing that
/// sequence produces negative movement. Each valid one-bit transition reports one
/// signed edge. Gold encoders consume every edge as a movement tick, while black
/// encoders accumulate four edges before reporting one physical detent.
///
/// The firmware polls both phases to catch slow B-only transitions, but only A is
/// connected to a hardware interrupt. `observeAEdge()` can therefore reconstruct an
/// adjacent B+A pair when fast movement changes both bits between observations.
class QuadratureDecoder {
public:
	/// Record the initial electrical state without reporting it as movement.
	void initialize(bool a, bool b) { state_ = encode(a, b); }

	/// Process a regularly polled A/B sample.
	/// Returns +1 or -1 for a valid one-bit transition, and zero for no movement or
	/// an ambiguous two-bit transition.
	int8_t observe(bool a, bool b, bool invert = false) {
		const uint8_t next = encode(a, b);
		int8_t movement = transition(state_, next);
		state_ = next;
		return invert ? -movement : movement;
	}

	/// Process a sample taken in response to an A-edge interrupt.
	/// A one-bit change reports one edge. If both bits changed since the previous
	/// observation, the known A interrupt allows the missed adjacent B+A pair to be
	/// reconstructed as two signed edges.
	int8_t observeAEdge(bool a, bool b, bool invert = false) {
		const uint8_t next = encode(a, b);
		int8_t movement = transition(state_, next);
		if ((state_ ^ next) == 0b11) {
			movement = directionFromAEdge(a, b, invert) * 2;
			state_ = next;
			return movement;
		}
		state_ = next;
		return invert ? -movement : movement;
	}

private:
	/// Pack the two Boolean pin levels into the lookup-table state order `AB`.
	static constexpr uint8_t encode(bool a, bool b) { return (static_cast<uint8_t>(a) << 1) | b; }

	/// Map every previous/current state pair to positive, negative, or no movement.
	/// Diagonal two-bit changes remain ambiguous here and are handled only by
	/// `observeAEdge()`, where the interrupt supplies additional context.
	static constexpr int8_t transition(uint8_t from, uint8_t to) {
		constexpr int8_t table[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
		return table[(from << 2) | to];
	}

	/// Most recently observed packed A/B state.
	uint8_t state_ = 0;
};

} // namespace deluge::hid::encoders
