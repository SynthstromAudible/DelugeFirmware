#pragma once

/// @brief The declarative menu data model (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-2-brief.md`): `Node`, `Binding`,
///        `ParamRef`, and the `constexpr` builders that assemble a menu tree as plain data.
///
/// @note **This header is model-free and host-testable.** Its only model include is
///       `modulation/params/param.h`, for `params::Kind` -- pulling in `definitions_cxx.hpp` is
///       accepted collateral (see the brief). Nothing here reaches into `gui/`: l10n string ids
///       are carried as plain `uint16_t` rather than `l10n::String`, so a caller resolves them
///       through whatever string table it likes. `MenuContext` and `MenuState` are forward
///       declared only -- their definitions belong to later tasks (`context.h`, the interpreter's
///       own state) and are never needed to lay out a `Node` tree as data.
///
/// @note `OledFrame`, `UiEvent`, `UiContext` and `Result` are likewise forward declared rather than
///       pulled in via `ui/oled/frame.h` / `shell/scene.h`: `CustomItem::render`/`handle` only ever
///       *declare* those types here (as references, or as a return type never invoked from this
///       header), which never requires a complete type, so this header stays free of the shell's
///       and the oled toolkit's own dependency closures.
#include "gfx/oled/icons.h" // CustomItem::row_icon's default answer -- one `extern const Icon`
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h" // CustomItem::learn_descriptor -- returned by value
#include "ui/oled/meters.h"
#include "ui/oled/values.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

/// Forward declared for `CustomItem`'s learn hooks, which only ever take it by reference -- the
/// same reason `OledFrame` and friends below are declared rather than included. `io/midi/` is
/// inside this layer's allow-list, but this header is the model-light one and has no need of the
/// definition.
class MIDICable;

namespace deluge::ui::oled {
struct OledFrame;
} // namespace deluge::ui::oled

namespace deluge::ui::shell {
struct UiEvent;
struct UiContext;
struct Result;
} // namespace deluge::ui::shell

