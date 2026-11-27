#include "ui/scenes/menu/nodes/note.h"

#include "ui/scenes/menu/note_bridge.h"

#include "base/notify/notice_queue.h"
#include "project/iterance.h"
#include "project/note_probability.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

using deluge::project::Iterance;

namespace deluge::ui::scenes::menu::nodes {

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

/// `InstrumentClipView::getFillString` (`gui/views/instrument_clip_view.cpp`): each `deluge::project::FillMode`'s word,
/// by value.
constexpr std::array<std::string_view, 3> kFillWords{"OFF", "NOT FILL", "FILL"};
static_assert(std::to_underlying(deluge::project::FillMode::OFF) == 0
                  && std::to_underlying(deluge::project::FillMode::NOT_FILL) == 1
                  && std::to_underlying(deluge::project::FillMode::FILL) == 2,
              "kFillWords is indexed by deluge::project::FillMode");

/// @brief `IteranceDivisor::readCurrentValue`/`writeCurrentValue`'s "OFF becomes 1of1" conversion
///        (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`,
///        `origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`), shared by both families' divisor and
///        step hooks -- the note version and the row version read the conversion the same way once each has its own raw
///        `Iterance`.
Iterance resolved_iterance(int32_t raw) {
	Iterance iterance = deluge::project::Iterance::from_packed(raw);
	if (iterance == deluge::project::kIteranceOff) {
		iterance = deluge::project::kIteranceCustom;
	}
	return iterance;
}

// -------------------------------------------------------------------------------------------
// One shared body per hook kind the note and note-row families
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

// Every read through the bridge below answers as if nothing were selected while no bridge is installed (a harness that
// never wires one, a whole-tree sweep): zero, no write, no target.
int32_t iterance_raw(const IteranceFamily& family) {
	if (note_bridge() == nullptr) {
		return 0;
	}
	return (note_bridge()->*family.raw_get)();
}
void iterance_raw_set(const IteranceFamily& family, int32_t value) {
	if (note_bridge() != nullptr) {
		(note_bridge()->*family.raw_set)(value);
	}
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
	return note_bridge() != nullptr && (note_bridge()->*family.has_target)();
}

/// `IterancePreset::readCurrentValue` converts the leftmost note's raw iterance to a preset index
/// (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`,
/// `origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`); the bridge hands back the raw int, so the
/// conversion happens here, with `deluge::project::Iterance::from_packed(...).preset_index()`.
int32_t get_iterance_preset(const IteranceFamily& family) {
	return deluge::project::Iterance::from_packed(iterance_raw(family)).preset_index();
}

/// `IterancePreset::selectButtonPress()`'s own test (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`,
/// `origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`), null-bridge-safe: `tables_3_spec.cpp`'s
/// whole-tree "never lets a select_chain hook name a node outside its own chain_targets" sweep calls every reachable
/// node's `select_chain` -- `chain_note_iterance`/`chain_note_row_iterance` included -- with no bridge installed at
/// all, because that spec validates the whole sound tree and never touches the note family.
bool iterance_is_custom(const IteranceFamily& family) {
	if (note_bridge() == nullptr) {
		return false;
	}
	return get_iterance_preset(family) == deluge::project::kCustomIterancePreset;
}

/// `IteranceDivisor::readCurrentValue`/`writeCurrentValue`
/// (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`,
/// `origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`).
/// @{
int32_t get_iterance_divisor(const IteranceFamily& family) {
	const Iterance iterance = resolved_iterance(iterance_raw(family));
	return std::clamp<int32_t>(iterance.divisor, 1, 8);
}
void set_iterance_divisor(const IteranceFamily& family, int32_t value) {
	Iterance iterance = resolved_iterance(iterance_raw(family));
	const int32_t mask =
	    (1 << value)
	    - 1; // `IteranceDivisor::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/note/iterance_divisor.h`,
	         // `origin/main:src/deluge/gui/menu_item/note_row/iterance_divisor.h`)
	const int32_t new_steps = (static_cast<int32_t>(iterance.packed()) & 0xFF) & mask;
	iterance_raw_set(family, Iterance{static_cast<uint8_t>(value), new_steps}.packed());
}
/// @}

/// `IteranceStepToggle::readCurrentValue`/`::writeCurrentValue`/`::isRelevant`
/// (`origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`,
/// `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`), indexed by
/// @p index. `isRelevant` alone reads the **unconverted** iterance (it tests `deluge::project::kIteranceOff` directly,
/// which `resolved_iterance` would already have converted away) and alone returns false outright with nothing to edit
/// -- legacy's own no-note (`IteranceStepToggle::isRelevant` in
/// `origin/main:src/deluge/gui/menu_item/note/iterance_step_toggle.h`) / no-row (`IteranceStepToggle::isRelevant` in
/// `origin/main:src/deluge/gui/menu_item/note_row/iterance_step_toggle.h`) arm, transcribed here as
/// `iterance_has_target`.
/// @{
int32_t iterance_step_get(const IteranceFamily& family, int32_t index) {
	const Iterance iterance = resolved_iterance(iterance_raw(family));
	return iterance.steps[static_cast<size_t>(index)] ? 1 : 0;
}
void iterance_step_set(const IteranceFamily& family, int32_t index, int32_t value) {
	Iterance iterance = resolved_iterance(iterance_raw(family));
	int32_t new_steps = static_cast<int32_t>(iterance.packed()) & 0xFF;
	if (value != 0) {
		new_steps |= (1 << index);
	}
	else {
		new_steps &= ~(1 << index);
	}
	iterance_raw_set(family, Iterance{iterance.divisor, new_steps}.packed());
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
	const Iterance iterance = deluge::project::Iterance::from_packed(iterance_raw(family));
	return (iterance == deluge::project::kIteranceOff && index == 0) || iterance.divisor > index;
}
/// @}

} // namespace

