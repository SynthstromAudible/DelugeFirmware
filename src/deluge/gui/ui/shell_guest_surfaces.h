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

#pragma once
#include "board_config.h"
#include "board_layout.hpp"
#include "gfx/grid/rgb.h"
#include "shell/surfaces.h"

#include <cstdint>
#include <span>

namespace deluge::gui::ui {

/// @brief The `SurfaceSet` the shell renders through while it runs as a *guest* inside the legacy
///        app: every sink forwards to legacy's own display and pad drivers instead of to libdeluge.
///
/// `deluge::ui::shell::BoardSurfaceSet` is the same seam bound straight to libdeluge, for when the
/// shell is the host (spec F §2.2). This class is the phase-4 twin: legacy still owns the panel and
/// the pad bus, so the guest hands its rendered frames to `OLED::main` / `PadLEDs::image` and lets
/// legacy's existing send paths push them.
///
/// **Copy, not alias -- and why (F1 task 4, 2026-09-07).** Spec F §3 said the framebuffer formats
/// were byte-identical "so aliasing is free and copy-less": the guest's `Main` surface would *be*
/// `OLED::main`'s storage and the grid surface's rows would alias `PadLEDs::image` through
/// `PadGrid::row(y)`. Neither half of that is true of the code:
///
///  - The **grid cannot alias at all.** `PadGrid` (`gfx/grid/pad_grid.h:42-141`) stores
///    `cells_[kMaxGridRows][kMaxGridCols]` inline -- a 32-column row stride -- while
///    `PadLEDs::image` (`hid/led/pad_leds.h:33`) is `[kDisplayHeight][kDisplayWidth +
///    kSideBarWidth]`, an 18-column stride. The strides differ, the storage is a member rather than
///    a span, and `pad_grid.h:40-41` says so in as many words: `row(y)` is the seam for *copying* a
///    single row to or from that legacy buffer, never for reinterpreting the whole array.
///  - The **OLED could alias but must not.** `OLED::main` is an `OwnedCanvas`
///    (`gfx/oled/canvas.h:378-386`) over its own 768-byte store, and `OLED::clearMainImage`
///    (`hid/display/oled.cpp:68-78`) zeroes it -- from seventeen legacy call sites (grep
///    `clearMainImage()` across `src/deluge`, less its declaration and its definition), including
///    `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp:351-371`), which clears it immediately before
///    asking each open UI to re-render. The shell is retained-mode: `Shell::flush`
///    (`shell/shell.cpp:902-1031`) blits only a surface whose `blit_pending` a repaint raised. An
///    aliased buffer that legacy zeroed between two shell renders would go blank with nothing to
///    tell the shell to paint it again.
///
/// So the guest copies, and the copy is measured rather than assumed -- see the three constants
/// below and `blit()` / `send_columns()`.
class GuestSurfaceSet final : public deluge::ui::shell::SurfaceSet {
public:
	/// @brief Bytes copied into `OLED::main` by one `blit()` of the main panel.
	///
	/// The whole 1-bpp page-major frame: 128 x 48 / 8.
	///
	/// **This is a per-tick cost, not a per-second one, and deliberately stated that way.** The OLED
	/// path has no diff -- `Shell::flush` blits a surface unconditionally once it has rendered, which
	/// `shell/shell.cpp:453-460` records as the reason the OLED half may not over-arm the way the
	/// grid half may. So the cost is 768 B on every tick in which the panel re-rendered, and the
	/// only ceiling is the host's tick rate: several `next_timer()` overrides return 0 ("as soon as
	/// possible"), so there is no millisecond floor to quote. The fastest *fixed* cadence in the
	/// tree that arms an OLED gate is `RecorderScene`'s `kRecorderPollMs = 10`
	/// (`src/scenes/recorder/recorder_scene.h:180`), which takes `Scene::timer_repaints()`'s
	/// `{.oled = true, .grid = true}` default (`shell/scene.h:629`) because it does not override it
	/// -- 768 B x 100/s = 76.8 kB/s on one panel, and a floor rather than a ceiling.
	///
	/// For scale, legacy already memcpys up to this whole image per frame in
	/// `copyBackgroundAroundForeground` (`hid/display/oled.cpp:331`) whenever a console or popup is
	/// up.
	static constexpr uint32_t kOledCopyBytesPerBlit = OLED_MAIN_WIDTH_PIXELS * OLED_MAIN_HEIGHT_PIXELS / 8;