namespace deluge::scenes::menu {

/// @brief What a `Node` is: which builder made it, and (with `Hint`) which widget renders it.
///
/// A `Kind` exists only if at least 3 items in E1's own tables use it -- spec E §4's rule, and
/// **Task 8 applied it**: with all eleven roots declared, `sim/scenes_spec/tables_3_spec.cpp`
/// counts the nodes of each kind across `nodes::kAllRoots` and fails below three.
///
/// Five enumerators did not survive that count and were removed rather than left as speculation:
///
///   * `Bipolar`, `MultiRange`, `Colour` -- zero nodes each, and no interpreter. The legacy items
///     that would have wanted them are all E2's bespoke ones (`menu_item::Bipolar`,
///     `multi_range.h`, `menu_item::Colour`), and every one of those is a `Kind::Custom`
///     placeholder in E1 because its state lives behind the `gui/` wall. (`Colour` stayed deleted
///     even after E2 task 11 made the six pad colours real: they are ordinary `Kind::Enum` nodes.)
///   * `Range` was deleted with them, on the same count, and **E2 task 11b brought it back** --
///     under the same rule, run the other way. Spec E §4's threshold is "a kind exists only if at
///     least 3 items use it", and there are now exactly three: `default_tempo`,
///     `default_swing_amount` (`IntegerRange`, `gui/ui/menus.cpp:1645-1648`) and `default_key`
///     (`KeyRange`, `:1650`). Three is the threshold, so the kind is warranted and no more nodes
///     were invented to pad it out: `MultiRange`'s picker is a different editor
///     (`nodes/multi_range.h`, a `CustomItem`) and is deliberately not counted here.
///   * `Action` -- zero nodes, and **still zero after E2 task 11b**, which is the task that
///     finally made those items real. Every legacy `MenuItem` whose select press *does* something
///     rather than editing a value reaches a UI this layer may not touch, so what they needed was
///     not a `Kind` but a way to *name* the action for someone else to run: `MenuAction` and
///     `MenuState::pending_action` (`scenes/menu/action.h`). A `Kind::Action` would have had to
///     carry the same intent through a `Binding` that has nowhere to put it, and would still not
///     have performed anything. `MenuScene::enter_child` keeps no action arm.
///
/// `SyncLevel` was confirmed by the same count (the delay, sidechain, reverb-sidechain and
/// arpeggiator syncs, the song swing interval, the default swing interval and the record
/// quantization -- well past three).
enum class Kind : uint8_t {
	Submenu,
	Toggle,
	Enum,
	Int,
	Decimal,
	SyncLevel,
	/// @brief A two-ended `{lower, upper}` editor -- `menu_item::Range` and its two subclasses
	///        (`gui/menu_item/range.h:30`, `integer_range.h:25`, `key_range.h:24`).
	///
	/// `Binding::get`/`set` are the **lower** edge; `Binding::range_spec` carries the upper edge's
	/// hooks and the two per-item rules (how the ends spell themselves, and what happens when they
	/// would cross). Rendered by `ui::oled::range_value`.
	Range,
	Custom,
};

/// @brief How a `Submenu`'s children lay out: a vertical list, or a horizontal strip of columns.
enum class Layout : uint8_t { Vertical, Horizontal };

/// @brief The horizontal-menu meter style a node's value draws with -- an alias, not a new type
///        (`ui/oled/meters.h:41`). `Enum`/`Toggle` are toolkit-only slot styles `columns` uses to
///        pick a slot renderer; every other enumerator maps 1:1 onto legacy's `Number::RenderingStyle`.
using Hint = deluge::ui::oled::Hint;

/// @brief An edited value's bounds and coarse step -- an alias, not a new type
///        (`ui/oled/values.h:18`).
using IntRange = deluge::ui::oled::Range;

/// @brief Which arithmetic converts a `ParamRef`'s stored `int32_t` to and from the menu units the
///        user sees, i.e. which of legacy's `readCurrentValue`/`getFinalValue` pairs a param item
///        used (`gui/menu_item/value_scaling.cpp`).
///
/// Legacy encoded this in the *class* of the menu item; as plain data it is one enumerator on the
/// `Binding`. Only a `ParamRef` binding is scaled at all -- a `get`/`set` hook owns its own units --
/// so only `param()` takes one.
///
///   * `Standard` -- INT32 range <-> 0..50. `patched_param::Integer` (`patched_param/integer.cpp:33,79`),
///     `menu_item::UnpatchedParam` (`unpatched_param.cpp:36,84`); the overwhelming majority.
///   * `HalfPrecision` -- 0..INT32_MAX <-> 0..50, for params that "aren't set up for negative
///     inputs" (`value_scaling.cpp:35-36`): `osc::PulseWidth` (`osc/pulse_width.h:32,34-37`) and
///     `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:29-33`).
///   * `Pan` -- INT32 range <-> -25..25, and a -25..25 range rather than 0..50:
///     `patched_param::Pan` (`patched_param/pan.cpp:28-34`, `pan.h:26-27`) and
///     `unpatched_param::Pan` (`unpatched_param/pan.cpp:29-36`, `pan.h:28-29`).
enum class Scaling : uint8_t { Standard, HalfPrecision, Pan };

/// @brief Which model parameter a `Binding` reads and writes through `ModelStack`, or an invalid
///        (unset) reference for a `Binding` driven entirely by `get`/`set` instead.
struct ParamRef {
	deluge::modulation::params::Kind kind = deluge::modulation::params::Kind::NONE;
	uint8_t index = 255;
	constexpr bool valid() const { return kind != deluge::modulation::params::Kind::NONE; }
};

/// @brief Forward declared only -- see the file-level note. Defined in `context.h` (a later task).
struct MenuContext;

using GetFn = int32_t (*)(const MenuContext&);
using SetFn = void (*)(const MenuContext&, int32_t);
using RangeFn = IntRange (*)(const MenuContext&);
using RelevantFn = bool (*)(const MenuContext&);
using DynamicNamesFn = size_t (*)(const MenuContext&, std::span<std::string_view> out);

struct Node;
using DynamicChildrenFn = size_t (*)(const MenuContext&, std::span<const Node*> out);

/// @brief The largest window `dynamic_children`/`dynamic_names` may fill on one call, and the
///        largest node-id the registry (`node_id.h`) will assign.
inline constexpr size_t kMaxDynamicChildren = 64;
inline constexpr size_t kMaxDynamicNames = 64;
inline constexpr size_t kMaxNodes = 1024;

/// @brief How a node reads, writes, and (for `Enum`) names its value.
///
/// Normally exactly one of `param` or `get`+`set` is set; `names`/`dynamic_names` only matter for
/// `Kind::Enum`. `names` holds l10n string ids (`uint16_t`), not `l10n::String`, so this header
/// stays free of `gui/l10n`.
///
/// @note **Precedence when both are set: `get`/`set` wins, and `param` is then a *learn* target
///       only.** Legacy has four items that are simultaneously a `Decimal` with their own
///       read/write and a `PatchedParam` with a real parameter id -- `menu_item::Transpose`
///       (`gui/menu_item/transpose.h:26`), which the two oscillator and two FM-modulator transpose
///       menus derive from. Their value does not live in the param (it is
///       `sampleHolder.transpose`/`cents`, or `Sound::modulatorTranspose`/`Cents`), so
///       `bindings::read`/`write` must take the hooks; but `getParamIndex()` (`transpose.h:37`) and
///       `getLearningThing()` -> `setToHaveParamOnly(getP())` (`source/transpose.h:28-32`) are real,
///       so MIDI/CC learn must still bind to `LOCAL_OSC_A/B_PITCH_ADJUST` and
///       `LOCAL_MODULATOR_0/1_PITCH_ADJUST`. Declaring both on the `Binding` is how that survives:
///       `bindings::read`/`write` test `get`/`set` first, `bindings::learn_target` reads `param`,
///       and `bindings::range` already prefers `fixed_range` over the param's default.
///
/// @note **`range` vs `fixed_range`.** Spec E §4 gives `range` as a function of `MenuContext` --
///       genuinely dynamic bounds (e.g. a rate whose ceiling depends on sync mode) -- documented
///       there as "optional; default from ParamRef" for a bound item. That leaves no field for the
///       common case the brief's own `int_item`/`decimal_item` builders take a *literal* `IntRange`
///       argument: a plain function pointer cannot close over an arbitrary runtime value, so a
///       literal range cannot be smuggled through `range` without one hand-written trampoline
///       function per distinct range (impractical, and not what those builders' signatures ask
///       for). `fixed_range` is the addition that resolves this: it holds the literal bounds those
///       two builders are given, engaged only when `range == nullptr`. Task 6+'s interpreter reads
///       in this order: `range(ctx)` if set, else `fixed_range` if engaged, else derive from
///       `param` (model code, out of reach here).
/// @brief The half of a `Kind::Range` binding that a `Kind::Int` one has no room for.
///
/// A `Binding` names one value through `get`/`set`; a range is two. Rather than grow four more
/// fields on a struct every node pays for, the three range nodes point at one of these -- the same
/// trade `Node::chain_targets`' span makes for the seven chaining nodes.
///
/// `get`/`set` on the `Binding` itself stay the **lower** edge, so `bindings::read`/`write` and
/// `validate_tree`'s "a value node has something to read" rule keep working on a range node without
/// a special case.
struct RangeSpec {
	/// @brief The upper edge. `IntegerRange::upper` / `KeyRange::upper`, which are now references
	///        onto `FlashStorage::defaultRanges` (`storage/flash_storage.h`).
	GetFn get_upper = nullptr;
	SetFn set_upper = nullptr;

