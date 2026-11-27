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

/// @file
/// @brief The one place the menu system touches the model. See `bindings.h` for the interface.
///
/// A node edits through its target: `ui/scenes/bind` reads its value, range, names and applicability from its
/// descriptor and the model, and its writes go to the menu's write sink as requests the app applies as Edits; a device
/// setting's target reads and writes the device port instead, which holds the settings no song does. A param node's
/// target is a PARAM target (`ui/scenes/bind/param_display.h` for its display; the app's sink writes it through
/// SetParamValue, or SetKitParamValue under affect-entire). What remains here is the hook nodes' own read and write,
/// the per-kind ranges, names and learn.
///
/// **Port table** -- one entry per line (a markdown table here gets reflowed into an unreadable
/// run-on by clang-format). Every line below is transcribed, not called: `gui/menu_item/` is behind the
/// `scenes-includes-outside` wall.
///
///   * 0..50 default -- `Param::getMaxValue`/`getMinValue` (`origin/main:src/deluge/gui/menu_item/param.h`)
///   * enum default -- `Enumeration::selectEncoderAction`'s `[0, numOptions)` clamp
///     (`origin/main:src/deluge/gui/menu_item/enumeration.cpp`)
///   * toggle default -- `Toggle::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/toggle.cpp`), a
///     `Value<bool>`
///   * automation refresh -- `automationView.possiblyRefreshAutomationEditorGrid` (`Integer::writeCurrentValue` in
///     `origin/main:src/deluge/gui/menu_item/patched_param/integer.cpp`, `UnpatchedParam::writeCurrentValue`
///     (`origin/main:src/deluge/gui/menu_item/unpatched_param.cpp`))
///   * patched learn -- `PatchedParam::getLearningThing` (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`)
///   * unpatched learn -- `UnpatchedParam::getLearningThing`
///     (`origin/main:src/deluge/gui/menu_item/unpatched_param.cpp`)
///
/// **What did NOT come across.**
///
///   * The UI-mode half of the affect-entire condition. Legacy asks
///     `currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()`
///     (`UnpatchedParam::writeCurrentValue`). The first half is a `currentUIMode` read, which this layer may not do, so
///     it arrives as `MenuContext::affect_entire_held` -- a field the caller sets and `context_for` never does. It is
///     deliberately NOT `MenuContext::affect_entire`: that is the *clip's*
///     `InstrumentClip::affectEntire` setting, which is true on the kit-row grid-shortcut path
///     (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) where legacy edits one row only.
///   * `getRootUI() == &automationView` and the `lastSelectedParamID`/`Kind` match inside
///     `possiblyRefreshAutomationEditorGrid` (`Integer::writeCurrentValue` in
///     `origin/main:src/deluge/gui/menu_item/patched_param/integer.cpp`, `automation_view.cpp`). The notify design
///     replaces "am I in the automation view, and is this the param it is showing?" with unconditional facts. See
///     `write()`'s own comment for which are raised and why.
///
/// **`currentSong` references in this file: 0** (a ratchet holds it there).

#include "ui/scenes/menu/bindings.h"
#include "gui/l10n/short_label.h"

#include "ui/scenes/bind/param_display.h"

#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/slot_reader.h"

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/note_bridge.h"
#include "ui/scenes/profile.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h" // a device setting's "restart to apply"
#include "gui/l10n/strings.h"
#include "plugin/types/display_mapping.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

