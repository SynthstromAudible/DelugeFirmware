#include "scenes/menu/nodes/note.h"

#include "scenes/menu/note_bridge.h"

#include "model/iterance/iterance.h"
#include "notify/notice_queue.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace deluge::scenes::menu::nodes {

namespace {

/// Copies as much of @p text as fits in @p out and returns how much that was -- the shared tail of
/// every `ValueTextFn` body below.
size_t write_text(std::string_view text, std::span<char> out) {
	size_t n = std::min(text.size(), out.size());
	if (n > 0) {
		std::memcpy(out.data(), text.data(), n);
	}
	return n;
}

/// @brief `IteranceDivisor::writeCurrentValue`'s "OFF becomes 1of1" conversion
///        (`note/iterance_divisor.h:45-49,59-63`, `note_row/iterance_divisor.h:49-53,68-72`),
///        shared by both families' divisor and step hooks -- the note version and the row version
///        read the conversion the same way once each has its own raw `Iterance`.
Iterance resolved_iterance(int32_t raw) {
	Iterance iterance = Iterance::fromInt(raw);
	if (iterance == kDefaultIteranceValue) {
		iterance = kCustomIteranceValue;
	}
	return iterance;
}

} // namespace

// -------------------------------------------------------------------------------------------
// The two guards -- `note::SelectedNote::selectEncoderActionIsPermitted()`
// (`note/selected_note.h:29-42`) and `note_row::SelectedNoteRow`'s twin
// (`note_row/selected_note_row.h:34-43`).
// -------------------------------------------------------------------------------------------

bool permitted_note(const MenuContext&) {
	if (note_bridge()->note_selected()) {
		return true;
	}
	deluge::notify::notice_text("Select Note"); // `note/selected_note.h:37`
	return false;
}

bool permitted_row(const MenuContext&) {
	if (note_bridge()->row_auditioning()) {
		return true;
	}
	deluge::notify::notice_text("Select Row"); // `note_row/selected_note_row.h:36`
	return false;
}

// -------------------------------------------------------------------------------------------
// The three bespoke value texts, shared by both families (spec §5.3).
// -------------------------------------------------------------------------------------------

/// `Probability::getProbabilityValue` (`note/probability.h:93-104`), and its two callers:
/// `renderInHorizontalMenu` (`:70-79`, column: `"35%"`/`"35L"`) and `getNotificationValue`
/// (`:81-89`, notification: `"35%"`/`"35% ltch"`). `note_row::Probability` is byte-identical
/// (`note_row/probability.h:63-110`).
ValueText value_text_probability(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	bool latching = false;
	int32_t probability = value;
	if (probability > kNumProbabilityValues) {
		probability &= 127;
		latching = true;
	}
	const int32_t shown = probability * 5;

	char buffer[24];
	int written =
	    (use == ValueTextUse::Column)
	        ? std::snprintf(buffer, sizeof(buffer), "%d%s", static_cast<int>(shown), latching ? "L" : "%")
	        : std::snprintf(buffer, sizeof(buffer), "%d%%%s", static_cast<int>(shown), latching ? " ltch" : "");
	if (written <= 0) {
		return ValueText{};
	}
	return ValueText{write_text(std::string_view(buffer, static_cast<size_t>(written)), out), false};
}

/// `IterancePreset::getIteranceDisplayValue` (`note/iterance_preset.h:87-111`): `OFF`/`CUSTOM` are
/// the leaf's own literals for the two values `iterancePresets` has no row for; every other value
/// is the bridge's own label (`iterance_preset_label`, itself
/// `getIteranceDisplayValue`'s table-lookup half). `compact` chooses the column's `"%d:%d"`
/// (`renderInHorizontalMenu`, `:74-78`) over the notification's `"%d of %d"`
/// (`getNotificationValue`, `:80-82`) -- see `note_bridge.h`'s own doc on `iterance_preset_label`.
/// `note_row::IterancePreset` is byte-identical (`note_row/iterance_preset.h:82-125`).
ValueText value_text_iterance(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (value == kDefaultIterancePreset) {
		return ValueText{write_text("OFF", out), false};
	}
	if (value == kCustomIterancePreset) {
		return ValueText{write_text("CUSTOM", out), false};
	}
	const bool compact = use == ValueTextUse::Column;
	return ValueText{note_bridge()->iterance_preset_label(value, compact, out), false};
}

