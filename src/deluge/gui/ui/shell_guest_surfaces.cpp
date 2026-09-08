/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "gui/ui/shell_guest_surfaces.h"

#include "definitions_cxx.hpp"
#include "gfx/oled/geometry.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "libdeluge/control_surface.h"

#include <algorithm>
#include <iterator>

namespace deluge::gui::ui {

using deluge::hid::display::OLED;
using deluge::ui::KnobIndicator;
using deluge::ui::shell::ColumnPair;
using deluge::ui::shell::kColumnPairLeds;
using deluge::ui::shell::SurfaceRole;

// The two framebuffer formats really are byte-identical, which is the half of spec F §3 that
// survives: the shell's Deluge geometry and legacy's OLED macros describe the same 1-bpp
// page-major 128x48 frame, and legacy's pad image is exactly the nine column pairs the shell
// sends. These are what make the copies below whole-buffer copies rather than conversions.
static_assert(deluge::gfx::oled::kDelugeOled.width == OLED_MAIN_WIDTH_PIXELS);
static_assert(deluge::gfx::oled::kDelugeOled.height == OLED_MAIN_HEIGHT_PIXELS);
static_assert(deluge::gfx::oled::kDelugeOled.visible_top == OLED_MAIN_TOPMOST_PIXEL);
static_assert(deluge::gfx::oled::kDelugeOled.bytes() == GuestSurfaceSet::kOledCopyBytesPerBlit);
static_assert(sizeof(PadLEDs::image) == GuestSurfaceSet::kGridCopyBytesPerFullFlush);
static_assert(GuestSurfaceSet::kGridCopyBytesPerColumnPair == 48);
static_assert(GuestSurfaceSet::kOledCopyBytesPerBlit == 768);
static_assert(GuestSurfaceSet::kGridCopyBytesPerFullFlush == 432);

namespace {
/// @brief Can a panel of this shape be handed to legacy's single `OLED::main`?
///
/// Shape, not byte count. Both are 768 bytes on the Deluge, but they are not the same question and
/// the difference is a blank screen: a 64x96 panel is *also* 768 bytes, would satisfy a size check,
/// and would then be dropped frame by frame by `blit`'s geometry guard with nothing saying why
/// (F1 task 4 review, Important 4). `visible_top` is deliberately not compared -- it is a faceplate
/// fact about what the user can see, not about what fits in the buffer, and `blit` does not carry
/// it either.
///
/// The constructor and `blit` both go through this one predicate so the accessor's promise and the
/// guard that enforces it cannot drift apart.
bool fits_legacy_panel(uint16_t w, uint16_t h) {
	return w == OLED_MAIN_WIDTH_PIXELS && h == OLED_MAIN_HEIGHT_PIXELS;
}
} // namespace

GuestSurfaceSet::GuestSurfaceSet(const DelugeBoard& board, const deluge::ui::shell::BoardProfile& profile) {
	build(board, profile);
	// Resolved once, here, rather than per blit: `oled(SurfaceRole::Main)` is a linear scan, and
	// the answer cannot change -- `build()` is the only thing that assigns roles.
	const deluge::ui::shell::OledSurface* main = oled(SurfaceRole::Main);
	if (main != nullptr && fits_legacy_panel(main->geometry.width, main->geometry.height)) {
		mainIndex_ = static_cast<int8_t>(main->index);
	}
}

bool GuestSurfaceSet::oled_busy(uint8_t index) {
	// `index` is deliberately ignored. Legacy's back-pressure on the display is not per-panel and
	// is not a transfer-in-flight flag at all: it is one gate on the shared pad-output bus, checked
	// once for the grid and the OLED together in `doAnyPendingUIRendering` (`gui/ui/ui.cpp:375-396`).
	// Read the braces there rather than the shape: the OLED push (`doAnyPendingOLEDRendering()`,
	// `:392`) sits inside *two* enclosing guards -- the `pendingUIRenderingLock` re-entrancy guard
	// opened at `:376-380`, and the pad-output-space `if` at `:389`, whose body is `:390-393` and
	// covers the grid render as well. This forwards to the second of those, negated, and to nothing
	// else.
	//
	// Not `deluge_display_busy_n()`, which is what `BoardSurfaceSet` uses (`shell/board_surfaces.cpp:27-29`):
	// no file under `src/deluge/` calls `deluge_display_busy` or `deluge_display_busy_n` at all
	// (grepped repo-wide; the only callers are the BSPs that define them and `board_surfaces.cpp`).
	// A guest that consulted it would be inventing a policy legacy does not have. `Shell::flush`'s
	// own comment used to describe "the `deluge_display_busy` skip around `OLED::sendMainImage`";
	// there is no such skip in `OLED::sendMainImage` (`hid/display/oled.cpp:335-385`), which gates
	// only on its own `needsSending` flag. Corrected at `shell/shell.cpp:905` and recorded as shell
	// spec §15.10.
	(void)index;
	return deluge_control_pad_output_space()
	       <= static_cast<uint32_t>((kNumBytesInMainPadRedraw + kNumBytesInSidebarRedraw) * 2);
}

void GuestSurfaceSet::blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) {
	// Stands in for `OLED::sendMainImage` (`hid/display/oled.cpp:335-385`), legacy's push of a
	// rendered panel -- except that the guest does not push. It hands the frame to `OLED::main` and
	// raises legacy's own dirty flag through `OLED::markChanged` (`hid/display/oled.h:76-83`);
	// legacy's render loop then does the pushing, with its screensaver, console and popup
	// compositing intact. Blitting past legacy to `deluge_display_blit_oled` would skip all three.
	if (mainIndex_ < 0 || index != static_cast<uint8_t>(mainIndex_)) {
		return; // Legacy holds exactly one framebuffer; nothing else has anywhere to go.
	}
	const std::span<uint8_t> dest = OLED::main.bytes();
	if (!fits_legacy_panel(w, h) || bytes.size() != dest.size()) {
		// The constructor already rejected a Main surface of the wrong shape (`canReachMainPanel()`)
		// through this same predicate, so this is unreachable today. It is still checked, because a
		// partial copy would show a torn frame rather than fail: half a frame is worse than none.
		return;
	}
	// kOledCopyBytesPerBlit (768 B). See the class doc for why this is a copy and not an alias.
	std::copy(bytes.begin(), bytes.end(), dest.begin());
	OLED::markChanged();
}