namespace deluge::ui::scenes::menu::bindings {

namespace {

/// Whether the dispatch can read @p target in @p ctx: a DEVICE row reads the device port alone (which the dispatch
/// checks), every other target the model and the bind ports.
bool readable(const deluge::ui::scenes::bind::Target& target, const MenuContext& ctx) {
	return target.on_device() || (ctx.model != nullptr && ctx.ports.has_bind_ports());
}

/// The addresses @p ctx's targets resolve against: the context's, with the output its selector chose for a
/// source-indexed target, where it chose one.
deluge::ui::scenes::bind::Addresses addresses_of(const MenuContext& ctx) {
	deluge::ui::scenes::bind::Addresses where = ctx.addresses;
	if (ctx.source_index >= 0) {
		where.source_index = static_cast<uint8_t>(ctx.source_index);
	}
	return where;
}

/// What @p target shows, or nullopt where it does not resolve or the menu has nothing to read it from.
std::optional<deluge::ui::scenes::bind::View> target_view(const deluge::ui::scenes::bind::Target& target,
                                                          const MenuContext& ctx) {
	if (!readable(target, ctx)) {
		return std::nullopt;
	}
	return deluge::ui::scenes::bind::view(target, ctx.model, addresses_of(ctx), ctx.ports);
}

/// What a target node shows, or nullopt where its target does not resolve or the menu has nothing to read it from.
std::optional<deluge::ui::scenes::bind::View> target_view(const Node& node, const MenuContext& ctx) {
	return target_view(node.bind.target, ctx);
}

/// The upper edge of range node @p node: its own target with the spec's upper row, or nullopt for a node with none.
std::optional<deluge::ui::scenes::bind::Target> upper_target(const Node& node) {
	const RangeSpec* spec = node.bind.range_spec;
	if (!node.bind.target.valid() || spec == nullptr) {
		return std::nullopt;
	}
	deluge::ui::scenes::bind::Target upper = node.bind.target;
	upper.path = spec->upper_path;
	return upper;
}

/// @brief The twin @p node edits in @p ctx: on the kit's own effects, its `kit_fx_twin`; null anywhere else.
const KitFxTwin* twin_in(const Node& node, const MenuContext& ctx) {
	return ctx.is_kit && ctx.is_kit_global_fx ? kit_fx_twin(node) : nullptr;
}

/// @brief The node whose binding @p node reads and writes in @p ctx: its twin's param on the kit's own effects.
const Node& bound(const Node& node, const MenuContext& ctx) {
	const KitFxTwin* twin = twin_in(node, ctx);
	return twin != nullptr ? *twin->param : node;
}

/// The text of a choice listed although it no longer applies: "<name> (<n/a>)". One buffer per listed entry, valid
/// until the next names() call, as the shell's TextScratch is.
std::array<std::array<char, 40>, kMaxDynamicNames> unavailable_text{};

/// @brief How many `Kind::Enum` options @p node has -- the input to its default range.
///
/// This is the *option* count, legacy's `Enumeration::size()`, not the number of names a particular caller has room to
/// render: `Enumeration::selectEncoderAction` clamps against `size()` on every detent
/// (`origin/main:src/deluge/gui/menu_item/enumeration.cpp`), so running a `dynamic_names` hook to obtain it is exactly
/// what legacy does, at the same rate. That is why the window here is the full `kMaxDynamicNames` (`node.h` -- the
/// documented cap on what one call may produce) rather than whatever `out` span `names()` was handed: a caller with
/// room for four names still edits a ten-option enum over its real 0..9 range. Cost is a `std::string_view[64]` -- 512
/// bytes on the 32-bit target -- in this frame for the duration of the hook call.
size_t name_count(const Node& node, const MenuContext& ctx) {
	if (node.bind.dynamic_names != nullptr) {
		std::string_view window[kMaxDynamicNames]{};
		return std::min(node.bind.dynamic_names(ctx, std::span<std::string_view>{window}), kMaxDynamicNames);
	}
	return std::min(node.bind.names.size(), kMaxDynamicNames);
}

/// @brief A write of menu value @p value to DEVICE target @p target: through the device port, never the song's sink.
///        A setting the device applies only after a restart says so, as the USB audio item does
///        (`STRING_FOR_RESTART_TO_APPLY`); a stored write then reaches the open menu through @p after_write, and
///        repaints the pad grid too where @p repaints_grid says the grid shows it.
/// @return Whether the device kept the value: false where the target did not resolve or the port refused it.
bool write_device(const deluge::ui::scenes::bind::Target& target, SetFn after_write, bool repaints_grid,
                  const MenuContext& ctx, int32_t value) {
	using deluge::ui::scenes::bind::DeviceWrite;
	const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
	    deluge::ui::scenes::bind::request_for(target, ctx.model, addresses_of(ctx), ctx.ports, value);
	if (!request || ctx.ports.device == nullptr) {
		return false;
	}
	const DeviceWrite done = ctx.ports.device->write(request->target.path, request->menu_value);
	if (done == DeviceWrite::REFUSED) {
		return false;
	}
	if (done == DeviceWrite::STORED_FOR_RESTART) {
		deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_RESTART_TO_APPLY));
	}
	if (after_write != nullptr) {
		after_write(ctx, request->menu_value);
	}
	base::notify::raise(base::notify::Dirty::Oled);
	if (repaints_grid) {
		base::notify::raise(base::notify::Dirty::GridMain);
	}
	return true;
}

} // namespace

