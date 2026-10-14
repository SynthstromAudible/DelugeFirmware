#pragma once
#include <cstdint>

namespace deluge::ui {

/// @brief What one widget or component has to say about the tick it was just called for.
///
/// @note The two flag groups are named for the surfaces that set them, but nothing stops a widget
///       from setting both -- `list` reports `entered`, `row` reports `pressed`, and a future
///       widget that does both would simply fill in both halves.
struct Response {
	bool changed = false, focused = false, entered = false; // OLED widgets
	bool pressed = false, released = false, held = false;   // grid components

	/// @brief The *pad* velocity, 0..127, and nothing else.
	///
	/// `GridFrame::pad`'s press velocity, passed straight through by everything that aggregates
	/// pads (`row`, `clip_row`, `status_pad`, `audition_pad`, `text_keyboard`). 0 when no press
	/// happened this tick.
	///
	/// @note It never carries an index, a character or any other encoding: those travel in
	///       `index`.
	uint8_t velocity = 0;

	/// @brief Widget-defined selection index: `row`/`clip_row` -> pressed column x;
	///        `text_keyboard` -> typed character code; -1 = none.
	///
	/// One field, one meaning per widget, and every widget that fills it says which in its own
	/// header.
	///
	/// @note Signed so "nothing" is -1 rather than a magic 0 that collides with column 0 or with a
	///       NUL character, and so it can coexist with a real velocity in the same call.
	int16_t index = -1;

	/// @brief The same selection, but for what is *held* this tick rather than what was pressed;
	///        -1 = nothing held.
	///
	/// `index` is an edge and `held_index` is the level under it, which is the pair a
	/// press-and-hold repeat needs: `index` says "act now", `held_index` says "still down, keep
	/// acting". A widget that fills `index` fills this by the same rule over `Response::held`
	/// instead of `Response::pressed`, and says so in its own header; a widget that fills neither
	/// leaves both at -1.
	///
	/// On the tick of a press this is *also* set, because `GridFrame::pad` answers `held` from the
	/// held-pad mask, which already includes the event being delivered
	/// (`ui/core/input.h`'s `PadMask`: "down right now, this event included"). So a caller never
	/// has to special-case the first tick of a hold.
	int16_t held_index = -1;
};

} // namespace deluge::ui