/// `Fill::renderInHorizontalMenu` (`note/fill.h:62-80`): the column draws only `"OFF"`/`"FILL"`,
/// struck through on `NOT_FILL`. `Fill::getNotificationValue` (`:82-84`) and
/// `InstrumentClipView::getFillString` (`gui/views/instrument_clip_view.cpp:3563-3574`) spell the
/// notification's three values. `note_row::Fill` is byte-identical (`note_row/fill.h:32-97`).
ValueText value_text_fill(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Column) {
		const std::string_view text = value == FillMode::OFF ? "OFF" : "FILL";
		return ValueText{write_text(text, out), value == FillMode::NOT_FILL};
	}
	std::string_view text = "OFF";
	if (value == FillMode::FILL) {
		text = "FILL";
	}
	else if (value == FillMode::NOT_FILL) {
		text = "NOT FILL";
	}
	return ValueText{write_text(text, out), false};
}

// -------------------------------------------------------------------------------------------
// The note editor.
// -------------------------------------------------------------------------------------------

int32_t get_note_velocity(const MenuContext&) {
	return note_bridge()->note_velocity(); // `note/velocity.h:44-52`
}
void adjust_note_velocity(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_velocity(offset); // `note/velocity.h:54-57`
}

int32_t get_note_probability(const MenuContext&) {
	return note_bridge()->note_probability(); // `note/probability.h:44-50`
}
void adjust_note_probability(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_probability(offset); // `note/probability.h:52-55`
}

/// `IterancePreset::readCurrentValue` converts the leftmost note's raw iterance to a preset index
/// (`note/iterance_preset.h:44-52`); the bridge hands back the raw int, so the conversion happens
/// here (`note_bridge.h`'s own doc: "Task 5's leaves convert with `Iterance::fromInt(...).
/// toPresetIndex()` themselves").
int32_t get_note_iterance(const MenuContext&) {
	return Iterance::fromInt(note_bridge()->note_iterance()).toPresetIndex();
}
void adjust_note_iterance(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_iterance(offset); // `note/iterance_preset.h:54-57`
}

/// `IterancePreset::selectButtonPress()`'s own test (`note/iterance_preset.h:59-67`), null-bridge-safe:
/// `validate_tree`'s whole-tree `select_chain` sweep (`tables_3_spec.cpp`) calls `chain_note_iterance`
/// with no bridge installed at all, unlike `get`/`set`/`adjust`/`permitted`/`value_text`, which
/// nothing calls outside a menu actually open on a note. "Not CUSTOM" is the safe answer -- the
/// same one `bindings::read` would give for `get_note_iterance` if it, too, tolerated a null bridge.
bool note_iterance_is_custom(const MenuContext& ctx) {
	if (note_bridge() == nullptr) {
		return false;
	}
	return get_note_iterance(ctx) == kCustomIterancePreset;
}

int32_t get_note_fill(const MenuContext&) {
	return note_bridge()->note_fill(); // `note/fill.h:44-50`
}
void adjust_note_fill(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_fill(offset); // `note/fill.h:52-55`
}

/// `IteranceDivisor::readCurrentValue`/`writeCurrentValue` (`note/iterance_divisor.h:40-69`).
int32_t get_note_iterance_divisor(const MenuContext&) {
	const Iterance iterance = resolved_iterance(note_bridge()->note_iterance());
	return std::clamp<int32_t>(iterance.divisor, 1, 8);
}
void set_note_iterance_divisor(const MenuContext&, int32_t value) {
	Iterance iterance = resolved_iterance(note_bridge()->note_iterance());
	const int32_t mask = (1 << value) - 1; // `:64`
	const int32_t new_steps = (static_cast<int32_t>(iterance.toInt()) & 0xFF) & mask;
	note_bridge()->set_note_iterance(Iterance{static_cast<uint8_t>(value), new_steps}.toInt());
}