/// Legacy `UnpatchedParam::writeCurrentValue` / `patched_param::Integer::writeCurrentValue` -- write the whole kit
/// rather than the selected row? `SoundEditor::editingKitRow` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is
/// `is_kit && !is_kit_global_fx`; the held-button half arrives as `affect_entire_held` (see the file header). Declared
/// in `bindings.h` because a couple of the tables' own `set` hooks branch on it.
bool writes_whole_kit(const MenuContext& ctx) {
	return ctx.affect_entire_held && ctx.is_kit && !ctx.is_kit_global_fx && ctx.addresses.kit_output.has_value()
	       && ctx.addresses.clip.has_value();
}

void write_song(const MenuContext& ctx, std::string_view path, int32_t value) {
	if (ctx.ports.writes == nullptr) {
		return;
	}
	(void)ctx.ports.writes->write({.target = {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
	                                          .path = path,
	                                          .group = deluge::project::SettingGroupId::SONG},
	                               .menu_value = value});
}

bool relevant(const Node& node, const MenuContext& ctx) {
	if (const KitFxTwin* twin = twin_in(node, ctx); twin != nullptr) {
		return target_view(*twin->param, ctx).has_value() && twin->listed(ctx);
	}
	if (node.bind.target.valid() && node.relevance_replaces_target) {
		return node.relevant == nullptr || node.relevant(ctx);
	}
	if (node.bind.target.valid()) {
		const std::optional<deluge::ui::scenes::bind::View> shown = target_view(node, ctx);
		return shown && shown->applies && (node.relevant == nullptr || node.relevant(ctx));
	}
	return node.relevant == nullptr || node.relevant(ctx);
}

size_t listed_children(const Node& node, const MenuContext& ctx, std::span<const Node*> out, ListFor list_for) {
	size_t kept = 0;
	if (node.is_group) {
		// `HorizontalMenuGroup` pages across its members: their columns in order, each member listing its own (a FIXED
		// member keeps its irrelevant ones). A member with nothing listed contributes nothing.
		for (const Node* member : node.children) {
			if (member != nullptr && kept < out.size()) {
				kept += listed_children(*member, ctx, out.subspan(kept), ListFor::Columns);
			}
		}
		return kept;
	}
	const bool keep_irrelevant = list_for == ListFor::Columns && node.fixed_columns;
	if (node.dynamic_children != nullptr) {
		const size_t produced = std::min(node.dynamic_children(ctx, out), out.size());
		for (size_t i = 0; i < produced; i++) {
			const Node* child = out[i];
			if (child != nullptr && (keep_irrelevant || relevant(*child, ctx))) {
				out[kept++] = child; // kept <= i always, so this compaction never overwrites ahead
			}
		}
		return kept;
	}
	for (const Node* child : node.children) {
		if (kept >= out.size()) {
			break;
		}
		if (child != nullptr && (keep_irrelevant || relevant(*child, ctx))) {
			out[kept++] = child;
		}
	}
	return kept;
}

const Node* group_member_of(const Node& group, const MenuContext& ctx, size_t column) {
	if (!group.is_group) {
		return &group;
	}
	size_t first = 0;
	for (const Node* member : group.children) {
		if (member == nullptr) {
			continue;
		}
		std::array<const Node*, kMaxDynamicChildren> kids{};
		const size_t n = listed_children(*member, ctx, kids, ListFor::Columns);
		if (column < first + n) {
			return member;
		}
		first += n;
	}
	return nullptr;
}

std::optional<int32_t> stored(const deluge::ui::scenes::bind::Target& target, const MenuContext& ctx) {
	const std::optional<deluge::ui::scenes::bind::Target> named =
	    deluge::ui::scenes::bind::concrete(target, addresses_of(ctx));
	if (!named || ctx.model == nullptr || ctx.ports.sound == nullptr || !ctx.addresses.slot) {
		return std::nullopt;
	}
	return deluge::ui::scenes::bind::SlotReader{*ctx.model, *ctx.addresses.slot, *ctx.ports.sound}.read(named->path);
}

