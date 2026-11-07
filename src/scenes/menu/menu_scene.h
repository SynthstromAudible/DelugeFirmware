#pragma once

/// @brief `MenuScene` -- one shell `Scene` that interprets any `Node` tree.
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
/// `preparePaging` (`gui/menu_item/horizontal_menu.cpp`) -- all four already
/// pinned by the `list` / `columns` widgets, which this scene composes rather than re-implements;
/// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp`), `buttonAction` (back ->
/// `goUpOneLevel`, which exits at depth 0 via `exitCompletely`),
/// `horizontalEncoderAction` (`:1046`), `padAction` (`:1460`) with `potentialShortcutPadAction`
/// (`:1166`), `enterSubmenu` (`:362`), `setupShortcutBlink` (`:997`) / `blinkShortcut`
/// (`:1012-1038`), `getGreyoutColsAndRows` (`:252`), and the `MultiRange` redirect
/// (`Permission::MustSelectRange`).
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::oled` widget, so
/// the layout rules live in one place and the count of primitive calls here stays at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "deluge-project/mapping_table.h"
#include "gfx/oled/icon.h"
#include "notify/dirty_set.h"
#include "scenes/menu/action.h"
#include "scenes/menu/chains.h"
#include "scenes/menu/context.h"
#include "scenes/menu/learn.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"
#include "scenes/menu/pads.h"
#include "scenes/menu/page_buttons.h"
#include "scenes/menu/present.h"
#include "scenes/menu/shortcut_blink.h"
#include "scenes/menu/shortcut_overlay.h"
#include "scenes/menu/transition.h"
#include "scenes/menu/value_session.h"
#include "scenes/menu/view.h"
#include "scenes/menu/voice_usage.h"
#include "scenes/menu/walk.h"
#include "shell/scene.h"
#include "ui/core/encoder_acceleration.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"
#include "ui/oled/columns.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::menu {

/// @brief The param an automation item edits, as the automation view names its selection: `Clip::lastSelectedParamKind`
///        and `lastSelectedParamID` (`MenuItem::getParamKind` and `getParamIndex` on legacy's item). A cable's kind is
///        `PATCH_CABLE` and its id the cable's descriptor word, as `PatchCableStrength::getModelStack` addresses it.
struct AutomationParam {
	deluge::modulation::params::Kind kind;
	int32_t id;
	[[nodiscard]] constexpr bool operator==(const AutomationParam&) const = default;
};

/// @brief The menu interpreter. One instance for the whole application (`registry.h`'s
///        `menu_scene()`).
class MenuScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at @p root, editing what @p ctx resolved, and reset the path.
	///
	/// The entry point for a scene that opens a menu: the caller resolves a `MenuContext`
	/// (`context_for`) and names the root. `on_enter` re-targets afterwards from `SceneId::arg`,
	/// so a push whose argument names a deeper node lands there instead.
	///
	/// @warning **`root` is dead weight for every caller that then pushes with `arg == 0`, and both
	///          of this tree's callers do.** `on_enter` re-roots unconditionally: from
	///          `node_by_id(arg)` when `arg != 0`, and otherwise from `ctx_.root` -- the copy this
	///          function just installed -- so whichever node was passed here is overwritten one
	///          call later. Measured, not reasoned: mutating
	///          `gui/ui/shell_menu_entry.cpp`'s call to pass `roots[0]` instead of the resolved
	///          root left **nothing in the suite noticing**. The parameter is kept because this is
	///          the only entry point that installs a `MenuContext` at all, and because a scene left
	///          pointing at the wrong root between this call and `on_enter` is a real (if brief)
	///          inconsistency for anything that reads `focused()`. What it is not is a second
	///          chance to get the root right. Splitting the context install from the re-root -- or
	///          asserting here that `root` is the one `ctx.root` names -- is the fix, and it is a
	///          `MenuScene` API change rather than a caller's.
	///
	/// @param root The node to show. See the warning: `on_enter` decides the final one.
	/// @param ctx  What the menu edits.
	void open(const Node& root, const MenuContext& ctx);

	/// @brief Back to what a boot leaves: every submenu's remembered row forgotten (each legacy `Submenu` keeps its
	///        `current_item_` for the life of the firmware, and this scene is as long-lived), the horizontal encoder's
	///        acceleration and the voice-usage clock reset.
	void reset_process_state();

	deluge::ui::shell::SceneKind kind() const override;
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	/// @brief Will the next render claim \p event? Back at any depth but the root, and whatever the focused
	///        page's widgets read (`owned_input`).
	/// @param event A widget-owned shape.
	/// @return True when the render will consume it.
	bool owns(const deluge::ui::shell::UiEvent& event) const override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;
	/// @brief Select acts on its release, as `SoundEditor::buttonAction` does (`sound_editor.cpp:609-614`).
	/// @return True.
	bool select_acts_on_release() const override { return true; }
	/// @brief True in learn mode, which `SoundEditor::buttonAction`'s Select arm does not act in
	///        (`sound_editor.cpp:609-614`).
	bool select_release_blocked() const override { return visit_.learn.mode(); }
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief The main pads while the SHIFT overlay shows (`SoundEditor::renderMainPads` answers for them then);
	///        otherwise none, and the pads are the root's.
	deluge::ui::shell::PadRegions pad_regions() const override {
		return overlay_up() ? deluge::ui::shell::PadRegions::Main : deluge::ui::shell::PadRegions::None;
	}
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief `SoundEditor::padAction`'s gate at `gui/ui/sound_editor.cpp:1467`, as a declaration.
	///
	/// Three predicates there, `!inSettingsMenu() && !inNoteEditor() && !inNoteRowEditor()`, decide
	/// whether `potentialShortcutPadAction` even runs; on any of the three failing, every pad --
	/// shift or not -- falls through to the forwarding switch and the tail instead. `inSettingsMenu`
	/// is `Root::Settings`, `inNoteEditor` is `Root::NoteEditor`, and `inNoteRowEditor` is `Root::NoteRowEditor`.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return (ctx_.root == MenuContext::Root::Settings || ctx_.root == MenuContext::Root::NoteEditor
		        || ctx_.root == MenuContext::Root::NoteRowEditor)
		           ? deluge::ui::shell::GridPadClaim::None
		           : deluge::ui::shell::GridPadClaim::ShortcutsOnly;
	}
	/// @brief False for every role: a menu covers the Main panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	// `input_transparent()` stays at its default false: `MenuScene` IS the `input_top()`
	// (`SceneStack::input_top`), which is what makes its frames the focused ones.

	deluge::ui::grid::GreyoutMask greyout_mask() const override;
	void save_state(deluge::ui::shell::SceneState& state) const override;
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief Grid only: `next_timer()`'s deadline here is purely the
	///        shortcut/source pad blink cadence -- a grid concern -- and the submenu's OLED content
	///        never depends on it. Arming the OLED gates from it too would cost nothing in pixels but
	///        blit both panels unconditionally, forever, whenever the
	///        focused node had shortcut or source pads. See `Scene::timer_repaints()`'s own doc.
	/// On the voice usage page the timer is also its 500 ms sample, which the panel shows.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override;
	/// @brief Advance the voice usage page's sampling clock (`VoiceUsage::timerCallback`, `cpu/voice_usage.h:77-87`).
	void tick(uint32_t dt_ms) override;
	/// @brief The voice usage page's counters.
	const VoiceUsageSampler& voice_usage() const { return voice_usage_.sampler(); }

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;

	/// @brief The value node the select/gold encoders would actually edit right now.
	///
	/// On a `Layout::Horizontal` submenu this is the focused column, *if* that column is not
	/// itself a submenu -- `Submenu::shouldForwardButtons` (`gui/menu_item/submenu.cpp:181-185`):
	/// legacy's same-param popup test (`View::modEncoderAction_forSomeReason`'s guard,
	/// `gui/views/view.cpp`) forwards through a horizontal page to whichever item the
	/// buttons would actually reach, not the page itself. On any other leaf this is `focused()`
	/// unchanged -- there the leaf IS what the encoders edit. On a vertical submenu, or a
	/// horizontal page focused on a submenu column, there is nothing to edit and this is null.
	///
	/// Exists for `services::ModKnobs::popup_suppressed` (`scenes/services/mod_knobs.cpp`)'s
	/// same-param test: `focused()` stops at the page itself on a horizontal menu, which is wrong by
	/// the same measure `shouldForwardButtons` names.
	const Node* focused_value() const;

	/// @brief Replace the facts about the root UI under the menu that the opener set: its greyout arm, and
	///        whether it is the keyboard screen or the automation view. The host calls it whenever the root, or
	///        the automation view's editor, changes while the menu is open; legacy re-reads them on every
	///        greyout (`sound_editor.cpp:497-500`).
	void set_root_facts(MenuContext::RootGreyout greyout, bool root_is_keyboard, bool root_is_automation);

	/// @brief The automation view's identity of the param `focused_value()` edits, or nullopt where it edits none.
	std::optional<AutomationParam> focused_automation_param() const;

	/// @brief Where a horizontal turn under the menu goes (`route_horizontal_turn`).
	enum class HorizontalTurn : uint8_t {
		Menu,    ///< The menu's: fed to it (a page, which does nothing with it; a cable page's decimal cursor).
		Root,    ///< Through to the root UI under the menu (`Param::horizontalEncoderAction`).
		Neither, ///< Nowhere: a cable page's cursor held still by X_ENC, short of the edge delay.
	};
	/// @brief Route a horizontal turn of @p offset, as legacy's current item does: a param leaf passes it to the root
	///        UI; a patch cable's page moves its decimal cursor (not while X_ENC is held) and passes the turn on only
	///        once the cursor has stood at an edge for `kShortPressTime`
	///        (`PatchCableStrength::horizontalEncoderAction`); anything else keeps it.
	///
	/// Ask once per turn: it keeps the cable page's edge delay.
	/// @param offset   The turn's detents.
	/// @param xenc_held Whether X_ENC is held (zooming the view under the menu).
	/// @param now_ms   The turn's time.
	HorizontalTurn route_horizontal_turn(int32_t offset, bool xenc_held, uint32_t now_ms);

	/// @brief Collect the change of the value node the last tick's event made, with its cause, and clear it.
	///
	/// An event that can move the menu (a select release or BACK, which the render consumes; a column button, a pad's
	/// jump) notes the value node it found; the record is filed after the tick's navigation, from that node to the one
	/// the menu is on then. One record per tick; the host drains it after `Shell::tick`, as it drains `take_action`.
	/// @return The record, or nullopt when no such event arrived since the last call.
	std::optional<MenuTransition> take_transition();

	/// @brief Re-read the focused node's value from the model -- `MenuItem::readValueAgain`.
	///
	/// The port of the one call legacy makes to it from outside the menu:
	/// `View::modEncoderButtonAction_changeModControllable`'s
	/// `soundEditor.getCurrentMenuItem()->readValueAgain()` (`gui/views/view.cpp`), run
	/// after a gold-knob **push** has changed a `ModControllable` param behind the menu's back. The
	/// edit storage this scene keeps (`MenuState::edit_int`, `enum_index`, …) is loaded on focus,
	/// so without this the menu would keep showing the value it read on the way in.
	///
	/// Spelled as `enter_leaf(node, FocusCause::NoRowPress)` because that is exactly what
	/// `readValueAgain` is -- reload the storage, with nothing pressed on this row (legacy's
	/// `beginScreen`/`focusRegained` shape). A no-op when nothing is focused.
	void read_value_again();
	/// @brief The param a knob learn binds to on the learn node (`MenuLearn::node`): the row, column or item under the
	///        cursor.
	///
	/// @note **Param learn, not global-command learn.** This is `PatchedParam`/`UnpatchedParam`'s
	///       `getLearningThing()` (`bindings.h`'s `learn_target`): "which automatable parameter
	///       would a CC bind to". The `learn_*` entry points below are the other learn path
	///       entirely -- `midi::Command`'s, which binds a whole MIDI *message* to a
	///       `GlobalMIDICommand` transport action and never touches a param. The twelve
	///       `midi_command_*` nodes have no `bind.param`, so this returns none for every one of them.
	std::optional<deluge::project::OwnerParamTarget> learn_target() const;

	/// @name MIDI-learn entry points -- the item learns of `SoundEditor`'s learn answers.
	///
	/// Each delivers one message to the learn node's item, as `getCurrentMenuItem()->learnX(...)` does, and says
	/// whether it learned; `unlearn` is `unlearnAction()`. In learn mode a real MIDI message reaches the same members
	/// through `handle()` (`MenuLearn::offer`), which also decides whether the message was used.
	///
	/// @warning **`wants_learn_blink()` is NOT the learn gate.** Legacy enters learn mode on
	///          `getCurrentMenuItem()->allowsLearnMode()` (`SoundEditor::buttonAction`'s LEARN arm), ported as
	///          `MenuLearn::allows_mode`, and only then asks `shouldBlinkLearnLed()` for the LED (`:488`, `:911-916`).
	///          They are different predicates over different sets: `allowsLearnMode` is true on every
	///          `MenuItemWithCCLearning` -- all the patched/unpatched params, the transpose items, the MIDI-follow
	///          items -- every one of which blinks *false*.
	/// @{
	/// @brief Offer a note-on to the learn node's item.
	/// @param cable   The cable it arrived on.
	/// @param channel Its MIDI channel.
	/// @param note    Its note number.
	/// @return Whether it learned.
	bool learn_note_on(::MIDICable& cable, int32_t channel, int32_t note);
	/// @brief Offer a CC to the learn node's item.
	/// @param cable   The cable it arrived on.
	/// @param channel Its MIDI channel.
	/// @param cc      The controller number.
	/// @param value   The controller value.
	/// @return Whether it learned.
	bool learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value);
	/// @brief Offer a program change to the learn node's item.
	/// @param cable   The cable it arrived on.
	/// @param channel Its MIDI channel.
	/// @param program The program number.
	/// @return Whether it learned.
	bool learn_program_change(::MIDICable& cable, int32_t channel, int32_t program);
	/// @brief Ask the learn node's item to forget what it learned.
	void unlearn();
	/// @brief Does the focused item want the LEARN LED blinking? `MenuItem::shouldBlinkLearnLed`.
	///
	/// Read by `leds()`, which is the port of `SoundEditor::beginScreen`'s own tail
	/// (`sound_editor.cpp:949-954`). Public because the LED is also legacy's own answer at `:488`,
	/// on the way *out* of learn mode -- **not** because it is the gate into it. See the second
	/// `@warning` above: that gate is `allowsLearnMode()`, which this is not a synonym for.
	bool wants_learn_blink() const;
	/// @}
	/// @brief The application's root menus, indexed by `MenuContext::Root`.
	///
	/// @note All twelve entries are filled in (`menu_scene.cpp`'s `kRoots`): the sound
	///       root, the drum/MIDI-drum/gate-drum roots, MIDI-or-CV, the audio-clip, performance,
	///       song and kit-global-FX roots, the settings root, and the note-editor and
	///       note-row-editor roots. `on_enter(arg == 0)` therefore resolves a real root from
	///       `MenuContext::root`; a caller may still name a node directly through `open()` or
	///       through `SceneId::arg`.
	static std::span<const Node* const> roots();

	/// @brief The live interpreter state.
	///
	/// Exposed for specs and for the range picker (which has to see `range_picker_pending`);
	/// nothing outside may write it.
	const MenuState& state() const { return st_; }
	/// @brief The state, writable: for specs that file a mailbox the way a node would.
	MenuState& state_for_spec() { return st_; }

	/// @brief One arm of `after_render`, in the order "The one-shot mailboxes" (`MenuState`) documents.
	enum class DrainBand : uint8_t {
		RangeChoice,
		SourceChoice,
		MidiCableChoice,
		ValueCommit,
		Navigation,
		PendingAction,
		Repaint,
		PendingScene,
	};
#ifdef DELUGE_HOST
	/// @return The arms the last `after_render` acted on, in the order they acted: the witness of the drain order.
	///         Host builds only; the device keeps no trace.
	std::span<const DrainBand> drain_trace() const { return {drain_trace_.data(), drain_trace_len_}; }
#endif

	/// @brief What the menu is currently editing -- the `MenuContext` the last `open()` installed.
	///
	/// This exists for a real gap rather than convenience: the application's
	/// entry points resolve this context themselves (`gui/ui/shell_menu_entry.cpp`), and two of its
	/// fields -- `is_session_view` and `is_performance` -- change nothing about which root is opened
	/// and everything about which items are relevant inside it. Without this accessor a caller
	/// passing the wrong one is invisible from outside the scene, so "the session and the arranger
	/// open the same root" could not be told apart from "the session and the arranger are the same
	/// thing". Read-only, like `state()`.
	///
	/// @note Spelled `menu_context` rather than `context` on purpose: CppSpec's `cppspec.hpp`
	///       defines `context` as a macro (`#define context self.context`), so a member of that
	///       name is unusable from every spec in this tree -- which is precisely where this
	///       accessor is meant to be read.
	const MenuContext& menu_context() const { return ctx_; }

	/// @brief Collect the root-UI action a menu item asked for, if any, and clear it.
	///
	/// The application-facing half of the action seam (`scenes/menu/action.h`). `after_render`
	/// drains `MenuWalkState::pending_action` into a one-slot outbox here -- beside where it drains
	/// `pending_scene` into a `Push`, and for the same reason: an item's hook has no `Result` to put
	/// anything in. `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`) polls this
	/// after ticking the shell, and is where each verb's legacy statement lands.
	///
	/// @note **All seven of these actions are performed.** The consumer polls once per guest tick and
	///       has a body for each: `OpenPerformanceView`, `StartStemExport`, the three one-line verbs
	///       (`SetClipLengthToSampleLength`, `OpenSongMidiLearn`, `OpenSongMacros`), `SetShortcutsVersion`
	///       and `ScreensaverSettingsChanged`. What each
	///       arm still owes is a named residue per arm, recorded at the arms themselves
	///       (`gui/ui/shell_action_dispatch.h`) rather than here.
	///
	/// @warning **This is a one-slot mailbox with a silent overwrite, not an exactly-once queue.**
	///          Two requests filed between two polls lose the first, and a request nobody polls for
	///          is delivered whenever someone finally does -- arbitrarily later, against whatever
	///          state exists then. `after_render` returns no `Action` announcing that one is
	///          waiting, so nothing enforces the polling: **poll after every tick, or requests are
	///          dropped or stale.**
	///
	///          That is deliberate. Making it exactly-once would mean either a queue (this layer
	///          allocates nothing, so a fixed ring with its own overflow policy -- the same problem
	///          one level down) or a new `Action` kind for the router to carry, which would put a
	///          concept the shell has no business knowing about (a legacy root-UI call) into
	///          `shell/scene.h`. An honest mailbox plus this warning is better than a
	///          guarantee that only half holds. **No shipped item can lose a request today, but the
	///          reason is narrow and the arithmetic does not carry it.** "One tick delivers at most
	///          one select press" would be a complete argument only if a select press were the sole
	///          route in. There are two routes: a `CustomItem` writing `pending_action` from `on_focus`/`render`, and
	///          `request_action` below, reached from a `Binding::set` hook by way of `commit_value`.
	///          Each fires at most once per `after_render`, so the ceiling is **two**, not one --
	///          "once each" is exactly the pair this paragraph needs to rule out, and it does not
	///          rule it out.
	///
	///          What rules it out is which items exist. The only caller of `request_action` is
	///          `nodes::set_shortcuts_version` (`menu/nodes/settings.cpp`), and its node is an
	///          ordinary `Kind::Enum` leaf with no `select_chain` and no `CustomItem` under it --
	///          so the `after_render` that commits its value has nothing to descend into, and
	///          cannot also reach an `on_focus` that files. **That is a fact about today's tables,
	///          not an invariant**: an item that filed from both routes in one tick would silently
	///          lose the first, and nothing here would say so.
	///
	/// @return The request, or `MenuAction::None` when none was filed since the last poll. Reading
	///         clears the slot.
	MenuActionRequest take_action();

	/// @brief Put the DX7 pages on @p dx: the param the parameter page shows and the two lists' cursors, which the next
	///        `open` carries over. An entry that opens the parameter page on a param of its own sets it first, as
	///        legacy sets `dxParam.param` before `soundEditor.setup`.
	void land_dx(const DxState& dx) { st_.dx = dx; }
	/// @brief Land on the DX7 cartridge page when the `.syx` browser over the menu closes, in place of going up: the
	///        browser read a bank. `DxSyxBrowser`'s `enterSubmenu(&dxCartridge)` then cleared
	///        `shouldGoUpOneLevelOnBegin`, so the page sits under "load DX7 .syx file", whose BACK reopens the browser.
	void land_dx_cartridge() { land_dx_cartridge_ = true; }
	/// @brief Shift + the keyboard's DX column pad for operator @p op with the menu open: `openForOpOrGlobal`. On the
	///        parameter page it moves the param (`dx::column_param`'s toggle and carry); anywhere else the menu moves
	///        to the page on the operator's param, as legacy's `setup()` re-roots, on its next tick. Either way the
	///        param's pad blinks.
	/// @param op The operator, 0-5; 6 and 7 for the column's two global pads.
	void dx_column_press(int32_t op);
	/// @brief The keyboard DX column row that shows white at @p now_ms: the param's operator row (`dx::flash_row`)
	///        during the 100 ms white half of `blinkSideColumn`'s alternation, which every param change restarts,
	///        while the parameter page is open over the keyboard screen.
	/// @param now_ms The guest's clock.
	/// @return The row, or nothing when no row is white.
	std::optional<int32_t> dx_column_flash(uint32_t now_ms) const;

	/// @brief File @p request on behalf of a hook that is handed no `MenuState` to write.
	///
	/// **The one route into `pending_action` that is not an item writing the field itself**, and it
	/// exists because `Binding::set` is `void (*)(const MenuContext&, int32_t)` (`SetFn`, `node.h`):
	/// a value-write hook gets the context and the value and nothing else. Every other filer is a
	/// `CustomItem`, whose `on_focus`/`render` are handed `MenuState&` and write the field directly
	/// (`nodes/actions.cpp`).
	///
	/// Its only caller today is `nodes::set_shortcuts_version` (`nodes/settings.cpp`), which files
	/// `MenuAction::SetShortcutsVersion` beside its own mirror write -- see `scenes/menu/action.h`'s
	/// "the sixth item" for why that item's write, rather than its press, is what crosses the wall.
	///
	/// @par The timing, which is what makes this safe rather than merely convenient
	///
	/// A hook reaches this through the `menu_scene()` singleton (`scenes/registry.h`) -- the same
	/// route `nodes/context_menu_items.cpp` already takes to `context_menu_scene()`. The only path
	/// that calls a `Binding::set` hook is `commit_value` (`bindings::write`), and `commit_value`'s
	/// one call site is inside `after_render`, **above** the `pending_action` drain. So a request
	/// filed here is drained on the same tick it was filed, by the same `after_render` call, and
	/// never sits in the field across a tick boundary where `retarget` could drop it.
	///
	/// @warning This overwrites whatever is in the field, exactly as an item's own assignment does.
	///          The one-slot mailbox's warning above applies unchanged.
	void request_action(MenuActionRequest request) { st_.walk.pending_action = request; }

	/// @brief Re-root the menu onto the horizontal page or group that lists `MenuWalkState::scene_opener`, with
	///        that item focused -- the sample claim (`SampleClaim`)'s return affordance
	///        (`gui/ui/browser/sample_browser.cpp:974-976`).
	///
	/// **The one thing `MenuState::path`/`depth` may be written for from outside this class**, and
	/// it exists because legacy's answer to "where does accepting a file land you" is spelled in
	/// two `MenuItem*`s no port carries. Legacy's three statements, in order, and what each becomes:
	///
	///   * `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` (`:1116`) -> `columns[0].focused`
	///     set to the item's index among the page's visible children. Legacy's `focusChild` writes
	///     `Submenu::current_item_`, a member of the page object (`gui/menu_item/submenu.cpp:21`);
	///     the port's focus is per *depth* (`MenuState::columns`), so moving the page to depth 0
	///     is what makes this a separate statement rather than something the move carries along.
	///   * `soundEditor.menuItemNavigationRecord[0] = parentMenuHeadingTo` (`:1117`) -> `path[0]`.
	///   * `soundEditor.navigationDepth = 0` (`:1118`) -> `depth = 0`. This is the whole of the
	///     "one extra BACK press" it would otherwise cost: without it the user is left on the file
	///     selector at whatever depth it was opened from, one level further in than legacy leaves
	///     them -- and, on the shortcut-pad route, out of the menu altogether.
	///
	/// @par Why the page is resolved here rather than carried in
	///
	/// Legacy writes `parentMenuHeadingTo` on the way in because `SoundEditor::maybeGetParentMenu`
	/// (`gui/ui/sound_editor.cpp`) is the only place that has the chain in scope. Its body is
	/// `find_if(chain, [](HorizontalMenu* m) { return m->hasItem(item); })` (`:2084`) over
	/// `getCurrentHorizontalMenusChain(false)` (`:2079`, dispatched on the root at `:2108-2130`).
	/// This scene has the root (`MenuContext::root`) and the chains (`chain_for`), so it answers the
	/// same question: the first chain entry that lists the item, which for an oscillator's file
	/// selector is the whole `source_group` (`sourceMenuGroup`), paged across its five members. A
	/// root whose chain lists the item nowhere falls back to the horizontal page in its subtree that
	/// does (`horizontal_page_listing`). Carrying a resolved page in would have meant paying that
	/// search on every scene push, including the four that have no return affordance at all.
	///
	/// @par Why an immediate mutator, and not `request_action`'s mailbox
	///
	/// `request_action` above is a mailbox because its caller is a `Binding::set` hook running
	/// *inside* `after_render`, where the drain it files into is two statements below. This one's
	/// caller is the application, between ticks (`gui::ui::shellSampleClaimDone`), and the shape
	/// for that is `open()` -- which already installs a `MenuContext` and re-roots `path`/`depth`
	/// from `gui/ui/shell_menu_entry.cpp` with no mailbox in sight. A mailbox would also be wrong
	/// rather than merely heavier: a pending re-root would sit in the field until the next
	/// `after_render`, one frame later, and `retarget` clears the mailboxes (see its body), so any
	/// re-root filed across a re-root would be silently dropped.
	///
	/// @warning Re-rooting abandons the walk that reached the item, exactly as `retarget` does for
	///          `open()` and the shortcut-pad jump -- every per-visit mailbox goes with it.
	///
	/// @return True if the menu moved. False when there is nothing to move: the `HorizontalMenus` feature off
	///         (`MenuContext::horizontal_menus`), no scene opener recorded, no path (`on_model_changed`'s song swap), a
	///         root out of range, or an item no horizontal page in this root lists.
	bool reroot_to_scene_opener_page();

	/// @brief Where a Shift + main pad from a scene the menu opened (the sample marker editor) jumps:
	///        `SoundEditor::potentialShortcutPadAction` asked from outside the editor, on the plain shortcut table
	///        (the second layer and the patching-source column are the editor's own screen's).
	///
	/// Arms the landing exactly as the menu's own Shift + pad does, so the `Replace` the caller files lands the
	/// same way (`on_enter`). An item the context refuses raises "PARAMETER NOT APPLICABLE", as legacy's `setup`.
	/// @param x,y The pad.
	/// @return The menu `SceneId` to replace with, or nothing: no item there, not this root's, or refused.
	std::optional<deluge::ui::shell::SceneId> shortcut_jump(uint8_t x, uint8_t y);

	/// @brief Shift+SAVE under the menu (`SoundEditor::buttonAction`'s SAVE arm): the note-range picker's delete
	///        (`MultiRange::deletePress`) when the picker is focused, and nothing otherwise. The range it leaves
	///        selected is the picker's choice, which the next frame applies.
	void delete_range_press();

	/// @brief A note played under the note-range picker (`MultiRange::noteOnToChangeRange`): with no edge edited, the
	///        picker moves to the range @p note falls in.
	/// @param note The note, transposed as the sound plays it.
	/// @return Whether the picker was focused and took the note.
	bool select_range_for_note(int32_t note);

	/// @brief Prepare the next `on_enter` to open a shortcut pad's @p target where
	///        `SoundEditor::setup(clip, item, sourceIndex)` lands it, and name the node to open.
	///
	/// The page is `shortcut_landing_page(menu_context(), target)`, so `open()` must already
	/// have run. Only the column to focus is recorded; the path moves in `on_enter`.
	///
	/// @param target The node the pad names.
	/// @return The `SceneId::arg` to open: the page's id when the target lands on a page, else the
	///         target's own; 0 when that node has no id.
	uint16_t arm_shortcut_landing(const Node& target);

	/// @brief The inputs `render_oled` claims for the page focused now: the claim sites of `render_oled` and the
	///        `render_*` functions it dispatches to, answered before they run.
	/// @return The claim set.
	deluge::ui::InputClaims owned_input() const;
	/// @brief The focused horizontal page's paging (empty off one): the page counter and the SCALE/CROSS_SCREEN rule
	///        read the same.
	deluge::ui::oled::Paging horizontal_paging() const { return menu::horizontal_paging(ctx_, st_); }
	/// @brief The focused page's title (`menu::page_title`).
	std::string_view page_title(NodeText& scratch) const { return menu::page_title(ctx_, st_, scratch); }

