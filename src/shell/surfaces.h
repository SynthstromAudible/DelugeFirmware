#pragma once
#include "gfx/grid/pad_grid.h"
#include "gfx/grid/rgb.h"
#include "gfx/oled/canvas.h"
#include "gfx/oled/geometry.h"
#include "libdeluge/board.h"
#include "shell/profile.h"
#include "ui/core/led_frame.h"
#include "ui/core/repaint_gate.h"
#include "ui/core/style.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::shell {

/// @brief How many OLED panels the surface pool holds. A board that declares more is clamped.
inline constexpr uint8_t kMaxOled = 4;
/// @brief Framebuffer bytes reserved per OLED surface: the largest panel the shell supports
///        (128x64 at 1bpp). Every surface gets this much whatever its declared geometry, so the
///        pool is one fixed-size block with no allocation.
inline constexpr size_t kOledStorageBytes = 128 * 64 / 8;
/// @brief LEDs in one main-grid column pair: two columns of eight pads.
inline constexpr uint8_t kColumnPairLeds = 16;
/// @brief Column-pair capacity, sized to `gfx::grid::kMaxGridCols / 2`.
inline constexpr uint8_t kMaxColumnPairs = gfx::grid::kMaxGridCols / 2;

/// @brief Bytes in one main-grid column-pair send, for `Shell::flush`'s pad-output-space budget.
///
/// Legacy: `kNumBytesInColUpdateMessage = 49` and `kNumBytesInMainPadRedraw =
/// kNumBytesInColUpdateMessage * 8` for the whole 8-pair main grid (`src/definitions_cxx.hpp:94,
/// 100`), so one pair's message is `kNumBytesInMainPadRedraw / 8` bytes -- 49, the same number as
/// `kNumBytesInColUpdateMessage` itself, since the main grid is exactly 8 pairs of one message
/// each. `flush_spec.cpp` copies that expression and `static_assert`s it against this literal, so
/// this file never includes `definitions_cxx.hpp` (no libdeluge includes outside
/// `board_surfaces.cpp`).
inline constexpr uint32_t kBytesPerColumnPair = 49;

/// @brief One OLED panel the shell can render into and flush.
///
/// The canvas draws over the pool storage `SurfaceSet::build()` handed it; `geometry` is that
/// same geometry kept alongside so callers need not go back through `canvas.geometry()`.
struct OledSurface {
	/// @brief Construct an unbuilt surface: an empty canvas with zero geometry.
	///
	/// The pool is a plain array, so every slot is default-constructed before
	/// `SurfaceSet::build()` fills in the ones the board actually has. `Canvas` has no default
	/// constructor; an empty span with a zero geometry satisfies its `storage.size() >= bytes()`
	/// precondition and draws nothing.
	OledSurface() : canvas(std::span<uint8_t>{}, gfx::oled::Geometry{0, 0, 0}) {}

	gfx::oled::Canvas canvas;
	gfx::oled::Geometry geometry{0, 0, 0};
	RepaintGate gate;
	SurfaceRole role = SurfaceRole::None;
	uint8_t index = 0;
	bool blit_pending = false;
	/// The board profile's style for this surface, copied in at build time so a render pass never
	/// re-indexes the profile.
	/// @warning The shell requires a style for every surface it renders. This is null for a
	///          surface past the end of `BoardProfile::style_by_index`; such a surface must not be
	///          rendered, and a board profile that leaves one null is a profile bug.
	const Style* style = nullptr;
};

/// @brief The 16 pad colours of one main-grid column pair.
///
/// Legacy order (`pad_leds.cpp` `sortLedsForCol`): column `x` rows 0..7, then column `x + 1`
/// rows 0..7.
struct ColumnPair {
	RGB colours[kColumnPairLeds];
	bool operator==(const ColumnPair&) const = default;
};

/// @brief A grid animation the surface is playing, driven by the shell's tick rather than by the
///        control surface's own smear.
struct ActiveTween {
	enum class Kind : uint8_t { Fade, ScrollH, ScrollV, GreyOut } kind;
	uint32_t start_ms;
	uint32_t duration_ms;
	int32_t param;
};