std::optional<int32_t> listed_choice(const Node& asked, const MenuContext& ctx, int32_t value) {
	const Node& node = bound(asked, ctx);
	if (!node.bind.target.valid() || !readable(node.bind.target, ctx)) {
		return std::nullopt;
	}
	const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
	    deluge::ui::scenes::bind::request_for(node.bind.target, ctx.model, addresses_of(ctx), ctx.ports, value);
	if (!request) {
		return std::nullopt;
	}
	return request->menu_value;
}

int32_t read(const Node& asked, const MenuContext& ctx) {
	const Node& node = bound(asked, ctx);
	const Binding& bind = node.bind;

	if (bind.target.valid()) {
		const std::optional<deluge::ui::scenes::bind::View> shown = target_view(node, ctx);
		return shown ? shown->value : 0;
	}

	// `get` first, then `param` -- `node.h`'s documented precedence. A node with both is one of
	// legacy's `menu_item::Transpose` derivatives, whose value lives outside the param set and
	// whose `ParamRef` exists only so `learn_target` below has something to bind.
	if (bind.get != nullptr) {
		return bind.get(ctx);
	}

	return 0;
}

bool write(const Node& asked, const MenuContext& ctx, int32_t value) {
	const Node& node = bound(asked, ctx);
	const Binding& bind = node.bind;

	// A target node: the request goes to the menu's sink, which applies the target's Edit; a device setting's goes to
	// the device port, which stores it outside any song and its undo. The stale entry, a value outside the range, an
	// unresolved target and a refused write ask for nothing and raise nothing.
	if (bind.target.valid()) {
		if (bind.target.on_device()) {
			return write_device(bind.target, bind.after_write, bind.repaints_grid, ctx, value);
		}
		if (!readable(bind.target, ctx) || ctx.ports.writes == nullptr) {
			return false;
		}
		const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
		    deluge::ui::scenes::bind::request_for(bind.target, ctx.model, addresses_of(ctx), ctx.ports, value);
		if (!request || !ctx.ports.writes->write(*request)) {
			return false;
		}
		base::notify::raise(base::notify::Dirty::Oled);
		if (bind.repaints_grid) {
			base::notify::raise(base::notify::Dirty::GridMain);
		}
		if (bind.target.kind == deluge::ui::scenes::bind::Target::Kind::PARAM) {
			// Three facts, replacing three legacy effects. Each is unconditional here where legacy was
			// gated; over-invalidation, never loss.
			//
			//  * `Dirty::ParamAutomation` and `Dirty::ModTarget` -- the OLED half. Legacy reaches it two ways:
			//    `possiblyRefreshAutomationEditorGrid` (`Integer::writeCurrentValue` in
			//    `origin/main:src/deluge/gui/menu_item/patched_param/integer.cpp` /
			//    `UnpatchedParam::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/unpatched_param.cpp`)),
			//    and the knob-indicator refresh `setCurrentValueInResponseToUserInput` triggers through
			//    `ParamManager::notifyParamModifiedInSomeWay` -> `View::notifyParamAutomationOccurred`, whose
			//    deferred `displayAutomation()` calls `setKnobIndicatorLevels()` (`View::displayAutomation`).
			//    `legacy_redraw_for` lists both enumerators under `r.oled` (`gui/ui/notify_drain_mapping.cpp`).
			//
			//  * `Dirty::GridMain` -- the PAD half, and the reason the pair above is not enough.
			//    `possiblyRefreshAutomationEditorGrid` ends in `uiNeedsRendering(&automationView)`
			//    (`AutomationView::possiblyRefreshAutomationEditorGrid`), i.e. a main-pad re-render
			//    (`AutomationView::renderMainPads`) -- and `ParamAutomation`/`ModTarget` appear *only* in `r.oled`,
			//    never in `r.main_rows` (`legacy_redraw_for` in `notify_drain_mapping.cpp`). Without this third
			//    raise, editing the param the automation view is showing would stop repainting its grid the moment
			//    `MenuScene` replaces the legacy items. `Dirty::GridMain` is the enumerator `r.main_rows` carries
			//    for exactly this; the mapping file itself is left alone.
			//
			//    Coarser than legacy on purpose: `possiblyRefreshAutomationEditorGrid` repaints only when the
			//    edited param *is* the view's `lastSelectedParamID`/`Kind` (`gui/views/automation_view.cpp`),
			//    a comparison this layer cannot make -- it may not read `automationView` or the root UI. So
			//    every param write asks for one grid repaint the drain already coalesces, rather than
			//    silently dropping the one that mattered.
			base::notify::raise(base::notify::Dirty::ParamAutomation);
			base::notify::raise(base::notify::Dirty::ModTarget);
			base::notify::raise(base::notify::Dirty::GridMain);
		}
		return true;
	}

	// A `get`/`set` (or `action`) node: the hook owns the model write, so all this layer knows is
	// that something the OLED shows has changed. Tested before `param` -- `node.h`'s documented
	// precedence, and the reason a `menu_item::Transpose` node can carry a learn-only `ParamRef`
	// without this function writing to it. The hook reports no refusal, so the write stands as taken.
	if (bind.set != nullptr) {
		bind.set(ctx, value);
		base::notify::raise(base::notify::Dirty::Oled);
		return true;
	}
	// Nothing to write, so nothing refused it.
	return true;
}