// -------------------------------------------------------------------------------------------
// The two guards -- `note::SelectedNote::selectEncoderActionIsPermitted()`
// (`origin/main:src/deluge/gui/menu_item/note/selected_note.h`) and `note_row::SelectedNoteRow`'s twin
// (`origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`).
// -------------------------------------------------------------------------------------------

bool permitted_note(const MenuContext&) {
	if (note_bridge() != nullptr && note_bridge()->note_selected()) {
		return true;
	}
	deluge::base::notify::notice_text("Select Note"); // `origin/main:src/deluge/gui/menu_item/note/selected_note.h`
	return false;
}

bool permitted_row(const MenuContext&) {
	if (note_bridge() != nullptr && note_bridge()->row_auditioning()) {
		return true;
	}
	deluge::base::notify::notice_text(
	    "Select Row"); // `origin/main:src/deluge/gui/menu_item/note_row/selected_note_row.h`
	return false;
}

// -------------------------------------------------------------------------------------------
// The three bespoke value texts, shared by both families.
// -------------------------------------------------------------------------------------------

/// A probability value split as the editors store it: the percentage in fives, and whether the note latches (a value
/// past `deluge::project::kProbabilityValueCount` carries the latch above its low seven bits).
struct ProbabilityShown {
	int32_t percent;
	bool latching;
};

ProbabilityShown probability_shown(int32_t value) {
	const bool latching = value > deluge::project::kProbabilityValueCount;
	return {.percent = (latching ? value & 127 : value) * 5, .latching = latching};
}

/// `Probability::getProbabilityValue` (`origin/main:src/deluge/gui/menu_item/note/probability.h`), and its two callers:
/// `renderInHorizontalMenu` (column: `"35%"`/`"35L"`) and `getNotificationValue`
/// (notification: `"35%"`/`"35% ltch"`). `note_row::Probability` is byte-identical
/// (`origin/main:src/deluge/gui/menu_item/note_row/probability.h`).
ValueText value_text_probability(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label) {
		return kValueTextFallback;
	}
	const auto [shown, latching] = probability_shown(value);

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

/// `IterancePreset::get_iterance_display_value` (`origin/main:src/deluge/gui/menu_item/note/iterance_preset.h`):
/// `OFF`/`CUSTOM` are the leaf's own literals for the two values `deluge::project::kIterancePresets` has no row for;
/// every other value is the bridge's own label (`iterance_preset_label`, itself
/// `IterancePreset::get_iterance_display_value`'s table-lookup half). `compact` chooses the column's `"%d:%d"`
/// (`renderInHorizontalMenu`) over the notification's `"%d of %d"`
/// (`getNotificationValue`) -- see `note_bridge.h`'s own doc on `iterance_preset_label`. `note_row::IterancePreset` is
/// byte-identical (`origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h`).
ValueText value_text_iterance(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label) {
		return kValueTextFallback;
	}
	if (value == deluge::project::kIterancePresetOff) {
		return ValueText{write_text("OFF", out), false};
	}
	if (value == deluge::project::kCustomIterancePreset) {
		return ValueText{write_text("CUSTOM", out), false};
	}
	if (note_bridge() == nullptr) {
		return kValueTextFallback;
	}
	const bool compact = use == ValueTextUse::Column;
	return ValueText{note_bridge()->iterance_preset_label(value, compact, out), false};
}