	/// @brief How one end spells itself -- `Range::getText`'s per-subclass half
	///        (`integer_range.cpp:70-95`, `key_range.cpp:60-100`).
	deluge::ui::oled::RangeFormatFn format = nullptr;

	/// @brief Which of legacy's two crossing rules this item uses (`ui/oled/values.h`'s
	///        `RangeClamp`): `Push` for `IntegerRange`, `Bound` for `KeyRange`.
	deluge::ui::oled::RangeClamp clamp = deluge::ui::oled::RangeClamp::Push;

	/// @brief Does opening the item land on the LEFT edge when the two ends differ?
	///
	/// `IntegerRange::beginSession` (`integer_range.cpp:26-31`) does exactly that;
	/// `KeyRange` has no `beginSession` override at all, so it takes `Range::beginSession`'s bare
	/// `editingRangeEdge = RangeEdit::OFF` (`range.cpp:30-33`). One `bool` rather than a second
	/// hook, because that is the whole of the difference.
	bool focus_left_when_split = false;
};

struct Binding {
	ParamRef param{};
	GetFn get = nullptr;
	SetFn set = nullptr;
	RangeFn range = nullptr;
	std::optional<IntRange> fixed_range{};
	std::span<const uint16_t> names{};
	DynamicNamesFn dynamic_names = nullptr;
	deluge::ui::oled::ValueStyle style{};
	uint8_t decimal_places = 0;

	/// @brief `Kind::Range` only: the upper edge and the two per-item rules. Null for every other
	///        kind, and `validate_tree` reports a `Kind::Range` node without one.
	const RangeSpec* range_spec = nullptr;

	/// @brief Which value scaling `param` is read and written through (`Scaling` above). Meaningful
	///        only when `param.valid()`; ignored for a `get`/`set` binding.
	Scaling scaling = Scaling::Standard;

	/// @brief Does committing this value oblige the audio engine to recompute its reverb
	///        parameters before the next render?
	///
	/// The port of `unpatched_param::UpdatingReverbParams::writeCurrentValue`
	/// (`gui/menu_item/unpatched_param/updating_reverb_params.h:26-29`): an ordinary
	/// `UnpatchedParam` write, then `AudioEngine::mustUpdateReverbParamsBeforeNextRender = true`.
	/// It is a flag on the `Binding` rather than a `set` hook because a `set` hook would *replace*
	/// the param write (`bindings::write` prefers hooks), and because two legacy items share the
	/// behaviour and neither can express it any other way: `sidechain::Shape`
	/// (`gui/menu_item/sidechain/shape.h:6`, this batch's `sidechain_shape`) and
	/// `sidechain::GlobalVolume` (`sidechain/volume_global.h:6`, the song and global-FX roots task
	/// 8 declares). Meaningful only for a `ParamRef` binding -- a `get`/`set` item that needs the
	/// flag sets it in its own hook, as the sidechain attack/release/sync ones do.
	bool updates_reverb_params = false;
};

/// @brief Forward declared only -- see the file-level note. Defined alongside the interpreter's own
///        per-instance state (a later task).
struct MenuState;

/// @brief Which menu a select press on this node opens *instead of* going up a level -- the port of
///        legacy's `MenuItem* MenuItem::selectButtonPress()` (`gui/menu_item/menu_item.h`).
///
/// Returning `nullptr` means "ascend", which is exactly the base class's `return nullptr` arm and
/// exactly what `SoundEditor` reads as `goUpOneLevel()` (`gui/ui/sound_editor.cpp:395-428`).
///
/// It takes the context and the interpreter's state because the answer is value-dependent for two
/// of the ported overrides: `arpeggiator::PresetMode` opens the custom octave-mode page only on
/// `ArpPreset::CUSTOM`, and on the drum twin of it when a kit row is being edited
/// (`arpeggiator/preset_mode.h:109-118`); `voice::PolyphonyType` opens the voice count only in
/// `PolyphonyMode::POLY` (`voice/polyphony.h:139-143`). The hook reads the *committed* value
/// through `bindings::read`, not `MenuState`'s in-flight edit copy, because legacy's own overrides
/// call `getValue()` after `writeCurrentValue()` has already run.
using SelectChainFn = const Node* (*)(const MenuContext&, const MenuState&);

/// @brief The escape hatch (spec E §4: at most 10 across the whole tree) for a bespoke item that
///        doesn't fit any `Kind`: the multi-range editor, patch cables, the file selector, and the
///        handful of others spec E §4 names.
struct CustomItem {
	virtual ~CustomItem() = default;
	virtual void render(deluge::ui::oled::OledFrame&, const MenuContext&, MenuState&) const = 0;
	virtual deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&,
	                                         const MenuContext&, MenuState&) const = 0;