int32_t read_upper(const Node& node, const MenuContext& ctx) {
	const std::optional<deluge::ui::scenes::bind::Target> upper = upper_target(node);
	const std::optional<deluge::ui::scenes::bind::View> shown = upper ? target_view(*upper, ctx) : std::nullopt;
	return shown ? shown->value : 0;
}

bool write_upper(const Node& node, const MenuContext& ctx, int32_t value) {
	// A range's ends are DEVICE rows; the upper edge writes as the lower one does, and the node's after-write is the
	// lower edge's alone. A node with no DEVICE upper row has nothing to write, so nothing refused it.
	const std::optional<deluge::ui::scenes::bind::Target> upper = upper_target(node);
	if (upper && upper->on_device()) {
		return write_device(*upper, nullptr, node.bind.repaints_grid, ctx, value);
	}
	return true;
}

void adjust(const Node& node, const MenuContext& ctx, int32_t offset) {
	// Mirrors `write`'s `get`/`set` hook arm above: the hook owns the model write --
	// legacy's `adjustVelocity`/`adjustProbability`/`adjustNoteIterance`/`adjustNoteFillWithOffset`
	// -- so all this layer knows is that something the OLED shows has changed.
	if (node.bind.adjust != nullptr) {
		node.bind.adjust(ctx, offset);
		base::notify::raise(base::notify::Dirty::Oled);
	}
}

IntRange range(const Node& asked, const MenuContext& ctx) {
	const Node& node = bound(asked, ctx);
	if (node.bind.target.valid()) {
		const std::optional<deluge::ui::scenes::bind::View> shown = target_view(node, ctx);
		return shown ? IntRange{shown->least, shown->greatest, 1} : IntRange{0, 0, 1};
	}
	// The order `Binding` documents (`node.h`): a dynamic hook, then the literal bounds a builder was
	// given, then the legacy per-kind default.
	if (node.bind.range != nullptr) {
		return node.bind.range(ctx);
	}
	if (node.bind.fixed_range.has_value()) {
		return *node.bind.fixed_range;
	}

	switch (node.kind) {
	case Kind::Toggle:
		// `Toggle` is a `Value<bool>` (`origin/main:src/deluge/gui/menu_item/toggle.h`) whose encoder action stores
		// `offset > 0` (`Toggle::selectEncoderAction` in `origin/main:src/deluge/gui/menu_item/toggle.cpp`).
		return IntRange{0, 1, 1};

	case Kind::Enum: {
		// `Enumeration::selectEncoderAction` clamps to `[0, numOptions)`
		// (`origin/main:src/deluge/gui/menu_item/enumeration.cpp`).
		const size_t count = name_count(node, ctx);
		return IntRange{0, count > 0 ? static_cast<int32_t>(count) - 1 : 0, 1};
	}

	default:
		// `origin/main:src/deluge/gui/menu_item/param.h` `Param::getMaxValue` (`kMaxMenuValue` / `kMinMenuValue`),
		// forwarded by `Integer::getMaxValue` (`origin/main:src/deluge/gui/menu_item/patched_param/integer.h`) and
		// `UnpatchedParam::getMaxValue` (`origin/main:src/deluge/gui/menu_item/unpatched_param.h`).
		//
		// This arm is also where `Kind::Range` and `Kind::SyncLevel` land. Each wants its own default -- main's
		// `menu_item::Range` (`origin/main:src/deluge/gui/menu_item/range.h`) and `menu_item::SyncLevel`
		// (`origin/main:src/deluge/gui/menu_item/sync_level.h`). Until one has its own, 0..50 is a placeholder, not a
		// claim: no table declares a node that reaches here without one, and every such node carries either a
		// `fixed_range` from its builder or a `range` hook, both of which are consulted above before this switch is
		// ever reached. Deliberately not a hard failure -- a `FREEZE` in a library this layer means to keep pure would
		// be worse than a wrong number.
		return IntRange{0, plugin::kMaxMenuSteps, 1};
	}
}