uint32_t GuestSurfaceSet::pad_output_space() {
	// The same number legacy consults before pushing a pad column: `sendOutMainPadColours` gates on
	// `deluge_control_pad_output_space() <= kNumBytesInMainPadRedraw`
	// (`hid/led/pad_leds.cpp:1019-1037`, the test at `:1021`) -- an all-or-nothing gate on the whole
	// eight-pair main grid. Legacy also asks the same question a column at a time, `<=
	// kNumBytesInColUpdateMessage`, inside `clearTickSquares` (`hid/led/pad_leds.cpp:134-163`, the
	// test at `:153`) and `setTickSquares` (`:165-230`, at `:221`); in both it sits three braces
	// deep -- inside `if (colsToSend)`, inside the `for (x)`, inside `if (colsToSend & (1 << x))` --
	// and `break`s out of the column loop, sending what fits and dropping the rest of that pass.
	// `Shell::flush` divides this by `kBytesPerColumnPair` (= `kNumBytesInColUpdateMessage` = 49,
	// `definitions_cxx.hpp:95`) for a per-pair budget, which is that second shape, except that it
	// re-arms the pairs it could not send instead of dropping them.
	return deluge_control_pad_output_space();
}

void GuestSurfaceSet::send_columns(uint8_t pair_index, const ColumnPair& pair) {
	// Stands in for `sortLedsForCol` (`hid/led/pad_leds.cpp:260-275`), legacy's pad-column send,
	// and is driven the way `sendOutMainPadColours` (`:1019-1037`) drives it -- one call per
	// column pair, with a single `deluge_control_flush()` afterwards, which is `flush()` below.
	//
	// The colours go into `PadLEDs::image` first and `sortLedsForCol` reads them back out, rather
	// than going straight to `deluge_control_set_pad_columns`. That is deliberate and is what keeps
	// the guest doing what legacy does: `sortLedsForCol` passes every pad through `prepareColour`
	// (`hid/led/pad_leds.cpp:283-298`), which applies the slow-flash cursor and the greyout, and it
	// leaves `image` holding what is on the pads for everything else that reads it (greyout
	// re-assessment, the zoom and scroll animations, a later full `sendOutMainPadColours`).
	//
	// Costs kGridCopyBytesPerColumnPair (48 B) per changed pair; see the class doc.
	//
	// @warning On a board with FEWER rows than legacy's `kDisplayHeight`, this writes rows
	//          0..rows-1 and `sortLedsForCol` still reads all eight (`hid/led/pad_leds.cpp:267-272`),
	//          so the rows above the board's go out as whatever `image` last held. Deliberately not
	//          zeroed: those rows are not the shell's to own, and blanking them would be the guest
	//          inventing a policy about legacy's buffer. Hypothetical-board only -- every board in
	//          the tree is 8 rows -- and stated at the spec site as uncovered (F1 task 4 review).
	const uint8_t x0 = static_cast<uint8_t>(pair_index * 2);
	const uint8_t x1 = static_cast<uint8_t>(x0 + 1);
	if (x1 >= kDisplayWidth + kSideBarWidth) {
		return; // A pair past legacy's 18 columns has no cells in `image` to write.
	}
	// Unpacked exactly the way `read_column_pair` packed it (`shell/shell.cpp:61-74`): `rows`
	// colours from column x0, then `rows` from x1, where `rows` is the grid's row count clamped to
	// half a pair. Mirroring that expression rather than hardcoding 8 is what stops a board with
	// fewer rows silently reading the second column from the wrong offset.
	const uint8_t rows =
	    std::min<uint8_t>(std::min<uint8_t>(grid().grid().dims().rows, kColumnPairLeds / 2), kDisplayHeight);
	for (uint8_t y = 0; y < rows; y++) {
		PadLEDs::image[y][x0] = pair.colours[y];
		PadLEDs::image[y][x1] = pair.colours[rows + y];
	}
	PadLEDs::sortLedsForCol(x0);
}