	/// @par The focus hooks: three names, two questions
	///
	/// **`on_focus`, `on_focus_backward` and `MenuScene::FocusCause` look like three spellings of one
	/// thing, and a contributor has to learn all of them. They are not one thing** (E3 final review,
	/// A7). What each answers, and when it fires:
	///
	/// * **Which direction did focus arrive from?** -- answered by *two virtuals*: `on_focus`
	///   (forward) and `on_focus_backward` (returning from a page this item itself opened). Exactly
	///   one of the two fires per focus transition; `MenuScene::ascend` runs the backward one
	///   *instead of* the forward one.
	/// * **Was there a select press on this node's own row?** -- answered by *one discriminator*,
	///   `MenuScene::FocusCause`, scoped per item by `on_focus_is_press()`. It is consulted before
	///   `on_focus`, and decides whether it runs at all.
	///
	/// **Why they are not one mechanism**, in each direction:
	///
	/// * Direction is *two virtuals* rather than one virtual plus a cause because the two arms share
	///   no code in legacy either: `beginSession(navigatedBackwardFrom)` is a body with two disjoint
	///   halves, and every override that has both writes them separately. Two hooks let an item
	///   implement only the arm it has, and the do-nothing default is the right answer for the rest.
	///   One hook plus a cause would make every such item open with a branch on an argument.
	/// * The press question is *a discriminator* rather than a third virtual because no item wants a
	///   **different body** for a shortcut jump or a restore -- it wants the body it already has
	///   **suppressed**. A virtual would force every press-shaped item to write an empty second
	///   override, and the empty one is exactly what a later contributor would fill in by mistake.
	///
	/// Rule of thumb: override a hook when the item **does something different**; consult the cause
	/// when the scene must decide whether the item should act **at all**.

	/// @brief Called when this item becomes the focused node (`MenuScene::enter_leaf`, on `open()`,
	///        descending into a child, and restoring saved state into a leaf) -- mirroring legacy's
	///        `MenuItem::beginSession` (`gui/menu_item/menu_item.h`), which every stateful bespoke
	///        item overrides to (re-)initialise itself before its first render of a visit.
	///
	/// Do-nothing default: most items have no state to initialise (`render`'s own arguments are
	/// enough). Fires once per focus transition, not once per render -- `render_oled`'s `Kind::Custom`
	/// arm calls only `render`, every tick; this is the one hook a `CustomItem` gets for "I just
	/// became the screen", the same distinction `beginSession` draws from `drawPixelsForOled` in
	/// legacy. Named examples this task's foundation exists for: `battery::Level::beginSession`
	/// resets its charging-heuristic baseline (`battery/level.h:43-51`, and see
	/// `nodes::BatteryLevelItem::on_focus`); `midi::Command::beginSession` (twelve items) and
	/// `midi::Devices::beginSession` (which seeds the initially-selected device) are E2's next
	/// callers.
	virtual void on_focus(const MenuContext&, MenuState&) const {}

	/// @brief Called when this item becomes the focused node again because a page it opened was
	///        left -- legacy's `beginSession(navigatedBackwardFrom != nullptr)` arm.
	///
	/// `on_focus` above is the **forward** arm only: `MenuScene::ascend` runs this instead of it, so
	/// the two never both fire for one transition. That is legacy's own shape --
	/// `SoundEditor::goUpOneLevel` ends in `beginScreen(oldItem)` with a non-null `oldItem`
	/// (`gui/ui/sound_editor.cpp:710-728`), where every other entry passes `nullptr`
	/// (`:263`, `:292`) -- expressed as two hooks rather than one hook and a discriminator, because
	/// the two arms of every legacy override that has both share no code at all.
	///
	/// Do-nothing default, and that is the *right* answer for almost every item: an item whose
	/// legacy backward arm restores a cursor gets it for free, because the cursor lives in
	/// `MenuState::list[depth]` and `ascend` deliberately leaves that alone. `PatchCablesItem`
	/// (legacy's `currentValue = savedVal`, `patch_cables.cpp:16-26`) and `SourceSelectionItem`
	/// (legacy's "walk back to the source `s`", `source_selection.cpp:110-116`) are both exactly
	/// that case and both stay on the default; each says so at its own `on_focus`.
	///
	/// **E3 task 10 added this hook because "leave `MenuState` alone" is not always the backward
	/// arm.** Until then `ascend` skipped `Kind::Custom` outright and the skip *was* the stand-in --
	/// recorded at that site as sound only while every chain-source custom item's backward behaviour
	/// happened to be "keep what `MenuState` already holds", with nothing testing it. It is not:
	/// `gate::Selection`'s backward arm is `setValue(soundEditor.currentSourceIndex)`
	/// (`gui/menu_item/gate/selection.h:33-41`) and its `selectButtonPress` does **not** write
	/// `currentSourceIndex` on the minimum-off-time row (`:45-47`), so legacy puts the cursor back on
	/// the last *output* the user chose and "keep the cursor" puts it back on MINIMUM OFF TIME. See
	/// `nodes::GateSelectionItem::on_focus_backward`.
	virtual void on_focus_backward(const MenuContext&, MenuState&) const {}