size_t names(const Node& node, const MenuContext& ctx, std::span<std::string_view> out,
             std::span<deluge::ui::shell::TextScratch> scratch, NameForm form) {
	// The two spans are documented as the same length; the shorter one wins if a caller gets it
	// wrong, so a mismatch truncates rather than writing past either end.
	const size_t limit = std::min({out.size(), scratch.size(), kMaxDynamicNames});
	if (limit == 0) {
		return 0;
	}

	const deluge::ui::shell::BoardProfile& text = scenes::deluge_profile();
	const bool short_form = form == NameForm::Short && node.short_options;
	// An id's text in the form asked for: the short table's entry where the node shortens and there is one.
	const auto name_of = [&](uint16_t id, deluge::ui::shell::TextScratch& slot) {
		if (short_form) {
			if (const auto shortened = deluge::l10n::shortForm(static_cast<deluge::l10n::String>(id))) {
				return *shortened;
			}
		}
		return text.text_or_digits(id, slot);
	};
	if (node.bind.target.valid()) {
		if (!readable(node.bind.target, ctx)) {
			return 0;
		}
		std::array<deluge::ui::scenes::bind::ChoiceName, kMaxDynamicNames> listed{};
		const size_t count = std::min(
		    deluge::ui::scenes::bind::choice_names(node.bind.target, ctx.model, addresses_of(ctx), ctx.ports, listed),
		    limit);
		for (size_t i = 0; i < count; ++i) {
			const std::string_view name =
			    form == NameForm::Short && listed[i].short_name != deluge::l10n::String::EMPTY_STRING
			        ? text.text_or_digits(static_cast<uint16_t>(listed[i].short_name), scratch[i])
			        : name_of(static_cast<uint16_t>(listed[i].name), scratch[i]);
			if (!listed[i].unavailable) {
				out[i] = name;
				continue;
			}
			deluge::ui::shell::TextScratch mark_scratch{};
			const std::string_view mark = text.text_or_digits(
			    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_CHOICE_UNAVAILABLE), mark_scratch);
			const int written =
			    snprintf(unavailable_text[i].data(), unavailable_text[i].size(), "%.*s (%.*s)",
			             static_cast<int>(name.size()), name.data(), static_cast<int>(mark.size()), mark.data());
			out[i] = std::string_view(unavailable_text[i].data(), std::min(static_cast<size_t>(std::max(written, 0)),
			                                                               unavailable_text[i].size() - 1));
		}
		return count;
	}

	if (node.bind.dynamic_names != nullptr) {
		// The hook writes views it owns (static strings, or storage of its own); `scratch` is for
		// the id path only, and `DynamicNamesFn`'s signature (node.h) has nowhere to pass it.
		return std::min(node.bind.dynamic_names(ctx, out.first(limit)), limit);
	}

	// Ids resolve through the profile's `text_or_digits`, never `text_lookup` directly --
	// `BoardProfile::text_or_digits` already implements the digits fallback for an id with no entry.
	const size_t count = std::min(node.bind.names.size(), limit);
	for (size_t i = 0; i < count; ++i) {
		out[i] = name_of(node.bind.names[i], scratch[i]);
	}
	return count;
}

