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

/// @brief `IteranceDivisor::readCurrentValue`/`writeCurrentValue`'s "OFF becomes 1of1" conversion
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

// -------------------------------------------------------------------------------------------
// One shared body per hook kind (fix round 1, important 2): the note and note-row families
// differ only in which `NoteBridge` member they read and write, so a small pointer-to-member
// accessor picks that out and every hook below is a one-line call through it.
// -------------------------------------------------------------------------------------------

/// @brief Which `NoteBridge` entries answer "the iterance", "write the iterance" and "is there
///        something to edit" for one family. `note_iterance`/`set_note_iterance`/`note_selected`
///        for the note family; `row_iterance`/`set_row_iterance`/`row_auditioning` for the row
///        family (`note_bridge.h`'s own member list) -- every `NoteBridge` field named here has the
///        same declared type in both families (`int32_t(*)()`, `void(*)(int32_t)`, `bool(*)()`), so
///        one pointer-to-DATA-member type each covers both (`decltype` rather than hand-spelling
///        the pointer-to-member-of-a-function-pointer syntax, which is easy to get wrong silently).
using RawGet = decltype(&NoteBridge::note_iterance);
using RawSet = decltype(&NoteBridge::set_note_iterance);
using HasTarget = decltype(&NoteBridge::note_selected);

struct IteranceFamily {
	RawGet raw_get;
	RawSet raw_set;
	HasTarget has_target;
};

constexpr IteranceFamily kNoteFamily{&NoteBridge::note_iterance, &NoteBridge::set_note_iterance,
                                     &NoteBridge::note_selected};
constexpr IteranceFamily kRowFamily{&NoteBridge::row_iterance, &NoteBridge::set_row_iterance,
                                    &NoteBridge::row_auditioning};

int32_t iterance_raw(const IteranceFamily& family) {
	return (note_bridge()->*family.raw_get)();
}
void iterance_raw_set(const IteranceFamily& family, int32_t value) {
	(note_bridge()->*family.raw_set)(value);
}
/// @brief Is there a note (row family: a note row) to edit at all? The note family's
///        `note_selected()` and the row family's `row_auditioning()` are each already the bridge's
///        own stand-in for that question -- `note_selected()` is `SelectedNote::
///        shouldEnterSubmenu()`'s own test (`note_bridge.h`'s doc on that member), the same one
///        `permitted_note` gates every note leaf's edit on; `row_auditioning()` is `SelectedNoteRow
///        ::shouldEnterSubmenu()`'s (likewise `permitted_row`'s gate). Legacy's own iterance-step
///        `isRelevant` overrides ask a narrower question -- a non-null `Note*`/`NoteRow*` -- that
///        the bridge does not expose directly; these are the closest answer it has, and the one the
///        rest of this file already treats as "a note (row) is selected to edit".
bool iterance_has_target(const IteranceFamily& family) {
	return (note_bridge()->*family.has_target)();
}

/// `IterancePreset::readCurrentValue` converts the leftmost note's raw iterance to a preset index
/// (`note/iterance_preset.h:44-52`, `note_row/iterance_preset.h:48-60`); the bridge hands back the
/// raw int, so the conversion happens here (`note_bridge.h`'s own doc: "Task 5's leaves convert with
/// `Iterance::fromInt(...).toPresetIndex()` themselves").
int32_t get_iterance_preset(const IteranceFamily& family) {
	return Iterance::fromInt(iterance_raw(family)).toPresetIndex();
}

/// `IterancePreset::selectButtonPress()`'s own test (`note/iterance_preset.h:59-67`,
/// `note_row/iterance_preset.h:72-80`), null-bridge-safe: `tables_3_spec.cpp`'s whole-tree
/// "never lets a select_chain hook name a node outside its own chain_targets" sweep calls every
/// reachable node's `select_chain` -- `chain_note_iterance`/`chain_note_row_iterance` included --
/// with no bridge installed at all, because that spec validates the whole sound tree and never
/// touches the note family.
bool iterance_is_custom(const IteranceFamily& family) {
	if (note_bridge() == nullptr) {
		return false;
	}
	return get_iterance_preset(family) == kCustomIterancePreset;
}

/// `IteranceDivisor::readCurrentValue`/`writeCurrentValue` (`note/iterance_divisor.h:40-69`,
/// `note_row/iterance_divisor.h:41-78`).
/// @{
int32_t get_iterance_divisor(const IteranceFamily& family) {
	const Iterance iterance = resolved_iterance(iterance_raw(family));
	return std::clamp<int32_t>(iterance.divisor, 1, 8);
}
void set_iterance_divisor(const IteranceFamily& family, int32_t value) {
	Iterance iterance = resolved_iterance(iterance_raw(family));
	const int32_t mask = (1 << value) - 1; // `note/iterance_divisor.h:64`, `note_row/iterance_divisor.h:73`
	const int32_t new_steps = (static_cast<int32_t>(iterance.toInt()) & 0xFF) & mask;
	iterance_raw_set(family, Iterance{static_cast<uint8_t>(value), new_steps}.toInt());
}
/// @}