/// @brief The pad grid plus the send-side shadow of what was last pushed to the hardware.
///
/// `last_sent`/`sent_once` are what the legacy `sendOutMainPadColours` lacks: without them every
/// flush re-sends every column pair.
struct GridSurface {
	/// The pad framebuffer, valid once `SurfaceSet::build()` has run.
	///
	/// `gfx::grid::PadGrid` has no default constructor (its dimensions are a constructor
	/// argument) and the surface pool must be constructible before the board is known, so the
	/// grid is held two-phase in an `optional` and emplaced by `build()`. Reach it through
	/// `grid()`, never through the optional.
	std::optional<gfx::grid::PadGrid> grid_storage;
	RepaintGate gate;
	ColumnPair last_sent[kMaxColumnPairs]{};
	bool sent_once[kMaxColumnPairs]{};
	/// How many column pairs this board's grid has; indexes `last_sent`/`sent_once`.
	uint8_t pairs = 0;
	std::optional<ActiveTween> tween;
	/// Has the grid been re-rendered since the last flush? The grid-side twin of
	/// `OledSurface::blit_pending`: `Shell::render_grid` raises it and `Shell::flush` (Task 8)
	/// lowers it once the changed column pairs have gone out. It is deliberately *not* "some pad
	/// changed colour" -- that question is answered by diffing `last_sent`, which is the only
	/// thing that can tell a re-render that painted the same pixels from one that did not.
	bool dirty = false;

	/// @brief The pad framebuffer.
	/// @pre `SurfaceSet::build()` has run.
	/// @return Reference to the grid.
	gfx::grid::PadGrid& grid() { return *grid_storage; }
	/// @copydoc grid()
	const gfx::grid::PadGrid& grid() const { return *grid_storage; }
};

/// @brief The board's indicator LEDs and knob indicators, plus their send-side shadow.
///
/// The three LED arrays are all indexed by *board LED index* -- `LedId`'s own underlying value,
/// which is the number `SurfaceSet::set_led` takes -- not by a position in `frame.entries()`:
/// `LedFrame::set` appends a newly-declared id and overwrites an already-declared one, so an
/// entry's position moves as scenes come and go and would make a useless shadow key.
struct LedSurface {
	/// This tick's composed declarations, filled by `Shell::derive_leds` through
	/// `LedCompose::compose` (`shell/leds.h`).
	LedFrame frame;
	/// Whether board LED `i` should be lit right now: `led_is_on()` evaluated against this tick's
	/// clock, so the blink phase is resolved before the diff rather than inside it.
	std::array<bool, LedFrame::kMaxEntries> desired_on{};
	/// Whether anything declared board LED `i` at all this tick. An undeclared LED is left alone
	/// rather than driven off -- a board LED nothing in the stack mentions is not the shell's to
	/// turn off.
	std::array<bool, LedFrame::kMaxEntries> desired_declared{};
	std::array<bool, LedFrame::kMaxEntries> last_on{};
	std::array<bool, LedFrame::kMaxEntries> sent_once{};
	KnobIndicator last_indicators[2]{};
	bool indicators_sent[2]{};
};

/// @brief The shell's only hardware seam: the surfaces it renders into and the sinks it flushes
///        them through.
///
/// Everything above this class works in terms of `OledSurface`/`GridSurface`/`LedSurface` and the
/// pure-virtual sinks below; `BoardSurfaceSet` binds those sinks to libdeluge and
/// `uitest::FakeSurfaceSet` binds them to recorders. No other part of the shell talks to the
/// hardware.
class SurfaceSet {
public:
	virtual ~SurfaceSet() = default;