std::optional<deluge::project::ControlsAddress> controls_owner(const MenuContext& ctx) {
	using Kind = deluge::project::ControlsAddress::Kind;
	if (const auto& slot = ctx.addresses.slot) {
		return deluge::project::ControlsAddress{
		    .kind = slot->kind == deluge::project::SlotAddress::Kind::DRUM ? Kind::DRUM : Kind::OUTPUT, .id = slot->id};
	}
	if (const auto& chain = ctx.addresses.chain) {
		return chain->kind == deluge::project::ChainAddress::Kind::SONG
		           ? deluge::project::ControlsAddress{.kind = Kind::SONG}
		           : deluge::project::ControlsAddress{.kind = Kind::OUTPUT, .id = chain->id};
	}
	return std::nullopt;
}

std::optional<deluge::project::OwnerParamTarget> learn_target(const Node& node, const MenuContext& ctx) {
	// `PatchedParam`/`UnpatchedParam::getLearningThing` (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`,
	// `origin/main:src/deluge/gui/menu_item/unpatched_param.cpp`):
	// the node's param, by its engine id.
	const std::optional<deluge::project::ControlsAddress> owner = controls_owner(ctx);
	if (!node.bind.param.valid() || !owner) {
		return std::nullopt;
	}
	// An arp param is named in the arp's space on any owner.
	const deluge::project::ParamTarget param =
	    node.bind.param.kind == deluge::plugin::ParamKind::ARP
	        ? deluge::project::ParamTarget{.space = deluge::project::ParamSpace::ARP,
	                                       .id = param_id_of(node.bind.param)}
	        : deluge::project::engine_target(param_id_of(node.bind.param).value);
	return deluge::project::OwnerParamTarget{.owner = *owner, .param = param};
}

// ---------------------------------------------------------------------------------------------
// The value scaling `get`/`set` items reuse (`bindings.h`'s note)

int32_t standard_to_menu(int32_t stored) {
	return deluge::ui::scenes::bind::menu_of(deluge::plugin::MappingKind::STANDARD, stored);
}
int32_t menu_to_standard(int32_t menu_value) {
	return deluge::ui::scenes::bind::stored_of(deluge::plugin::MappingKind::STANDARD, menu_value);
}
int32_t pan_to_menu(int32_t stored) {
	return deluge::ui::scenes::bind::menu_of(deluge::plugin::MappingKind::PAN, stored);
}
int32_t menu_to_pan(int32_t menu_value) {
	return deluge::ui::scenes::bind::stored_of(deluge::plugin::MappingKind::PAN, menu_value);
}
int32_t unsigned_to_menu(uint32_t stored) {
	// `computeCurrentValueForUnsignedMenuItem` (`util/value_scaling.cpp`).
	return ((int64_t)stored * plugin::kMaxMenuSteps + 2147483648) >> 32;
}
uint32_t menu_to_unsigned(int32_t menu_value) {
	// `computeFinalValueForUnsignedMenuItem` (`util/value_scaling.cpp`).
	return (uint32_t)menu_value * 85899345;
}

// ---------------------------------------------------------------------------------------------
// The play direction (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`)