/// `IteranceStepToggle::readCurrentValue`/`::writeCurrentValue`/`::isRelevant`
/// (`note/iterance_step_toggle.h:34-80`, `note_row/iterance_step_toggle.h:54-110`), indexed by
/// @p index. `isRelevant` alone reads the **unconverted** iterance (it tests `kDefaultIteranceValue`
/// directly, which `resolved_iterance` would already have converted away) and alone returns false
/// outright with nothing to edit -- legacy's own no-note (`note/iterance_step_toggle.h:79`) / no-row
/// (`note_row/iterance_step_toggle.h:109`) arm, transcribed here as `iterance_has_target`.
/// @{
int32_t iterance_step_get(const IteranceFamily& family, int32_t index) {
	const Iterance iterance = resolved_iterance(iterance_raw(family));
	return iterance.iteranceStep[static_cast<size_t>(index)] ? 1 : 0;
}
void iterance_step_set(const IteranceFamily& family, int32_t index, int32_t value) {
	Iterance iterance = resolved_iterance(iterance_raw(family));
	int32_t new_steps = static_cast<int32_t>(iterance.toInt()) & 0xFF;
	if (value != 0) {
		new_steps |= (1 << index);
	}
	else {
		new_steps &= ~(1 << index);
	}
	iterance_raw_set(family, Iterance{iterance.divisor, new_steps}.toInt());
}
bool iterance_step_relevant(const IteranceFamily& family, int32_t index) {
	// `validate_tree`'s `EmptySubmenu`/`DuplicateName` sweeps call every reachable `relevant` under
	// whatever contexts a caller supplies, including host specs (`tables_2_spec.cpp`,
	// `tables_3_spec.cpp`) that validate the whole tree (`nodes::kAllRoots`) without ever installing
	// a note bridge -- unlike `get`/`set`/`adjust`/`permitted`/`value_text`, which nothing calls
	// outside a menu actually open on a note, `relevant` is reachable with no bridge at all.
	if (note_bridge() == nullptr) {
		return false;
	}
	// Legacy's own no-note/no-row arm (see this function's own doc): with nothing selected, every
	// step is not relevant -- not just index 0's "OFF" case.
	if (!iterance_has_target(family)) {
		return false;
	}
	const Iterance iterance = Iterance::fromInt(iterance_raw(family));
	return (iterance == kDefaultIteranceValue && index == 0) || iterance.divisor > index;
}
/// @}

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
/// `InstrumentClipView::getFillString` (`gui/views/instrument_clip_view.cpp:3563-3576`) spell the
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

int32_t get_note_iterance(const MenuContext&) {
	return get_iterance_preset(kNoteFamily);
}
void adjust_note_iterance(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_iterance(offset); // `note/iterance_preset.h:54-57`
}
bool note_iterance_is_custom(const MenuContext&) {
	return iterance_is_custom(kNoteFamily);
}

int32_t get_note_fill(const MenuContext&) {
	return note_bridge()->note_fill(); // `note/fill.h:44-50`
}
void adjust_note_fill(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_note_fill(offset); // `note/fill.h:52-55`
}

int32_t get_note_iterance_divisor(const MenuContext&) {
	return get_iterance_divisor(kNoteFamily);
}
void set_note_iterance_divisor(const MenuContext&, int32_t value) {
	set_iterance_divisor(kNoteFamily, value);
}

#define DELUGE_ITERANCE_STEP_HOOKS(prefix, family, i, idx)                                                             \
	int32_t get_##prefix##_iterance_step_##i(const MenuContext&) {                                                     \
		return iterance_step_get(family, idx);                                                                         \
	}                                                                                                                  \
	void set_##prefix##_iterance_step_##i(const MenuContext&, int32_t value) {                                         \
		iterance_step_set(family, idx, value);                                                                         \
	}                                                                                                                  \
	bool relevant_##prefix##_iterance_step_##i(const MenuContext&) {                                                   \
		return iterance_step_relevant(family, idx);                                                                    \
	}

DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 1, 0)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 2, 1)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 3, 2)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 4, 3)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 5, 4)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 6, 5)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 7, 6)
DELUGE_ITERANCE_STEP_HOOKS(note, kNoteFamily, 8, 7)

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
	return get_iterance_preset(kRowFamily);
}
void adjust_note_row_iterance(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_row_iterance(offset); // `note_row/iterance_preset.h:62-70`
}
bool note_row_iterance_is_custom(const MenuContext&) {
	return iterance_is_custom(kRowFamily);
}

int32_t get_note_row_fill(const MenuContext&) {
	return note_bridge()->row_fill(); // `note_row/fill.h:44-54`
}
void adjust_note_row_fill(const MenuContext&, int32_t offset) {
	note_bridge()->adjust_row_fill(offset); // `note_row/fill.h:56-65`
}

int32_t get_note_row_iterance_divisor(const MenuContext&) {
	return get_iterance_divisor(kRowFamily);
}
void set_note_row_iterance_divisor(const MenuContext&, int32_t value) {
	set_iterance_divisor(kRowFamily, value);
}

DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 1, 0)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 2, 1)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 3, 2)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 4, 3)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 5, 4)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 6, 5)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 7, 6)
DELUGE_ITERANCE_STEP_HOOKS(note_row, kRowFamily, 8, 7)
#undef DELUGE_ITERANCE_STEP_HOOKS

} // namespace deluge::scenes::menu::nodes