private:
	/// @brief Whether the SHIFT shortcut overlay shows (`shortcut_overlay.h`).
	bool overlay_up() const { return overlay_shown(ctx_, visit_.shift_held); }
	/// @brief `after_render`'s navigation and its other arms, before the tick's transition is filed.
	deluge::ui::shell::Result after_render_navigation();
	/// @brief Note the cause of a transition @p event may make, with the value node @p value_before it found
	///        (`take_transition`).
	void note_transition(const deluge::ui::shell::UiEvent& event, const deluge::ui::shell::Result& r,
	                     const Node* value_before);
	/// @brief Note @p cause and the value node @p value_before it leaves, unless this tick already noted one.
	void note_cause(TransitionCause cause, const Node* value_before);
	/// @brief `handle`'s body, beneath its SHIFT-overlay bookkeeping.
	deluge::ui::shell::Result handle_event(const deluge::ui::shell::UiEvent& event,
	                                       const deluge::ui::shell::UiContext& ctx);
	/// @brief The scene's context and state, for a unit's call.
	MenuCore core() { return {ctx_, st_}; }
	/// @brief The child the current layout has focus on, or `nullptr`.
	const Node* selected_child(const Node& node) const;
	bool on_horizontal_page() const { return menu::on_horizontal_page(ctx_, st_); }
	bool is_horizontal(const Node& node) const { return menu::is_horizontal(ctx_, node); }
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target), and begin the voice usage
	///        page's session afresh.
	void retarget(const Node& node);

	MenuState st_{};