	/// @brief Commit whatever this item has been editing -- `MenuItem::writeCurrentValue`, for a
	///        bespoke item whose value does not live in a `Binding`.
	///
	/// The mirror of `on_focus`: `MenuScene::commit_value`'s `Kind::Custom` arm calls it, which is
	/// reached from `after_render` when the item set `MenuState::value_changed` during its render.
	/// That indirection is the point -- a `CustomItem::render` may not write the model
	/// (`menu_scene.h`: "never mutates the model from `render_oled`"), so an item that edits a value
	/// records the change and this hook performs it, exactly where every other kind's write happens.
	///
	/// Do-nothing default: only the three patch-cable-strength pages have a value with no `Binding`
	/// behind it (`nodes/patch_cables.h`).
	virtual void commit(const MenuContext&, MenuState&) const {}

	/// @brief The icon this item's row draws in its **parent's** list -- legacy's
	///        `MenuItem::renderSubmenuItemTypeForOled` (`gui/menu_item/menu_item.cpp:69-75`), which
	///        `Submenu::drawSubmenuItemsForOled` calls once per visible row
	///        (`gui/menu_item/submenu.cpp:117`).
	///
	/// **The submenu arrow is the default because it is legacy's default**: the base
	/// `MenuItem::renderSubmenuItemTypeForOled` blits `submenuArrowIcon` unconditionally, and only
	/// three overrides in the tree replace it -- `Toggle` and `Selection::isToggle()` with a
	/// checkbox (`gui/menu_item/toggle.cpp:50-57`, `gui/menu_item/selection.cpp:38-56`), and
	/// `midi::device_definition::Linked`/`HideUnlabeled` with a checkbox of their own
	/// (`gui/menu_item/midi/device_definition/linked.h:56`, `hide_unlabeled.h:48`). A bespoke item
	/// whose legacy class overrides none of those therefore draws the arrow, whatever it is.
	///
	/// This hook is what `src/scenes/README.md` recorded as owed when it wrote "a `Kind::Custom`
	/// node cannot draw a row icon in its parent's list … closing this needs a `CustomItem` hook
	/// `row_icon` can ask". `MenuScene`'s `row_icon` asks it for every `Kind::Custom` node
	/// (`menu_scene.cpp`).
	///
	/// @param ctx The menu context, for an item whose icon is value-dependent (the checkbox arms
	///            above are: `MidiDeviceLinkedItem` reads whether a definition file is linked).
	/// @return The icon, or `nullptr` for a row that draws none. Returning `nullptr` also gives the
	///         label the full row width, exactly as legacy's `endX` does
	///         (`gui/menu_item/submenu.cpp:109`: `OLED_MAIN_WIDTH_PIXELS -
	///         getSubmenuItemTypeRenderLength()`), so it is not merely "the icon is hidden".
	virtual const deluge::gfx::oled::Icon* row_icon(const MenuContext&) const {
		return &deluge::gfx::oled::icons::submenuArrow;
	}

	/// @brief Is this a stand-in for an item E2 still owes, rather than the real thing?
	///
	/// E1's tables have to name every legacy bespoke item so the tree's *shape* is complete, but the
	/// implementations of the file selector, the audio recorder, the loop points, the name editors,
	/// the patch-cable pages and the multi-range picker all belonged to E2 and E3. Such a node
	/// points at a shared placeholder (`nodes::kPending`) that draws the item's title and consumes
	/// nothing.
	///
	/// A null `custom` would be worse -- nothing would render, and `validate.h`'s `NullCustom` rule
	/// exists precisely to catch that -- but a placeholder is still not a finished item, so
	/// `validate_tree` reports one (`Issue::Kind::PlaceholderCustom`).
	///
	/// @note **No node in the shipped tree answers true any more** (E3 task 7,
	///       `validate.h`'s `kPendingCustomNodes`), and the option that used to suppress the rule
	///       is gone with them. This hook and `nodes::kPending` stay: they are what a *future*
	///       table declaring a shape before its item exists reaches for, and the rule now reports
	///       such a node with nothing able to turn it off.
	virtual bool is_placeholder() const { return false; }

