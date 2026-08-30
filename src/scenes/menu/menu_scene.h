#pragma once

/// @brief `MenuScene` -- one shell `Scene` that interprets any `Node` tree (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-5-brief.md`).
///
/// There is exactly one of these for the whole menu system. It renders whatever `Node` is at the
/// top of its own path through the toolkit widgets, consumes input the toolkit way (a widget
/// claims what it owns while it is being drawn), and commits every edit through
/// `menu::bindings`. What legacy spread across `SoundEditor` plus ~200 `MenuItem` subclasses is
/// here: the data is `node.h`'s, the model access is `bindings.h`'s, and this file is the
/// interpreter between them.
///
/// **Ports.** `MenuItem::drawItemsForOled` (`gui/menu_item/menu_item.cpp:47`),
/// `Submenu::drawSubmenuItemsForOled` / `focusChild` (`gui/menu_item/submenu.cpp:99`, `:21`),
/// `HorizontalMenu::renderOLED` / `renderMenuItems` / `switchVisiblePage` / `selectMenuItem` /
/// `preparePaging` (`gui/menu_item/horizontal_menu.cpp:109,203,345,444,477`) -- all four already
/// pinned by the `list` / `columns` widgets, which this scene composes rather than re-implements;
/// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp:1034`), `buttonAction` (`:333`,
/// back -> `goUpOneLevel` `:672`, which exits at depth 0 via `exitCompletely` `:678`),
/// `horizontalEncoderAction` (`:1008`), `padAction` (`:1405`) with `potentialShortcutPadAction`
/// (`:1111`), `enterSubmenu` (`:324`), `setupShortcutBlink` (`:959`) / `blinkShortcut`
/// (`:974-1000`), `getGreyoutColsAndRows` (`:214`), and the `MultiRange` redirect -> spec E
/// §4.1's `Permission::MustSelectRange`.
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::oled` widget, so
/// the layout rules live in one place and Task 12's ratchet can count primitive calls at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "notify/dirty_set.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::menu {

/// @brief How deep a menu path may go, and how many `ListState`s the scene therefore keeps.
///
/// Unrelated to `ui::shell::kStackDepth` (`shell/scene.h:48`), which counts *scenes*: the whole
/// menu is one scene however deep the user walks into it. Eight is legacy's own
/// `menuItemNavigationRecord` depth (`gui/ui/sound_editor.h`).
inline constexpr uint8_t kMenuDepth = 8;

/// @brief The most children one menu level materialises in a frame.
///
/// Matches `kMaxDynamicChildren` (`node.h`), so a `dynamic_children` hook that fills its whole
/// window is never truncated by the renderer. The row array this sizes is the scene's one large
/// stack allocation (~4 KB in `render_vertical`); it is a frame local rather than a member so it
/// costs nothing between ticks.
inline constexpr size_t kMaxMenuChildren = kMaxDynamicChildren;

/// @brief The most columns one horizontal page materialises. Legacy pages four slots at a time
///        and a `Column` may claim several, so sixteen is well past any real page
///        (`horizontal_menu.cpp:477`'s `preparePaging`).
inline constexpr size_t kMaxMenuColumns = 16;

/// @brief The two phases of `render_grid`'s shortcut-blink cycle, in milliseconds.
///
/// **Not two independent blinks, and not one symmetric period.** Legacy runs *one* alternating
/// cycle off `shortcutBlinkCounter` (`sound_editor.cpp:974-1000`): `counter & 1` selects the phase,
/// the param phase flashes the focused item's pad and re-arms `TimerName::SHORTCUT_BLINK` at
/// **180 ms** (`:981-986`), and the source phase flashes the patching-source pads and re-arms at
/// **20 ms** (`:989-999`). So the cycle is `kSourceBlinkMs + kShortcutBlinkMs` = 200 ms, of which
/// the source phase is the short leading slice and the param phase is the rest. The two pads are
/// deliberately never lit at the same instant -- legacy's own comment at `:971` says why ("blinking
/// two pads on the same row at same time doesn't work").
///
/// One cycle needs one anchor, not two `Blink`s: `MenuState::blink_anchor_ms` is it, and
/// `blink_phase` / `ms_to_phase_change` (`menu_scene.cpp`) are what read it.
/// @{
inline constexpr uint16_t kShortcutBlinkMs = 180;
inline constexpr uint16_t kSourceBlinkMs = 20;
inline constexpr uint32_t kBlinkCycleMs = uint32_t{kSourceBlinkMs} + kShortcutBlinkMs;
/// @}

/// @brief Live interpreter state: the path, the widgets' own POD state, and what this tick's
///        render observed for `after_render()` to act on.
///
/// **Not** what `save_state` writes -- see `MenuSavedState`. This is a member of `MenuScene` and
/// is handed to a `CustomItem` by reference, which is why `node.h` forward declares it.
struct MenuState {
	/// The menu path, root first. `path[depth]` is what is on screen.
	const Node* path[kMenuDepth] = {};
	uint8_t depth = 0;

	/// One per menu depth, so walking back up restores the level's own cursor -- legacy's
	/// `Submenu::current_item_` being a member of each submenu (`submenu.cpp:21`).
	///
	/// @note `list[depth].editing` IS the editing flag (`ui/core/state.h:15`): a reserved bool no
	///       widget reads, which is exactly what a scene-owned "the leaf is being edited" bit
	///       wants to be.
	deluge::ui::ListState list[kMenuDepth]{};
	/// One per menu depth, like `list`: a horizontal page nested under another must not lose the
	/// parent's column focus when the child is entered and left again.
	deluge::ui::ColumnsState columns[kMenuDepth]{};
	/// One per menu depth, for the same reason -- and because `render_leaf_toggle` borrows this
	/// for its own two-row list, which would otherwise fight the parent menu's label scroller.
	deluge::ui::ScrollText scroll[kMenuDepth]{};

	// --- typed per-kind edit storage: the widgets do not share one int32 ---
	/// `Kind::Int` and `Kind::Decimal`: `int_value` / `decimal_value` take an `int32_t&`.
	int32_t edit_int = 0;
	/// `Kind::Toggle`: `toggle()` takes a `bool&` (`ui/oled/values.h:169`).
	bool edit_bool = false;
	/// `Kind::Enum`: `enum_value()` takes an `int32_t& index` ...
	int32_t enum_index = 0;
	/// ... plus its own `ListState` ...
	deluge::ui::ListState enum_list{};
	/// ... plus its own `ScrollText` (`ui/oled/values.h:143`).
	deluge::ui::ScrollText enum_scroll{};
	/// `decimal_value`'s in/out cursor position.
	int8_t decimal_pos = 0;
	/// `decimal_value`'s OLED cursor blink ONLY (`ui/core/state.h:38`; `until_ms` is a phase
	/// anchor, not a deadline).
	deluge::ui::Blink cursor_blink{};
	/// `render_grid`: when the current shortcut-blink cycle started.
	///
	/// One anchor for both phases, because legacy has one counter for both
	/// (`sound_editor.cpp:974-1000`). Not a `Blink`: a `Blink` carries a single symmetric period,
	/// which is precisely the shape this cycle is not.
	///
	/// Re-anchored whenever `blink_node` changes, which is legacy's `shortcutBlinkCounter = 0` in
	/// `setupShortcutBlink` (`sound_editor.cpp:964`) -- reached from `updatePadLightsFor` on every
	/// screen change (`:806`, via `beginScreen` `:1015`). Without it the port would free-run from
	/// t = 0 and a newly focused pad could light mid-cycle.
	uint32_t blink_anchor_ms = 0;
	/// Which node `blink_anchor_ms` was set for; `render_grid` is the only writer of both.
	const Node* blink_node = nullptr;

	/// Legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1309-1316`): pressing the focused
	/// item's own shortcut pad a second time takes the second shortcut layer, and pressing any
	/// other pad clears it. `handle()` is the only writer.
	bool second_layer = false;

	// --- what this tick's render observed, for after_render to act on ---
	/// The widget `Response` the render pass produced (`changed` / `entered`).
	deluge::ui::Response render_resp{};
	/// Whether the render pass moved the value in the edit storage above.
	bool value_changed = false;
	/// Which node `value_changed` belongs to.
	///
	/// An addition to the brief's field list, forced by the horizontal layout: there the node on
	/// screen is the *submenu* and the edited value belongs to the focused column's child, so
	/// "the current node" is not enough to commit through.
	const Node* value_node = nullptr;
	/// Whether `render_oled` read (and therefore claimed) the back button this tick. Only ever
	/// set at `depth > 0` -- see `MenuScene::render_oled`.
	bool back_claimed = false;
	/// A select press on a *value* column of a horizontal page, which `columns` does not own (it
	/// claims the press only for a submenu column, `ui/oled/columns.cpp:419-423`). Legacy's
	/// `HorizontalMenu::handleItemAction` answers it with the notification popup rather than any
	/// navigation (`gui/menu_item/horizontal_menu.cpp:657-661`).
	bool column_press = false;
	/// Spec E §4.1's `MustSelectRange` outcome, recorded rather than acted on: E2 owns the range
	/// picker, and E1 leaves the node unentered.
	bool range_picker_pending = false;
	/// The node that would have been entered once a range is chosen.
	const Node* pending_after_range = nullptr;

	/// @brief A scene a focused `CustomItem` asked the shell to push, or `SceneKind::Null` for
	///        none.
	///
	/// The port of the `openUI(...)` call every legacy bespoke item makes from its own
	/// `beginSession` (`gui/menu_item/edit_name.cpp:54`, `file_selector.cpp`, `loop_point.cpp`,
	/// `sample_marker_editor.cpp`, `dx/browse.cpp`, `osc/audio_recorder.h`). `CustomItem::on_focus`
	/// IS `beginSession` (`node.h`) but returns `void` -- and it has to, because it is reached from
	/// `retarget()` and `restore_state()` as well as from `after_render`, neither of which has a
	/// `Result` to put an `Action` in. So the item writes its request here and `after_render`, which
	/// does have one, drains it into a `Push`.
	///
	/// @note Written by a `CustomItem`, read and cleared by `MenuScene::after_render`, and dropped
	///       by `retarget` along with the rest of the per-visit state.
	deluge::ui::shell::SceneId pending_scene{};

	/// @brief Step up one level the next time this scene resumes -- legacy's
	///        `SoundEditor::shouldGoUpOneLevelOnBegin` (`gui/ui/sound_editor.h:113`).
	///
	/// Set when `pending_scene` is pushed, exactly where legacy sets the flag (beside its own
	/// `openUI`), and read in `on_resume`, exactly where legacy reads it
	/// (`SoundEditor::focusRegained`, `sound_editor.cpp:280-283`). Without it, closing the pushed
	/// scene would land back on the item that opened it and `on_focus` would open it again on the
	/// next descent -- which is precisely why legacy has the flag.
	bool go_up_on_resume = false;
};