/// `Fill::renderInHorizontalMenu` (`origin/main:src/deluge/gui/menu_item/note/fill.h`): the column draws only
/// `"OFF"`/`"FILL"`, struck through on `NOT_FILL`. `Fill::getNotificationValue` and `InstrumentClipView::getFillString`
/// (`gui/views/instrument_clip_view.cpp`) spell the notification's three values. `note_row::Fill` is byte-identical
/// (`origin/main:src/deluge/gui/menu_item/note_row/fill.h`).
ValueText value_text_fill(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use == ValueTextUse::Label) {
		return kValueTextFallback;
	}
	if (use == ValueTextUse::Column) {
		const std::string_view text = kFillWords[value == std::to_underlying(deluge::project::FillMode::OFF)
		                                             ? std::to_underlying(deluge::project::FillMode::OFF)
		                                             : std::to_underlying(deluge::project::FillMode::FILL)];
		return ValueText{write_text(text, out), value == std::to_underlying(deluge::project::FillMode::NOT_FILL)};
	}
	const std::string_view text = value == std::to_underlying(deluge::project::FillMode::FILL)
	                                      || value == std::to_underlying(deluge::project::FillMode::NOT_FILL)
	                                  ? kFillWords[value]
	                                  : kFillWords[std::to_underlying(deluge::project::FillMode::OFF)];
	return ValueText{write_text(text, out), false};
}

size_t fill_names(const MenuContext& /*ctx*/, std::span<std::string_view> out) {
	const size_t count = std::min(kFillWords.size(), out.size());
	std::copy_n(kFillWords.begin(), count, out.begin());
	return count;
}

ValueText leaf_text_probability(const MenuContext&, int32_t value, ValueTextUse use, std::span<char> out) {
	if (use != ValueTextUse::Column) {
		return kValueTextFallback;
	}
	const auto [shown, latching] = probability_shown(value);
	char buffer[24];
	const int written =
	    std::snprintf(buffer, sizeof(buffer), "%d%%%s", static_cast<int>(shown), latching ? " (L)" : "");
	if (written <= 0) {
		return ValueText{};
	}
	return ValueText{write_text(std::string_view(buffer, static_cast<size_t>(written)), out), false};
}

ValueText leaf_text_fill(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out) {
	return use == ValueTextUse::Column ? value_text_fill(ctx, value, ValueTextUse::Notification, out)
	                                   : kValueTextFallback;
}

ValueText leaf_text_iterance(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out) {
	return use == ValueTextUse::Column ? value_text_iterance(ctx, value, ValueTextUse::Notification, out)
	                                   : kValueTextFallback;
}

// -------------------------------------------------------------------------------------------
// The note editor.
// -------------------------------------------------------------------------------------------

int32_t get_note_velocity(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/note/velocity.h` `Velocity::readCurrentValue`
	if (note_bridge() == nullptr) {
		return 0;
	}
	return note_bridge()->note_velocity();
}
void adjust_note_velocity(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note/velocity.h` `Velocity::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_note_velocity(offset);
	}
}

int32_t get_note_probability(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/note/probability.h` `Probability::readCurrentValue`
	if (note_bridge() == nullptr) {
		return 0;
	}
	return note_bridge()->note_probability();
}
void adjust_note_probability(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note/probability.h` `Probability::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_note_probability(offset);
	}
}

int32_t get_note_iterance(const MenuContext&) {
	return get_iterance_preset(kNoteFamily);
}
void adjust_note_iterance(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note/iterance_preset.h` `IterancePreset::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_note_iterance(offset);
	}
}
bool note_iterance_is_custom(const MenuContext&) {
	return iterance_is_custom(kNoteFamily);
}

int32_t get_note_fill(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/note/fill.h` `Fill::readCurrentValue`
	if (note_bridge() == nullptr) {
		return 0;
	}
	return note_bridge()->note_fill();
}
void adjust_note_fill(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note/fill.h` `Fill::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_note_fill(offset);
	}
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
	// `origin/main:src/deluge/gui/menu_item/note_row/probability.h` `Probability::readCurrentValue`
	if (note_bridge() == nullptr) {
		return 0;
	}
	return note_bridge()->row_probability();
}
void adjust_note_row_probability(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note_row/probability.h` `Probability::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_row_probability(offset);
	}
}

int32_t get_note_row_iterance(const MenuContext&) {
	return get_iterance_preset(kRowFamily);
}
void adjust_note_row_iterance(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note_row/iterance_preset.h` `IterancePreset::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_row_iterance(offset);
	}
}
bool note_row_iterance_is_custom(const MenuContext&) {
	return iterance_is_custom(kRowFamily);
}

int32_t get_note_row_fill(const MenuContext&) {
	// `origin/main:src/deluge/gui/menu_item/note_row/fill.h` `Fill::readCurrentValue`
	if (note_bridge() == nullptr) {
		return 0;
	}
	return note_bridge()->row_fill();
}
void adjust_note_row_fill(const MenuContext&, int32_t offset) {
	// `origin/main:src/deluge/gui/menu_item/note_row/fill.h` `Fill::selectEncoderAction`
	if (note_bridge() != nullptr) {
		note_bridge()->adjust_row_fill(offset);
	}
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

} // namespace deluge::ui::scenes::menu::nodes