	/// @brief Does this item's `on_focus` stand in for a **press** rather than for `beginSession`?
	///
	/// `on_focus` is `beginSession`, and `MenuScene` runs it on every focus transition -- a descent
	/// from the parent list, a `SceneId` deep link, the shortcut-pad jump, and `restore_state`
	/// rebuilding a saved path. Legacy runs `beginSession` on all of those too
	/// (`SoundEditor::focusRegained` -> `beginScreen`, `gui/ui/sound_editor.cpp:330`, `:1053`), so
	/// for an item that *initialises* itself there the port matches and this stays false.
	///
	/// **It must be true for an item whose `on_focus` is really `selectButtonPress`.** Legacy has
	/// items whose entire body is a press handler and which `SoundEditor` therefore never navigates
	/// into (`shouldEnterSubmenu() == false`, `gui/ui/sound_editor.cpp:420-423`); the port has no
	/// equivalent of that flag, so it descends for one tick and acts from `on_focus`
	/// (`nodes/actions.h`). That works for a press on this item's own row and is wrong for every
	/// other way in: a restore, a deep link **or a shortcut-pad jump** would run the press with no
	/// press on that row behind it -- the jump is a press, but on a grid pad. Legacy guards the same
	/// case explicitly -- `Toggle::selectButtonPress` returns without toggling when the item is
	/// already the current one (`gui/menu_item/toggle.h:35-37`), which is precisely "we got here
	/// without a press on our row". With this true, `MenuScene::enter_leaf` runs `on_focus` only for
	/// `FocusCause::RowPress`, whose two values are named for exactly this question.
	///
	/// @note The line is **"does it act without a press"**, not "does it do anything". The items
	///       that open a scene -- `EditNameItem`, `ContextMenuItem`, the nine in `nodes/sample.h` --
	///       keep this false on purpose: their legacy classes do their work in `beginSession` and
	///       legacy re-fires them on a re-entry too, which is the shipped ruling
	///       `MenuScene::restore_state` argues at its own site and `rename_scene_spec.cpp` pins.
	///       Opening a scene the user can back out of is not the same as writing the model or
	///       firing a one-shot verb.
	virtual bool on_focus_is_press() const { return false; }

	/// @name MIDI-learn hooks -- the port of `MenuItem`'s own learn overrides.
	///
	/// Legacy declares these on `MenuItem` itself (`gui/menu_item/menu_item.h:194-199`:
	/// `allowsLearnMode`, `shouldBlinkLearnLed`, `learnNoteOn`, `learnProgramChange`, `learnCC`,
	/// `unlearnAction`), all with do-nothing/false defaults, and `SoundEditor` calls them on
	/// `getCurrentMenuItem()` without asking what class it is
	/// (`gui/ui/sound_editor.cpp:516`, `:1550-1570`). The port keeps that shape but puts the hooks
	/// on `CustomItem` rather than on `Node`: the only ported items that learn anything are
	/// bespoke ones (the twelve `nodes::MidiCommandItem`s), and a `Kind`'s interpreter has no
	/// business growing a learn arm none of its nodes use.
	///
	/// Every default here is "did nothing, used nothing", so no other `CustomItem` changes.
	///
	/// @note **Two of the three are routed now** (F1 task 10):
	///       `ShellHostUI::noteOnReceivedForMidiLearn` and `pcReceivedForMidiLearn` override the two
	///       `UI` virtuals legacy's dispatchers ask (`gui/ui/ui.h:130`, `:132`) and forward here
	///       through `MenuScene::learn_note_on` / `learn_program_change`. **`learn_cc` still has
	///       none**, because `PlaybackHandler::midiCCReceived` reaches learn through a hardcoded
	///       `getCurrentUI() == &soundEditor` (`playback/playback_handler.cpp:3164`) with no virtual
	///       to override; and all three are inert until something under the guest sets
	///       `UI_MODE_MIDI_LEARN` -- see `gui/ui/shell_host_ui.h`'s `@par MIDI learn`.
	/// @{

	/// @brief Bind a note-on to whatever this item learns.
	/// @return Whether the message was used (`MenuItem::learnNoteOn`'s own `bool`).
	virtual bool learn_note_on(::MIDICable& /*cable*/, int32_t /*channel*/, int32_t /*note*/) const { return false; }

	/// @brief Bind a CC to whatever this item learns.
	///
	/// Returns a `bool` where `MenuItem::learnCC` returns `void`, because legacy's caller
	/// (`SoundEditor::midiCCReceived`) answers "was some use made of the message here" for the whole
	/// UI and could only ever say `true`; here the focused item answers for itself.
	virtual bool learn_cc(::MIDICable& /*cable*/, int32_t /*channel*/, int32_t /*cc*/, int32_t /*value*/) const {
		return false;
	}

	/// @brief Bind a program change to whatever this item learns.
	/// @return Whether the message was used -- including "used, and refused with a popup", which is
	///         what `SoundEditor::pcReceivedForMidiLearn` reports for legacy's one refusing item.
	virtual bool learn_program_change(::MIDICable& /*cable*/, int32_t /*channel*/, int32_t /*program*/) const {
		return false;
	}

	/// @brief Forget what this item learned -- `MenuItem::unlearnAction` (`sound_editor.cpp:516`).
	virtual void unlearn() const {}

	/// @brief Does this item want the LEARN LED blinking while it is focused?
	///        `MenuItem::shouldBlinkLearnLed` (`sound_editor.cpp:949-954`, `:488-492`).
	virtual bool wants_learn_blink() const { return false; }