void GuestSurfaceSet::set_led(uint8_t board_led, bool on) {
	// Replaces `setLedState` (`hid/led/indicator_leds.cpp:42-52`) -- and calls it, rather than the
	// `deluge_control_set_led` at its `:51`, for two reasons. It keeps legacy's `ledStates[]`
	// shadow true, so the LEDs are coherent when the guest closes; and its default
	// `allowContinuedBlinking = false` stops any legacy blinker on that LED
	// (`stopLedBlinking`, `:141-149`), which is right, because while the guest is open the shell's
	// own `LedFrame` owns the blink phase.
	//
	// `board_led` passes straight through: the board profile's `led_by_button` values ARE
	// `indicator_leds::LED`'s underlying values, `x + y * NUM_LED_COLS`
	// (`shell/profiles/deluge.cpp:24-60`, each entry citing its own line of `indicator_leds.h`).
	indicator_leds::setLedState(static_cast<indicator_leds::LED>(board_led), on);
}

void GuestSurfaceSet::set_indicator(uint8_t which, const KnobIndicator& indicator) {
	// Replaces `actuallySetKnobIndicatorLevel` (`hid/led/indicator_leds.cpp:182-245`), whose whole
	// body is the conversion from one scalar level to four per-segment brightnesses followed by the
	// boundary call at `:241`. The shell has already done that conversion -- `KnobIndicator` IS the
	// four segment levels (`ui/core/led_frame.h:30-32`) -- so there is no scalar to hand the legacy
	// function, and forwarding to it would mean inventing a level to convert back from.
	//
	// `which` passes straight through, as it does in `BoardSurfaceSet::set_indicator`
	// (`shell/board_surfaces.cpp:47-52`): `IndicatorId` is numbered `{Lower = 0, Upper = 1}` to
	// match legacy's `whichKnob` mod-encoder number.
	deluge_control_set_indicator(which, indicator.levels, static_cast<uint8_t>(std::size(indicator.levels)));
}

void GuestSurfaceSet::flush() {
	// The `deluge_control_flush()` that legacy calls once after a run of `sortLedsForCol`s
	// (`sendOutMainPadColours`, `hid/led/pad_leds.cpp:1032`; `sendOutSidebarColours`, `:1053`) --
	// which is also what `sortLedsForCol`'s own comment at `:259` asks its callers to do, under
	// that call's older name.
	// `Shell::flush` calls this at most once per tick, and only when something was actually sent
	// (`shell/shell.cpp:1028-1030`).
	deluge_control_flush();
}

} // namespace deluge::gui::ui