#ifdef DELUGE_HOST
	/// What the last `after_render` drained, in order (`drain_trace`).
	std::array<DrainBand, 8> drain_trace_{};
	size_t drain_trace_len_ = 0;
#endif
	void trace_drain([[maybe_unused]] DrainBand band) {
#ifdef DELUGE_HOST
		if (drain_trace_len_ < drain_trace_.size()) {
			drain_trace_[drain_trace_len_++] = band;
		}
#endif
	}
	/// The voice usage page's clock and counters; process-lifetime, like legacy's `cpu::VoiceUsage`.
	VoiceUsageClock voice_usage_;
	/// The renders, and the storage a horizontal page draws from.
	MenuView view_;
	MenuContext ctx_{};
	/// @brief The cause an event of this tick noted, and the value node it found (`take_transition`).
	std::optional<TransitionCause> pending_transition_;
	const Node* transition_from_ = nullptr;
	/// @brief The record filed by the last `after_render`, until `take_transition` collects it.
	std::optional<MenuTransition> transition_;
	/// @brief When a cable page's cursor, standing at an edge, starts passing horizontal turns on; 0 while it moves.
	uint32_t cable_edge_until_ms_ = 0;

	/// @brief The one-slot outbox `take_action()` empties. Not part of `MenuState`: `retarget`
	///        clears the whole of that on a re-root, and an action already filed must still be
	///        delivered even if the path moves in the same tick.
	MenuActionRequest taken_action_{};

	/// The shortcut pads: their blinks, the jump and the landing it arms.
	MenuPads pads_;
	/// A landing `shortcut_jump` armed for the scene above, which the resume its removal brings must keep.
	bool landing_survives_resume_ = false;
	/// The `.syx` browser over the menu read a bank: its resume lands on the cartridge page (`land_dx_cartridge`).
	bool land_dx_cartridge_ = false;
	/// A node the next `after_render` re-targets the menu to with a `Replace` (`dx_column_press`), or 0 for none.
	uint16_t pending_retarget_ = 0;
	/// The clock of the last grid render, which `next_timer` measures the DX column's next flip from.
	uint32_t render_ms_ = 0;
	/// Whether the keyboard DX column flashes: the parameter page over the keyboard, drawn at least once.
	bool dx_column_flashing() const;

	/// @brief What belongs to one open of the scene: reset by value in `open()` and `on_enter()`, which is legacy's
	///        `beginScreen` -- a new screen.
	struct Visit {
		/// @brief Use up the current Select press at the next `after_render`: legacy's `SoundEditor::opened` and
		///        `focusRegained` (`sound_editor.cpp`), so the release of a press that opened or
		///        returned to the menu does not act.
		bool use_up_select = true;
		/// Whether the next `leds()` restarts the shared normal blink, BACK's and LEARN's (`blinkLed`), and the screen
		/// the render last checked that against (`beginScreen` restarts it for a LEARN-blinking item).
		bool blink_restart = true;
		/// Learn mode and its LED.
		MenuLearn learn;
		/// @brief Select was held during the last render: a value edited under it uses the press up
		///        (`Value::selectEncoderAction`, `menu_item/value.h:64-67`).
		bool select_held = false;
		const Node* blink_screen = nullptr;
		/// The value being edited.
		ValueSession session;
		/// The shift level the last event or uncovered frame brought, latched or not: the SHIFT overlay's.
		bool shift_held = false;
		/// The visit's first `after_render` has run: a scene pushed before it opened straight from the menu's entry.
		bool entered = false;
		/// A scene the entry opened at once covers the menu: legacy's `SoundEditor::opened` opened it from
		/// `beginScreen`, before `focusRegained` set the editor's LEDs, so the menu declares none until it resumes.
		bool leds_held_back = false;
	};
	Visit visit_{};
	/// The walk through the tree, and the rows each submenu was last left on (process lifetime).
	MenuWalk walk_;