	/// @brief Which automatable parameter a CC would bind to on this item -- `getLearningThing()`
	///        for a bespoke item whose answer is not a `Binding::param`.
	///
	/// The *other* learn path (see `MenuScene::learn_target`'s note): `bindings::learn_target` reads
	/// `Node::bind.param`, which is enough for every `PatchedParam`/`UnpatchedParam` node, but the
	/// three patch-cable-strength pages have no `ParamRef` at all -- their descriptor is assembled
	/// at read time from `MenuState`'s `patching_param` / `patch_source` / `patch_range_source`
	/// (`patch_cable_strength/regular.cpp:30-34`, `range.cpp:25-30`). Hence the two arguments:
	/// unlike every other hook here, the answer is a function of where the user navigated, not of
	/// the item alone.
	///
	/// A null descriptor (the default) means "this item has nothing to learn", which is what
	/// `MenuScene::learn_target` falls back from to `bindings::learn_target`. Spelled with
	/// `setToNull()` rather than `{}` on purpose: a default-constructed `ParamDescriptor` has
	/// `data == 0` and `isNull()` is false for it (`modulation/params/param_descriptor.h:106-110`),
	/// so `{}` would read as "param 0 from source 0" -- the same spelling `bindings::learn_target`
	/// uses for the same reason.
	virtual ParamDescriptor learn_descriptor(const MenuContext&, const MenuState&) const {
		ParamDescriptor descriptor;
		descriptor.setToNull();
		return descriptor;
	}
	/// @}
};

/// @brief One menu item or submenu, as plain data.
///
/// `children` is only ever populated by `submenu()` -- a `dynamic_children`/`dynamic_names` node
/// has no *static* children, so `assign_node_ids` (which walks `children` and `chain_targets`)
/// never reaches, and can never address by id, whatever those hooks produce at render time (spec
/// E §4's dynamic submenus are resolved then, not ahead of time).
struct Node {
	uint16_t name = 0;
	uint16_t title = 0;
	Kind kind = Kind::Submenu;
	Hint hint = Hint::None;
	Layout layout = Layout::Vertical;
	Binding bind{};
	std::span<const Node* const> children{};
	RelevantFn relevant = nullptr;
	DynamicChildrenFn dynamic_children = nullptr;
	const CustomItem* custom = nullptr;

	/// @brief The select-press chain hook (`SelectChainFn`), or null for "a select press ascends".
	SelectChainFn select_chain = nullptr;

	/// @brief Every node `select_chain` is allowed to return, as static data.
	///
	/// **Why a span beside the hook, rather than the hook alone.** `select_chain` is a function of a
	/// live `MenuContext`, so nothing that walks the tree ahead of time can call it -- and the tree
	/// *is* walked ahead of time, three times: `assign_node_ids` numbers it, `validate_tree` checks
	/// it, and `deluge_menu_dump` prints it. Legacy has four pages that no submenu literal names at
	/// all and that a chain is the *only* way to reach (`arpOctaveModeToNoteModeMenu` and its three
	/// relatives, `menus.cpp:511-521`), plus three more under the settings root (`cvSubmenu`,
	/// `gateModeMenu`, `gateOffTimeMenu`). Without a static spelling of "these are the possible
	/// answers" every one of those would be an unnumbered, unvalidated, undumped node -- so
	/// `SceneId::arg` could not name a chained page and `MenuScene::on_enter` could not restore one.
	///
	/// Set it and `select_chain` together (`with_select_chain`); `validate_tree` reports
	/// `Flag::ChainTargetsMismatch` if only one of the two is present. Listing a node here that the
	/// hook never returns is harmless but pointless -- it makes the page reachable and numbered
	/// while nothing can navigate to it.
	std::span<const Node* const> chain_targets{};

	uint8_t slots = 1;

	/// @brief Does this item edit something that belongs to one `MultiRange` rather than to the
	///        `Sound` as a whole? The port of `MenuItem::isRangeDependent()`
	///        (`gui/menu_item/menu_item.h:202`, "only relevant to some note ranges").
	///
	/// This, not "has a `bind.param`", is what makes spec E §4.1's permission rule fire
	/// (`menu::permission_for`). The distinction matters: an ordinary patched param such as LPF
	/// frequency lives on the `Sound` and is editable on a multisampled synth without picking a
	/// range first, while a range-dependent item's value lives in the range itself
	/// (`sample/transpose.h:139-165` writes `sampleHolder.transpose` / `source.transpose`) and has
	/// nowhere to go until one is chosen.
	///
	/// The legacy items that override `isRangeDependent()` to true, and so are the ones that set
	/// this when Tasks 6-8 declare them: `sample/transpose.h:137`, `sample/loop_point.h:31`, and
	/// `multi_range.h:32` (the picker itself). `file_selector` and `osc/audio_recorder` reach the
	/// same rule through `checkPermissionToBeginSessionForRangeSpecificParam` without the override,
	/// and set this too.
	bool range_dependent = false;
};

/// @brief `n` with `range_dependent` set -- a `constexpr` post-hoc modifier rather than a trailing
///        argument on all nine builders.
///
/// Only a handful of nodes in the whole tree are range-dependent (the five named on
/// `Node::range_dependent`), so paying for the flag in every builder signature would be the wrong
/// trade: `with_range_dependent(param(...))` reads as the exception it is, and no existing call site or
/// `static_assert` changes.
constexpr Node with_range_dependent(Node n) {
	n.range_dependent = true;
	return n;
}

