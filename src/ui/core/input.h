#pragma once
#include <cstdint>
#include <span>

namespace deluge::ui {

/// @brief The hardware's encoder-role enumeration.
///
/// @note Named by nothing on this branch: `InputSnapshot` below lowers each encoder into its own
///       already-named field (`turn`, `edit[]`, `horizontal`, `vertical`), so no widget has to
///       index by role. Kept as the name the shell will use when it maps hardware encoders onto
///       those fields; delete it if it stays unused.
enum class EncoderRole : uint8_t { Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo, Count };

/// @brief One pad press/release event. `velocity == 0` means release.
struct PadEvent {
	uint8_t x, y;
	uint8_t velocity;
	bool pressed;
}; // velocity 0 = release

/// @brief Which pads are down *right now*, as a bitmask -- the level to `PadEvent`'s edge.
///
/// A press and its release are separated by many ticks, so `Response::held` cannot be answered
/// from this tick's `PadEvent`s alone; the shell already tracks the down set for its own
/// bookkeeping and lowers it here. One `uint32_t` per column, bit `y` per row: 32 columns x 32
/// rows, which covers `gfx::grid::kMaxGridCols` (32) and `kMaxGridRows` (16), and so the Deluge's
/// 18x8 grid with room to spare. POD -- `InputSnapshot{}` zeroes it, meaning "nothing held".
///
/// @note Deliberately a plain array rather than a `std::bitset` or a span: the snapshot is copied
///       by value in places (`ui/oled/values.cpp` masks the encoder fields that way), so it must
///       stay trivially copyable and own its storage.
struct PadMask {
	static constexpr uint8_t kCols = 32;
	static constexpr uint8_t kRows = 32; ///< bits per word

	uint32_t cols[kCols];

	/// @brief Is the pad at absolute grid coordinates (\p x, \p y) down?
	/// @param x Column, 0-based.
	/// @param y Row, 0-based.
	/// @return True if held; out-of-range coordinates read as "not held" rather than off the end
	///         of the array.
	constexpr bool test(uint8_t x, uint8_t y) const { return x < kCols && y < kRows && (cols[x] & (1u << y)) != 0; }
	/// @brief Set or clear the held state of the pad at (\p x, \p y). Out-of-range coordinates are
	///        ignored.
	/// @param x    Column, 0-based.
	/// @param y    Row, 0-based.
	/// @param down New held state.
	constexpr void set(uint8_t x, uint8_t y, bool down) {
		if (x >= kCols || y >= kRows) {
			return;
		}
		const uint32_t bit = 1u << y;
		cols[x] = down ? (cols[x] | bit) : (cols[x] & ~bit);
	}
};

/// @brief Input as the toolkit sees it for one tick.
///
/// Lowered by the shell; the only input a widget can read.
struct InputSnapshot {
	int8_t turn;    // Select encoder detents: navigate / move focus
	int8_t edit[2]; // GoldUpper, GoldLower detents: edit the focused value
	int8_t horizontal, vertical;

	/// @name Edges vs. levels
	///
	/// `select_pressed` and `back_pressed` are true only on the tick in which the button went
	/// down, exactly like the detents above and `pads` below -- never for as long as the button is
	/// held. `shift` is the odd one out and *is* a level, like `held_pads`: it is a modifier, read
	/// alongside another gesture rather than acted on itself.
	///
	/// @note The shell lowers the edges; the widgets deliberately do not latch. `list`, `columns`
	///       and `toggle` set `Response::entered` straight from `select_pressed`, so a shell that
	///       lowered a level here would enter a submenu once per frame for as long as the encoder
	///       was held -- a repeat rate of one entry per tick. Widgets cannot defend against that:
	///       an immediate-mode widget has no per-caller identity to hang "I already reported this
	///       press" on, and the POD state a scene passes in belongs to the widget's own job, not
	///       to the button. Supplying edges is therefore the caller's contract, and
	///       tests/spec/ui/list_spec.cpp pins it by asserting the un-defended behaviour: two
	///       consecutive ticks with `select_pressed` set report `entered` twice.
	/// @{
	bool select_pressed, back_pressed, shift;
	/// @}
	std::span<const PadEvent> pads; // this tick's pad events; grid frames filter by region
	PadMask held_pads{};            // pads currently down, absolute coordinates; the level to `pads`' edges
	uint32_t now_ms;
};

} // namespace deluge::ui