namespace {

/// @brief Where the play direction is: `sequence::Direction::getIndividualNoteRow`
///        (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`), minus the audition arm `bindings.h` documents
///        as belonging to `NoteClipScene`. A kit row the clip is not on affect-entire for has its own (the kit-row arm
///        of `Direction::getIndividualNoteRow`), when it has a note row; anything else edits the clip's.
deluge::ui::scenes::bind::Target direction_target(const MenuContext& ctx) {
	const bool row = !ctx.affect_entire && ctx.is_kit && ctx.addresses.lane.has_value();
	return {.kind = deluge::ui::scenes::bind::Target::Kind::MODEL,
	        .path = "sequenceDirection",
	        .group = row ? deluge::project::SettingGroupId::NOTE_ROW : deluge::project::SettingGroupId::CLIP};
}

/// @brief The addresses the play direction resolves against: the kit row's note row is its lane's, whatever kind of
///        drum it plays.
deluge::ui::scenes::bind::Addresses direction_where(const MenuContext& ctx) {
	deluge::ui::scenes::bind::Addresses where = addresses_of(ctx);
	if (ctx.addresses.lane && ctx.addresses.clip) {
		where.note_row = deluge::project::NoteRowAddress{.clip_id = ctx.addresses.clip->clip_id,
		                                                 .is_kit = true,
		                                                 .row_key = static_cast<int32_t>(ctx.addresses.lane->drum_id)};
	}
	return where;
}

/// @brief Is @p ctx one this pair of functions can work on at all: an instrument clip of the song the app plays?
bool has_instrument_clip(const MenuContext& ctx) {
	return ctx.addresses.clip.has_value() && !ctx.is_audio_clip && ctx.model != nullptr && ctx.ports.has_bind_ports();
}

// `getIndividualNoteRow`'s non-kit auditioned-row arm (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`),
// gated as legacy gates it by `soundEditor.selectedNoteRow` -- `MenuContext::selected_note_row`. Under
// `Root::NoteRowEditor` the latch is always true: `InstrumentClipView::enterNoteRowEditor` (`instrument_clip_view.cpp`)
// opens only with one row auditioning, and the editor's sidebar arm never ends that mode while it stays open
// (`InstrumentClipView::handleNoteRowEditorPadAction`), so the root answers for it.
bool note_row_bridge_direction(const MenuContext& ctx, int32_t* value) {
	return (ctx.root == MenuContext::Root::NoteRowEditor || ctx.selected_note_row) && !ctx.is_kit
	       && note_bridge() != nullptr && note_bridge()->row_sequence_direction(value);
}

} // namespace

int32_t read_sequence_direction(const MenuContext& ctx, bool* from_note_row) {
	if (from_note_row != nullptr) {
		*from_note_row = false;
	}
	int32_t bridged = 0;
	if (note_row_bridge_direction(ctx,
	                              &bridged)) { // `Direction::readCurrentValue`'s note-row arm, through `NoteBridge`
		if (from_note_row != nullptr) {
			*from_note_row = true;
		}
		return bridged;
	}
	if (!has_instrument_clip(ctx)) {
		return 0;
	}
	const deluge::ui::scenes::bind::Target target = direction_target(ctx); // `Direction::readCurrentValue`
	if (from_note_row != nullptr) {
		*from_note_row = target.group == deluge::project::SettingGroupId::NOTE_ROW;
	}
	const std::optional<deluge::ui::scenes::bind::View> shown =
	    deluge::ui::scenes::bind::view(target, ctx.model, direction_where(ctx), ctx.ports);
	return shown ? shown->value : 0;
}

void write_sequence_direction(const MenuContext& ctx, int32_t value) {
	// This call is the GATE, not a redundant resolve-then-resolve-again --
	// `note_row_bridge_direction` answering `false` (no row, or not this root/clip shape) is exactly
	// what sends the write down to the kit-row arm and clip fallback below instead, so the value it
	// reads back is never used, only whether it answered at all. Calling it here and then having
	// `set_row_sequence_direction` resolve the row a second time (rather than threading the first
	// resolution through) is safe because `NoteBridge`'s row resolution is idempotent: on a non-kit
	// clip with no row yet, both calls reach the identical `createNoteRowForYDisplay` at the same
	// `lastAuditionedYDisplay` (`shell_note_bridge.cpp`'s `noteRowForSequenceDirection`), and the
	// second call finds the row the first one already created rather than making another.
	int32_t existing = 0;
	if (note_row_bridge_direction(ctx,
	                              &existing)) { // `Direction::writeCurrentValue`'s note-row arm, through `NoteBridge`
		note_bridge()->set_row_sequence_direction(value);
		deluge::base::notify::raise(deluge::base::notify::Dirty::Oled);
		return;
	}
	if (!has_instrument_clip(ctx) || ctx.ports.writes == nullptr) {
		return;
	}
	// `Direction::writeCurrentValue` (`origin/main:src/deluge/gui/menu_item/sequence/direction.h`): the kit row's note
	// row, else the clip.
	const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
	    deluge::ui::scenes::bind::request_for(direction_target(ctx), ctx.model, direction_where(ctx), ctx.ports, value);
	if (request) {
		(void)ctx.ports.writes->write(*request);
	}
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled);
}

} // namespace deluge::ui::scenes::menu::bindings
