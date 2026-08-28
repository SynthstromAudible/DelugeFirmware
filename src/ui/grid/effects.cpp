#include "ui/grid/effects.h"

namespace deluge::ui::grid {

void greyout(GridFrame& f, GreyoutMask mask, int32_t amount) {
	if (mask.cols == 0 && mask.rows == 0) {
		return; // pad_leds.cpp:287, the `(greyoutRows || greyoutCols)` guard
	}

	const int32_t w = f.bounds().w;
	const int32_t h = f.bounds().h;

	for (int32_t y = 0; y < h; y++) {
		const bool row_hit = y < 32 && (mask.rows & (1u << y)) != 0; // :288, `greyoutRows & (1 << y)`

		for (int32_t x = 0; x < w; x++) {
			// :288, `greyoutCols & (1 << (kDisplayWidth + kSideBarWidth - 1 - x))`: the column bits
			// run right to left, so bit 0 is the last column of the frame.
			const int32_t col_bit = w - 1 - x;
			const bool col_hit = col_bit >= 0 && col_bit < 32 && (mask.cols & (1u << col_bit)) != 0;

			if (!row_hit && !col_hit) { // the two masks are ORed, not ANDed
				continue;
			}
			// Read back what has already been painted and re-tint it, as prepareColour does on the
			// way to the hardware (:289). greyOut is non-const, hence the copy.
			RGB current = f.grid.at(static_cast<uint8_t>(f.clip.x + x), static_cast<uint8_t>(f.clip.y + y));
			f.pad(x, y, current.greyOut(amount));
		}
	}
}

GestureEvent gesture(GridFrame& f, Hold& hold, HoldPolicy policy, int32_t& second_x, int32_t& second_y) {
	const uint32_t now = f.input.now_ms;

	for (const PadEvent& e : f.local_events()) {
		if (!hold.held) {
			if (!e.pressed) {
				continue; // a release with nothing tracked: not ours
			}
			// session_view.cpp:817-824 -- record the pad and the moment.
			hold.held = true;
			hold.hold_reported = false;
			hold.down_ms = now;
			hold.x = e.x;
			hold.y = e.y;
			// qwerty_ui.cpp:220's one-shot timer, as a repaint deadline (spec B section 5).
			f.gate.request_repaint_after(policy.short_press_ms, now);
			continue;
		}

		const bool same_pad = e.x == hold.x && e.y == hold.y;

		if (e.pressed) {
			if (same_pad) {
				continue; // a re-press of the pad already held: legacy has no transition for it
			}
			// session_view.cpp:884-902 -- "If Clip previously already pressed, clone it to
			// newly-pressed row", which then ends the press (`goto justEndClipPress`, :901).
			second_x = e.x;
			second_y = e.y;
			hold = Hold{};
			return GestureEvent::SecondPad;
		}

		if (!same_pad) {
			continue; // a release of some other pad: legacy matches on the pressed coordinates
		}

		// session_view.cpp:944-945 (short -> act) against arranger_view.cpp:1307 (>= -> exit with
		// no action). Unsigned subtraction, so a wrapped `now_ms` still measures the elapsed time.
		const uint32_t elapsed = now - hold.down_ms;
		hold = Hold{};
		return elapsed < policy.short_press_ms ? GestureEvent::Tap : GestureEvent::Release;
	}

	// The deadline armed at press has passed and the pad is still down. Fires once per press --
	// the `hold_reported` latch stands in for the one-shot timer legacy arms at qwerty_ui.cpp:220.
	if (hold.held && !hold.hold_reported && (now - hold.down_ms) >= policy.short_press_ms) {
		hold.hold_reported = true;
		return GestureEvent::Hold;
	}
	return GestureEvent::None;
}

} // namespace deluge::ui::grid