/// @brief What survives a `replace_root`.
///
/// Child *indices*, not pointers, so the block stays small and a table edit between the save and
/// the restore cannot resurrect a stale pointer -- it can only land on a different child, which
/// the restore clamps.
struct MenuSavedState {
	/// `node_id()` of `path[0]` (`node_id.h`).
	uint16_t root_node_id = 0;
	/// The child index taken at each level: `path_child[i]` indexes `path[i]`'s visible children
	/// to give `path[i + 1]`.
	uint8_t path_child[kMenuDepth] = {};
	uint8_t depth = 0;
	/// `list[depth].cursor` (vertical) or `columns.focused` (horizontal) at save time.
	int16_t focus = 0;
};
static_assert(sizeof(MenuSavedState) <= 32);
static_assert(sizeof(MenuSavedState) <= deluge::ui::shell::kSceneStateBytes); // shell/scene.h:50 -- 256

/// @brief The menu interpreter. One instance for the whole application (`registry.h`'s
///        `menu_scene()`).
class MenuScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at @p root, editing what @p ctx resolved, and reset the path.
	///
	/// The entry point for a scene that opens a menu: the caller resolves a `MenuContext`
	/// (`context_for`) and names the root. `on_enter` re-targets afterwards from `SceneId::arg`,
	/// so a push whose argument names a deeper node lands there instead.
	/// @param root The node to show.
	/// @param ctx  What the menu edits.
	void open(const Node& root, const MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override;
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows` (`greyout_mask()`), it does not
	///        halve every pad.
	bool dims_grid() const override { return false; }
	/// @brief False for every role: a menu covers the Main panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	// `input_transparent()` stays at its default false: `MenuScene` IS the `input_top()`
	// (`shell/scene.h:322`), which is what makes its frames the focused ones.

	deluge::ui::grid::GreyoutMask greyout_mask() const override;
	void save_state(deluge::ui::shell::SceneState& state) const override;
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;
	/// @brief The `ParamDescriptor` MIDI/CC learn (E3) binds to the focused node.
	///
	/// @note **Param learn, not global-command learn.** This is `PatchedParam`/`UnpatchedParam`'s
	///       `getLearningThing()` (`bindings.h`'s `learn_target`): "which automatable parameter
	///       would a CC bind to". The `learn_*` entry points below are the other learn path
	///       entirely -- `midi::Command`'s, which binds a whole MIDI *message* to a
	///       `GlobalMIDICommand` transport action and never touches a `ParamDescriptor`. The
	///       twelve `midi_command_*` nodes have no `bind.param`, so this returns a null descriptor
	///       for every one of them.
	ParamDescriptor learn_target() const;

	/// @name MIDI-learn entry points -- the port of `SoundEditor`'s own three delivery members.
	///
	/// `SoundEditor::noteOnReceivedForMidiLearn` / `midiCCReceived` / `pcReceivedForMidiLearn`
	/// (`gui/ui/sound_editor.cpp:1550-1570`) each forward to `getCurrentMenuItem()->learnX(...)`
	/// and report whether the message was used; `unlearn` is `unlearnAction()` at `:478`. These do
	/// the same, to `focused()`'s `CustomItem` -- the item decides, exactly as legacy's virtual
	/// dispatch let it.
	///
	/// @warning **Nothing calls these yet.** Routing MIDI into the scene stack belongs to the
	///          wiring plan; legacy's `currentUIMode == UI_MODE_MIDI_LEARN` gate and its shift
	///          exclusion (`:1550`, `:1562`) are that plan's, not this layer's -- a scene may not
	///          read a legacy UI mode. What is here is the half a menu owns: which item the message
	///          reaches, and what that item does with it.
	/// @{
	/// @brief Offer a note-on to the focused item. @return Whether it was used.
	bool learn_note_on(::MIDICable& cable, int32_t channel, int32_t note);
	/// @brief Offer a CC to the focused item. @return Whether it was used.
	bool learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value);
	/// @brief Offer a program change to the focused item. @return Whether it was used.
	bool learn_program_change(::MIDICable& cable, int32_t channel, int32_t program);
	/// @brief Ask the focused item to forget what it learned.
	void unlearn();
	/// @brief Does the focused item want the LEARN LED blinking? `MenuItem::shouldBlinkLearnLed`.
	///
	/// Read by `leds()`, which is the port of `SoundEditor::beginScreen`'s own tail
	/// (`sound_editor.cpp:911-916`); public because the wiring plan's learn gate asks the same
	/// question legacy asks at `:488`.
	bool wants_learn_blink() const;
	/// @}
	/// @brief The application's root menus, indexed by `MenuContext::Root`.
	///
	/// @note All ten entries are filled in (`menu_scene.cpp`'s `kRoots`, Tasks 6-8): the sound
	///       root, the drum/MIDI-drum/gate-drum roots, MIDI-or-CV, the audio-clip, performance,
	///       song and kit-global-FX roots, and the settings root. `on_enter(arg == 0)` therefore
	///       resolves a real root from `MenuContext::root`; a caller may still name a node
	///       directly through `open()` or through `SceneId::arg`.
	static std::span<const Node* const> roots();

	/// @brief The live interpreter state.
	///
	/// Exposed for specs and for E2's range picker (which has to see `range_picker_pending`);
	/// nothing outside may write it.
	const MenuState& state() const { return st_; }

private:
	/// @brief Fill @p out with @p node's children that `relevant` admits, resolving
	///        `dynamic_children` first.
	/// @return How many were written.
	size_t visible_children(const Node& node, std::span<const Node*> out) const;
	/// @brief The focused node's `CustomItem`, or `nullptr` when it is not a `Kind::Custom` leaf.
	const CustomItem* focused_custom() const;
	/// @brief @p node's @p index'th visible child, or `nullptr`.
	const Node* child_at(const Node& node, int32_t index) const;
	/// @brief The child the current layout has focus on, or `nullptr`.
	const Node* selected_child(const Node& node) const;

	void render_vertical(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_horizontal(deluge::ui::oled::OledFrame& f, const Node& node);
	// One helper per kind: each owns the widget's own argument shapes rather than sharing an
	// `edit_value` the widgets do not agree on.
	void render_leaf_int(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_decimal(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_enum(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_toggle(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_sync_level(deluge::ui::oled::OledFrame& f, const Node& node);

	/// @brief Load @p node's value into the edit storage its kind uses.
	void enter_leaf(const Node& node);
	/// @brief Commit the edit storage for @p node through `bindings::write`.
	void commit_value(const Node& node);
	/// @brief Descend into @p child if its permission allows, recording `MustSelectRange`.
	void enter_child(const Node& child);
	/// @brief Go up one level, skipping levels whose permission is now `No`
	///        (`sound_editor.cpp:672-683`).
	void ascend();
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target).
	void retarget(const Node& node);

	MenuState st_{};
	MenuContext ctx_{};
};

} // namespace deluge::scenes::menu