/// @brief `n` with a select-press chain: @p fn decides, @p targets is every node @p fn may name.
///
/// A post-hoc modifier for the same reason `with_range_dependent` is one -- seven nodes in the
/// whole tree chain, so the other nine builders keep their signatures. Both arguments are required
/// because the walkers need @p targets and the interpreter needs @p fn; see `Node::chain_targets`.
constexpr Node with_select_chain(Node n, SelectChainFn fn, std::span<const Node* const> targets) {
	n.select_chain = fn;
	n.chain_targets = targets;
	return n;
}

// ---------------------------------------------------------------------------------------------
// Builders. All `constexpr`; `title` defaults to `name` (0 means "use name").

/// @param scaling Which value scaling this param uses (`Scaling`). The default matches legacy's
///                `patched_param::Integer` / `menu_item::UnpatchedParam`; the two exceptions
///                (`Scaling::HalfPrecision` for pulse width and the compressor params,
///                `Scaling::Pan` for the two pan params) name themselves at the call site.
constexpr Node param(uint16_t name, ParamRef p, Hint h = Hint::Knob, RelevantFn rel = nullptr, uint16_t title = 0,
                     Scaling scaling = Scaling::Standard) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::Int;
	n.hint = h;
	n.bind.param = p;
	n.bind.scaling = scaling;
	n.relevant = rel;
	return n;
}

constexpr Node int_item(uint16_t name, GetFn get, SetFn set, IntRange r, Hint h = Hint::None,
                        RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Int;
	n.hint = h;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.relevant = rel;
	return n;
}

constexpr Node decimal_item(uint16_t name, GetFn get, SetFn set, IntRange r, uint8_t places, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Decimal;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.bind.decimal_places = places;
	n.relevant = rel;
	return n;
}

constexpr Node toggle_item(uint16_t name, GetFn get, SetFn set, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Toggle;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node enumeration(uint16_t name, std::span<const uint16_t> names, GetFn get, SetFn set,
                           RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Enum;
	n.bind.names = names;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node dynamic_enumeration(uint16_t name, DynamicNamesFn names, GetFn get, SetFn set,
                                   RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Enum;
	n.bind.dynamic_names = names;
	n.bind.get = get;
	n.bind.set = set;
	n.relevant = rel;
	return n;
}

constexpr Node submenu(uint16_t name, std::span<const Node* const> children, Layout layout = Layout::Vertical,
                       RelevantFn rel = nullptr, uint16_t title = 0) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::Submenu;
	n.layout = layout;
	n.children = children;
	n.relevant = rel;
	return n;
}

constexpr Node dynamic_submenu(uint16_t name, DynamicChildrenFn children, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Submenu;
	n.dynamic_children = children;
	n.relevant = rel;
	return n;
}

/// @brief A `Kind::SyncLevel` leaf -- legacy's `menu_item::SyncLevel` (`gui/menu_item/sync_level.h:27`).
///
/// The value is legacy's own menu-option index, `SyncLevel::syncTypeAndLevelToMenuOption`
/// (`gui/menu_item/sync_level.cpp:79-81`), so a hook pair is always what reads and writes it -- no
/// `ParamRef` spelling exists for a `{syncType, syncLevel}` pair.
///
/// @param r The option range. Normally `{0, NUM_SYNC_VALUES - 1, 1}` (`sync_level.h:31` over
///          `model/sync.h:37`), but `sidechain::Sync` overrides `size()` to 10
///          (`gui/menu_item/sidechain/sync.h:28`), so the bounds are an argument rather than baked
///          in -- and baking them in would drag `model/sync.h` into this deliberately model-light
///          header.
constexpr Node sync_level_item(uint16_t name, uint16_t title, GetFn get, SetFn set, IntRange r,
                               RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::SyncLevel;
	n.bind.get = get;
	n.bind.set = set;
	n.bind.fixed_range = r;
	n.relevant = rel;
	return n;
}

/// @brief A `Kind::Range` leaf -- `menu_item::Range` (`gui/menu_item/range.h:30`).
///
/// @param get_lower/@param set_lower The lower edge, which is also what `bindings::read`/`write`
///                                   see (`RangeSpec`).
/// @param spec  The upper edge and the two per-item rules. Required: a null one is a
///              `validate_tree` issue, not a defaulted-to-something.
/// @param r     The bounds both ends live inside, and the coarse knob's step --
///              `IntegerRange::minValue`/`maxValue` (`integer_range.h:45`), or `KeyRange`'s own
///              `KEY_MIN`/`KEY_MAX` locals (`key_range.cpp:29-30`).
constexpr Node range_item(uint16_t name, uint16_t title, GetFn get_lower, SetFn set_lower, const RangeSpec* spec,
                          IntRange r, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = title != 0 ? title : name;
	n.kind = Kind::Range;
	n.bind.get = get_lower;
	n.bind.set = set_lower;
	n.bind.fixed_range = r;
	n.bind.range_spec = spec;
	n.relevant = rel;
	return n;
}

constexpr Node custom(uint16_t name, const CustomItem* item, RelevantFn rel = nullptr) {
	Node n{};
	n.name = name;
	n.title = name;
	n.kind = Kind::Custom;
	n.custom = item;
	n.relevant = rel;
	return n;
}

} // namespace deluge::scenes::menu
