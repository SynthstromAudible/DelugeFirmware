#pragma once
#include <cstdint>
#include <span>

namespace deluge::ui {

enum class EncoderRole : uint8_t { Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo, Count };

struct PadEvent {
	uint8_t x, y;
	uint8_t velocity;
	bool pressed;
}; // velocity 0 = release

/// Which pads are down *right now*, as a bitmask -- the level to `PadEvent`'s edge (spec B §3
/// amendment, recorded with the `GridFrame` task).
///
/// A press and its release are separated by many ticks, so `Response::held` cannot be answered
/// from this tick's `PadEvent`s alone; the shell already tracks the down set for its own
/// bookkeeping and lowers it here. One `uint32_t` per column, bit `y` per row: 32 columns x 32
/// rows, which covers `gfx::grid::kMaxGridCols` (32) and `kMaxGridRows` (16), and so the Deluge's
/// 18x8 grid with room to spare. POD -- `InputSnapshot{}` zeroes it, meaning "nothing held".
///
/// Deliberately a plain array rather than a `std::bitset` or a span: the snapshot is copied by
/// value in places (`ui/oled/values.cpp` masks the encoder fields that way), so it must stay
/// trivially copyable and own its storage.
struct PadMask {
	static constexpr uint8_t kCols = 32;
	static constexpr uint8_t kRows = 32; ///< bits per word

	uint32_t cols[kCols];

	/// Is the pad at absolute grid coordinates (\p x, \p y) down? Out-of-range coordinates read
	/// as "not held" rather than off the end of the array.
	constexpr bool test(uint8_t x, uint8_t y) const { return x < kCols && y < kRows && (cols[x] & (1u << y)) != 0; }
	constexpr void set(uint8_t x, uint8_t y, bool down) {
		if (x >= kCols || y >= kRows) {
			return;
		}
		const uint32_t bit = 1u << y;
		cols[x] = down ? (cols[x] | bit) : (cols[x] & ~bit);
	}
};

/// Input as the toolkit sees it for one tick. Lowered by the shell; the only input a widget can read.
struct InputSnapshot {
	int8_t turn;    // Select encoder detents: navigate / move focus
	int8_t edit[2]; // GoldUpper, GoldLower detents: edit the focused value
	int8_t horizontal, vertical;
	bool select_pressed, back_pressed, shift;
	std::span<const PadEvent> pads; // this tick's pad events; grid frames filter by region
	PadMask held_pads{};            // pads currently down, absolute coordinates; the level to `pads`' edges
	uint32_t now_ms;
};

} // namespace deluge::ui