	/// @brief Bytes written into `PadLEDs::image` by one `send_columns()`.
	///
	/// Sixteen pads -- two columns of eight -- as two strided eight-cell column writes.
	static constexpr uint32_t kGridCopyBytesPerColumnPair = deluge::ui::shell::kColumnPairLeds * sizeof(RGB);

	/// @brief Bytes written into `PadLEDs::image` by a flush that re-sends every column pair.
	///
	/// Nine pairs on the Deluge (16 main + 2 sidebar columns), so exactly `sizeof(PadLEDs::image)`
	/// -- asserted against it in the .cpp.
	///
	/// **The bound here is the diff, not a cadence** (corrected in the F1 task 4 review; the first
	/// version of this note quoted a 20 ms ceiling that two shipped scenes already exceed).
	/// `AudioClipScene::next_timer()` returns `0u` for as long as the playhead is live
	/// (`src/scenes/audio_clip/audio_clip_scene.cpp:1199`) with `{.oled = false, .grid = true}`
	/// (`audio_clip_scene.h:477`), so the grid gate arms on **every tick** and there is no
	/// millisecond ceiling to name. What makes that free is that `Shell::flush` diffs each pair
	/// against `last_sent` before calling `send_columns` at all (`shell/shell.cpp:959-975`): a tick
	/// in which no pad changed colour costs zero bytes however fast the ticks come, and the 432 B
	/// above is the cost of a *full repaint*, not of a tick. The real figure is 48 B x (pairs whose
	/// colours actually changed).
	///
	/// Legacy's own `sortLedsForCol` (`hid/led/pad_leds.cpp:260-275`) already copies these same 16
	/// colours into a `doubleColumn` for every pair it sends, so the guest adds one copy of that
	/// same size per pair, not a new order of cost.
	static constexpr uint32_t kGridCopyBytesPerFullFlush =
	    kGridCopyBytesPerColumnPair * ((kDisplayWidth + kSideBarWidth) / 2);

	/// @brief Build the guest's surfaces for \p board as described by \p profile.
	/// @param board Board capability descriptor, normally `deluge::ui::shell::kDelugeBoard`.
	/// @param profile The matching board profile.
	GuestSurfaceSet(const DelugeBoard& board, const deluge::ui::shell::BoardProfile& profile);

	bool oled_busy(uint8_t index) override;
	void blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) override;
	uint32_t pad_output_space() override;
	void send_columns(uint8_t pair_index, const deluge::ui::shell::ColumnPair& pair) override;
	void set_led(uint8_t board_led, bool on) override;
	void set_indicator(uint8_t which, const deluge::ui::KnobIndicator& indicator) override;
	void flush() override;

	/// @brief Does this board's `SurfaceRole::Main` surface have somewhere to go?
	///
	/// Legacy owns exactly one framebuffer, `OLED::main`, at exactly one geometry. A profile whose
	/// Main surface is a different shape (or which declares no Main surface at all) has nowhere to
	/// put its bytes, and `blit()` drops them rather than showing half a frame. That is a
	/// configuration error, not a runtime condition, so it is reported here for the caller
	/// (`ShellHostUI`) to refuse to open on rather than discovered as a blank screen.
	///
	/// The question asked is the panel's **shape**, and it is asked through the same predicate
	/// `blit()` enforces (`fits_legacy_panel`, in the .cpp), so this cannot promise more than the
	/// guard delivers. It was a byte-count comparison until the F1 task 4 review: a 64x96 panel is
	/// also 768 bytes, so a size check said yes and every frame was then dropped silently -- exactly
	/// the blank screen this accessor exists to let the caller refuse.
	/// @return True if `blit()` will actually reach the panel.
	bool canReachMainPanel() const { return mainIndex_ >= 0; }

private:
	/// libdeluge display index of the `SurfaceRole::Main` surface, or -1 if this board has none
	/// that legacy's single `OLED::main` can hold. Resolved once in the constructor.
	int8_t mainIndex_ = -1;
};

} // namespace deluge::gui::ui
