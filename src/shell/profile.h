#pragma once
#include "ui/core/input.h"
#include "ui/core/style.h"
#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::ui::shell {

/// @brief Every physical button/encoder-push the shell can name, board-independent.
///
/// A `BoardProfile::button_by_matrix` maps a physical matrix coordinate onto one of these;
/// `ButtonSet` and `led_by_button` then work in terms of the id rather than the coordinate. The
/// six `*Enc` entries are an encoder's own push action (distinct from `EncoderRole`, which is the
/// encoder's *turn*), so `Count` bounds both a `ButtonSet` bit index and a `button_name()` lookup.
enum class ButtonId : uint8_t {
	None = 0,
	AffectEntire,
	Session,
	Clip,
	Synth,
	Kit,
	Midi,
	Cv,
	Keyboard,
	Scale,
	CrossScreen,
	Back,
	Load,
	Save,
	Learn,
	TapTempo,
	SyncScaling,
	Triplets,
	Play,
	Record,
	Shift,
	Mod0,
	Mod1,
	Mod2,
	Mod3,
	Mod4,
	Mod5,
	Mod6,
	Mod7,
	SelectEnc,
	TempoEnc,
	XEnc,
	YEnc,
	ModEnc0,
	ModEnc1,
	Count
};

/// @brief Which surface a UI event/render call targets.
///
/// `Main` is the primary OLED (or the sole surface on a board with only one); `Param0..3` are
/// per-parameter displays on boards that have them; `Aux` is a catch-all extra surface; `None`
/// marks "no surface" (e.g. a routed event with nothing to render).
enum class SurfaceRole : uint8_t { Main, Param0, Param1, Param2, Param3, Aux, None };

/// @brief How an encoder's turn behaves.
///
/// `Detented` reports one step per physical click; `Accelerating` reports a magnitude that grows
/// with turn speed. `BoardProfile::encoder_kind` maps each `EncoderRole` to one of these.
enum class EncoderKind : uint8_t { Detented, Accelerating };

/// @brief A bitset of `ButtonId`s: which buttons are currently held, or which changed this tick.
///
/// One `uint64_t`, so `ButtonId::Count` must stay at or under 64 (asserted below).
struct ButtonSet {
	uint64_t bits = 0;

	/// @brief Is \p b set?
	bool has(ButtonId b) const;
	/// @brief Set or clear \p b.
	void set(ButtonId b, bool on);
	/// @brief True if any bit is set.
	bool any() const;
};
static_assert(static_cast<int>(ButtonId::Count) <= 64);

/// @brief Pad-index -> parameter-id tables for a shortcut layer.
///
/// `layer0`/`layer1` are indexed `y * cols + x` (board-defined `cols`); the value is a param id,
/// or a sentinel the shortcut consumer defines for "no shortcut here". Spec E fills these in.
struct ShortcutTable {
	std::span<const uint16_t> layer0, layer1;
};

/// @brief The shell's l10n bridge: a string id (as stored on disk / baked into a board profile)
/// to display text. The shell has no l10n of its own; a board profile supplies this bridge, or
/// `nullptr` to fall back to rendering the id as digits.
/// @param string_id The id to resolve.
/// @return The resolved text.
using TextLookup = std::string_view (*)(uint16_t string_id);
/// @brief As `TextLookup`, for error codes.
/// @param error_code The code to resolve.
/// @return The resolved text.
using ErrorLookup = std::string_view (*)(int16_t error_code);

/// @brief Everything the shell needs to know about one physical board.
///
/// Every span is a view over board-owned storage (typically a `constexpr` array in a board's
/// `.cpp`), so a `BoardProfile` itself is cheap to copy and carries no ownership.
struct BoardProfile {
	std::span<const ButtonId> button_by_matrix;      ///< [y * button_cols + x]
	std::span<const int8_t> led_by_button;           ///< [ButtonId] -> board LED index, -1 = none
	std::span<const EncoderRole> encoder_by_index;   ///< libdeluge encoder index -> role
	std::span<const EncoderKind> encoder_kind;       ///< [EncoderRole]
	std::span<const SurfaceRole> oled_role_by_index; ///< libdeluge display index -> role
	/// libdeluge display index -> toolkit `Style` (task-9 reconciliation: was a single `style`;
	/// a board with more than one OLED, or a panel-height-64 second frontend, needs a `Style`
	/// per surface -- `OledSurface::style` (Task 2) is chosen from here at build time).
	std::span<const Style* const> style_by_index;
	ShortcutTable shortcuts;
	TextLookup text;
	ErrorLookup error_text;
	uint8_t button_cols, button_rows;
	/// How many of the board's pad columns are sidebar rather than main grid.
	///
	/// `DelugeBoard::pad_grid_width` counts *main* columns only and the descriptor has no sidebar
	/// field, so the sidebar width is a profile fact rather than a board-descriptor one. The
	/// Deluge's two sidebar columns are the default; a board with no sidebar sets 0.
	uint8_t sidebar_cols = 2;
};

/// @brief The button's name, for `.state` files and diagnostics.
/// @param b The id to name; anything at or past `ButtonId::Count` returns `"?"`.
/// @return A static, null-terminated string.
const char* button_name(ButtonId b);

} // namespace deluge::ui::shell