/// One shared body per hook kind, indexed by @p index -- `IteranceStepToggle::readCurrentValue`
/// (`note/iterance_step_toggle.h:34-46`), `::writeCurrentValue` (`:47-67`) and `::isRelevant`
/// (`:71-80`). `isRelevant` alone reads the **unconverted** iterance: it tests
/// `kDefaultIteranceValue` directly, which `resolved_iterance` would already have converted away.
/// @{
int32_t note_iterance_step_get(int32_t index) {
	const Iterance iterance = resolved_iterance(note_bridge()->note_iterance());
	return iterance.iteranceStep[static_cast<size_t>(index)] ? 1 : 0;
}
void note_iterance_step_set(int32_t index, int32_t value) {
	Iterance iterance = resolved_iterance(note_bridge()->note_iterance());
	int32_t new_steps = static_cast<int32_t>(iterance.toInt()) & 0xFF;
	if (value != 0) {
		new_steps |= (1 << index);
	}
	else {
		new_steps &= ~(1 << index);
	}
	note_bridge()->set_note_iterance(Iterance{iterance.divisor, new_steps}.toInt());
}
bool note_iterance_step_relevant(int32_t index) {
	// `validate_tree`'s `EmptySubmenu`/`DuplicateName` sweeps call every reachable `relevant` under
	// whatever contexts a caller supplies, including host specs (`tables_2_spec.cpp`,
	// `tables_3_spec.cpp`) that validate the whole tree (`nodes::kAllRoots`) without ever installing
	// a note bridge -- unlike `get`/`set`/`adjust`/`permitted`/`value_text`, which nothing calls
	// outside a menu actually open on a note, `relevant` is reachable with no bridge at all. "Not
	// relevant" is the same safe default `note_bridge.h` itself uses for "nothing selected" (0).
	const NoteBridge* bridge = note_bridge();
	if (bridge == nullptr) {
		return false;
	}
	const Iterance iterance = Iterance::fromInt(bridge->note_iterance());
	return (iterance == kDefaultIteranceValue && index == 0) || iterance.divisor > index;
}
/// @}

#define DELUGE_NOTE_STEP_HOOKS(i, idx)                                                                                 \
	int32_t get_note_iterance_step##i(const MenuContext&) {                                                            \
		return note_iterance_step_get(idx);                                                                            \
	}                                                                                                                  \
	void set_note_iterance_step##i(const MenuContext&, int32_t value) {                                                \
		note_iterance_step_set(idx, value);                                                                            \
	}                                                                                                                  \
	bool relevant_note_iterance_step##i(const MenuContext&) {                                                          \
		return note_iterance_step_relevant(idx);                                                                       \
	}

DELUGE_NOTE_STEP_HOOKS(1, 0)
DELUGE_NOTE_STEP_HOOKS(2, 1)
DELUGE_NOTE_STEP_HOOKS(3, 2)
DELUGE_NOTE_STEP_HOOKS(4, 3)
DELUGE_NOTE_STEP_HOOKS(5, 4)
DELUGE_NOTE_STEP_HOOKS(6, 5)
DELUGE_NOTE_STEP_HOOKS(7, 6)
DELUGE_NOTE_STEP_HOOKS(8, 7)
#undef DELUGE_NOTE_STEP_HOOKS

// -------------------------------------------------------------------------------------------
// The note-row editor -- the same shape, through the row half of the bridge.
// -------------------------------------------------------------------------------------------

int32_t get_note_row_probability(const MenuContext&) {
	return note_bridge()->row_probability(); // `note_row/probability.h:43-53`
}
void adjust_note_row_probability(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_row_probability(offset); // `note_row/probability.h:55-61`
}

int32_t get_note_row_iterance(const MenuContext&) {
	return Iterance::fromInt(note_bridge()->row_iterance()).toPresetIndex(); // `note_row/iterance_preset.h:48-60`
}
void adjust_note_row_iterance(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_row_iterance(offset); // `note_row/iterance_preset.h:62-70`
}