	/// @brief The board's OLED surfaces, in libdeluge display-index order.
	/// @return A view over the built surfaces (empty on a board with no OLED).
	std::span<OledSurface> oled() { return {oled_, oled_count_}; }
	/// @brief The surface a board profile assigned to \p r.
	/// @param r Role to look up. `SurfaceRole::None` means "no surface" and never matches, even
	///          though unassigned surfaces carry it.
	/// @return The surface, or nullptr if the board has none in that role.
	OledSurface* oled(SurfaceRole r);
	/// @brief The pad grid surface.
	GridSurface& grid() { return grid_; }
	/// @brief The LED/indicator surface.
	LedSurface& leds() { return leds_; }

	/// @name Sinks
	/// Implemented by the backend: libdeluge on hardware, recorders in tests.
	/// @{

	/// @brief Is panel \p index still transferring a previous blit?
	/// @param index libdeluge display index.
	/// @return True while a transfer is in flight.
	virtual bool oled_busy(uint8_t index) = 0;
	/// @brief Push a 1-bpp page-major framebuffer to panel \p index.
	/// @param index libdeluge display index.
	/// @param bytes The framebuffer, `w * h / 8` bytes.
	/// @param w Panel width in pixels.
	/// @param h Panel height in pixels.
	virtual void blit(uint8_t index, std::span<const uint8_t> bytes, uint16_t w, uint16_t h) = 0;
	/// @brief Free space, in bytes, in the control surface's bulk pad-output buffer.
	/// @return The free byte count; the flush uses it as back-pressure.
	virtual uint32_t pad_output_space() = 0;
	/// @brief Send one main-grid column pair.
	/// @param pair_index Column-pair index, `< GridSurface::pairs`.
	/// @param pair The 16 colours to send.
	virtual void send_columns(uint8_t pair_index, const ColumnPair& pair) = 0;
	/// @brief Set one indicator LED.
	/// @param board_led Board-defined LED index.
	/// @param on Whether it should be lit.
	virtual void set_led(uint8_t board_led, bool on) = 0;
	/// @brief Set one knob indicator's per-segment levels.
	/// @param which 0 for the upper knob, 1 for the lower (matching `IndicatorId`).
	/// @param indicator The levels to send.
	virtual void set_indicator(uint8_t which, const KnobIndicator& indicator) = 0;
	/// @brief Push everything queued by the calls above to the hardware.
	virtual void flush() = 0;
	/// @}

protected:
	/// @brief Size the surfaces from a board descriptor and a board profile. Called by a derived
	///        class's constructor.
	///
	/// OLED surfaces come from `display_count` (clamped to `kMaxOled`) and `displays[]`, falling
	/// back to `display_width`/`display_height` with a zero `visible_top` when `displays` is null.
	/// Each surface's `role` comes from `BoardProfile::oled_role_by_index` and its `style` from
	/// `BoardProfile::style_by_index`; a surface past the end of either span gets
	/// `SurfaceRole::None` and a null style.
	///
	/// @note Pad-grid width convention: `DelugeBoard::pad_grid_width` counts **main** columns only
	///       (16 on the Deluge -- see `deluge_board()` in `src/bsp/host/host_bsp.c`) and the
	///       descriptor carries no sidebar field, so the sidebar width comes from
	///       `BoardProfile::sidebar_cols`. The grid is built as
	///       `{pad_grid_width, sidebar_cols, pad_grid_height}` and `pairs` counts pairs across the
	///       *total* width (9 on the Deluge). The profile is the authority for the sidebar.
	///
	/// @param board Board capability descriptor.
	/// @param profile Board profile; supplies the surface roles, styles and sidebar width.
	/// @pre The board's total column count fits `gfx::grid::kMaxGridCols` and its row count fits
	///      `gfx::grid::kMaxGridRows` (enforced by `PadGrid`'s own contract).
	void build(const DelugeBoard& board, const BoardProfile& profile);

	uint8_t storage_[kMaxOled][kOledStorageBytes]{};
	OledSurface oled_[kMaxOled];
	uint8_t oled_count_ = 0;
	GridSurface grid_;
	LedSurface leds_;
};

} // namespace deluge::ui::shell