public:
	/// @name The two navigation steps a caller outside the scene takes
	///
	/// They belong beside `read_value_again()` in spirit.
	/// @{
	/// @brief One level up the path -- `SoundEditor::goUpOneLevel` (`gui/ui/sound_editor.cpp`), for a
	///        caller outside the scene.
	///
	/// For the note-mode handlers that call `soundEditor.goUpOneLevel()` themselves
	/// (`gui/views/instrument_clip_view.cpp`'s `nudgeNotes`, and `SoundEditor::toggleNoteEditorParamMenu`'s
	/// release arm). The step is `ascend()`, which is
	/// already this scene's port of the loop at `:714-722` and of `beginScreen(oldItem)` at `:728`.
	///
	/// @return **false where legacy exits completely instead**, having moved nothing in the first case:
	///         the loop's `if (navigationDepth == 0) { exitCompletely(); return; }` (`:715-717`) fires
	///         either on the first pass, at depth 0, or on a later pass, when the loop has stepped down to
	///         depth 0 and `:720-722` refuses that level too. Closing the whole editor is the
	///         application's act, not the scene's -- `gui::ui::closePortedMenu()` -- so the caller does it
	///         on false. `ascend()` at depth 0 asks for the scene's own close instead, which these callers
	///         do not want, so this takes that request back on false.
	bool go_up_one_level();
	/// @brief Push @p node one level below the focused node -- `SoundEditor::enterSubmenu`
	///        (`gui/ui/sound_editor.cpp`), for a caller outside the scene.
	///
	/// For `SoundEditor::toggleNoteEditorParamMenu`'s press arm
	/// (`:1967-1969`). Legacy's body is `endScreen()`, `navigationDepth++`, the record write and
	/// `beginScreen(navigatedBackwardFrom)` (`:364-368`), and `beginScreen` begins the item's session
	/// without asking `checkPermissionToBeginSession` (`:957-976`) -- so this is `descend_to`, which
	/// asks no permission either, and nothing more. **`menu_context()` is untouched**: unlike `open()`,
	/// this never replaces `ctx_`.
	///
	/// @note **A recorded divergence: the focus the entered page lands on.** Legacy's `beginScreen`
	///       begins the entered item's session (`gui/ui/sound_editor.cpp:959`), and the toggle names no
	///       item to navigate back from (`:1968`). For a `HorizontalMenu` that is
	///       `HorizontalMenu::beginSession` (`gui/menu_item/horizontal_menu.cpp:40-48`) ->
	///       `Submenu::beginSession` (`gui/menu_item/submenu.cpp:11-19`) -> `focusChild(nullptr)`
	///       (`:21-37`), which keeps the process-lifetime object's `current_item_`: a second toggle hold
	///       lands on the column last focused. `descend_to` resets `st_.list[depth]` and
	///       `st_.columns[depth]` (`MenuWalk::descend_to`, `menu/walk.cpp`), so this always lands on the
	///       first. The port keeps focus per depth for a
	///       session, not per menu object for the process, and meets it by a descend rather than a
	///       re-open.
	/// @param node The submenu to enter.
	void enter_submenu(const Node& node);
	/// @}
};

/// @brief The automation view's identity of the param @p item edits, on the owner @p ctx edits; nullopt where it edits
///        none.
std::optional<AutomationParam> automation_param(const Node& item, const MenuContext& ctx, const MenuState& state);

/// @brief Whether shift + select on @p item deletes its param's automation: a param node, a node whose value is learned
///        as a param (the transposes), or a custom item that says so (the cable strength pages) -- legacy's
///        `Automation` items.
bool is_automation_item(const Node& item);

/// @brief The address of the param whose automation shift + select on @p item deletes, on the owner @p ctx edits;
///        nullopt where it edits none.
std::optional<deluge::project::ParamAddress> automation_address(const Node& item, const MenuContext& ctx,
                                                                const MenuState& state);

} // namespace deluge::scenes::menu