/// See `note_iterance_is_custom`'s own note.
bool note_row_iterance_is_custom(const MenuContext& ctx) {
	if (note_bridge() == nullptr) {
		return false;
	}
	return get_note_row_iterance(ctx) == kCustomIterancePreset;
}

int32_t get_note_row_fill(const MenuContext&) {
	return note_bridge()->row_fill(); // `note_row/fill.h:44-54`
}
void adjust_note_row_fill(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_row_fill(offset); // `note_row/fill.h:56-65`
}

/// `note_row::IteranceDivisor::readCurrentValue`/`writeCurrentValue` (`note_row/iterance_divisor.h:41-78`).
int32_t get_note_row_iterance_divisor(const MenuContext&) {
	const Iterance iterance = resolved_iterance(note_bridge()->row_iterance());
	return std::clamp<int32_t>(iterance.divisor, 1, 8);
}
void set_note_row_iterance_divisor(const MenuContext&, int32_t value) {
	Iterance iterance = resolved_iterance(note_bridge()->row_iterance());
	const int32_t mask = (1 << value) - 1; // `:73`
	const int32_t new_steps = (static_cast<int32_t>(iterance.toInt()) & 0xFF) & mask;
	note_bridge()->set_row_iterance(Iterance{static_cast<uint8_t>(value), new_steps}.toInt());
}

/// `note_row::IteranceStepToggle::readCurrentValue`/`::writeCurrentValue`/`::isRelevant`
/// (`note_row/iterance_step_toggle.h:54-110`).
/// @{
int32_t note_row_iterance_step_get(int32_t index) {
	const Iterance iterance = resolved_iterance(note_bridge()->row_iterance());
	return iterance.iteranceStep[static_cast<size_t>(index)] ? 1 : 0;
}
void note_row_iterance_step_set(int32_t index, int32_t value) {
	Iterance iterance = resolved_iterance(note_bridge()->row_iterance());
	int32_t new_steps = static_cast<int32_t>(iterance.toInt()) & 0xFF;
	if (value != 0) {
		new_steps |= (1 << index);
	}
	else {
		new_steps &= ~(1 << index);
	}
	note_bridge()->set_row_iterance(Iterance{iterance.divisor, new_steps}.toInt());
}
bool note_row_iterance_step_relevant(int32_t index) {
	// See `note_iterance_step_relevant`'s own note: `validate_tree` reaches this with no bridge
	// installed from specs that validate the whole tree without ever touching the note family.
	const NoteBridge* bridge = note_bridge();
	if (bridge == nullptr) {
		return false;
	}
	const Iterance iterance = Iterance::fromInt(bridge->row_iterance());
	return (iterance == kDefaultIteranceValue && index == 0) || iterance.divisor > index;
}
/// @}

#define DELUGE_NOTE_ROW_STEP_HOOKS(i, idx)                                                                             \
	int32_t get_note_row_iterance_step##i(const MenuContext&) {                                                        \
		return note_row_iterance_step_get(idx);                                                                        \
	}                                                                                                                  \
	void set_note_row_iterance_step##i(const MenuContext&, int32_t value) {                                            \
		note_row_iterance_step_set(idx, value);                                                                        \
	}                                                                                                                  \
	bool relevant_note_row_iterance_step##i(const MenuContext&) {                                                      \
		return note_row_iterance_step_relevant(idx);                                                                   \
	}

DELUGE_NOTE_ROW_STEP_HOOKS(1, 0)
DELUGE_NOTE_ROW_STEP_HOOKS(2, 1)
DELUGE_NOTE_ROW_STEP_HOOKS(3, 2)
DELUGE_NOTE_ROW_STEP_HOOKS(4, 3)
DELUGE_NOTE_ROW_STEP_HOOKS(5, 4)
DELUGE_NOTE_ROW_STEP_HOOKS(6, 5)
DELUGE_NOTE_ROW_STEP_HOOKS(7, 6)
DELUGE_NOTE_ROW_STEP_HOOKS(8, 7)
#undef DELUGE_NOTE_ROW_STEP_HOOKS

} // namespace deluge::scenes::menu::nodes
