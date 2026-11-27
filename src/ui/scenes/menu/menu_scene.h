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
/// **Ports.** `MenuItem::drawItemsForOled` (`origin/main:src/deluge/gui/menu_item/menu_item.cpp`),
/// `Submenu::drawSubmenuItemsForOled` / `focusChild` (`origin/main:src/deluge/gui/menu_item/submenu.cpp`),
/// `HorizontalMenu::renderOLED` / `renderMenuItems` / `switchVisiblePage` / `selectMenuItem` /
/// `preparePaging` (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`) -- all four already
/// pinned by the `list` / `columns` widgets, which this scene composes rather than re-implements;
/// `SoundEditor::selectEncoderAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), `buttonAction` (back ->
/// `goUpOneLevel`, which exits at depth 0 via `exitCompletely`),
/// `horizontalEncoderAction`, `padAction` with `potentialShortcutPadAction`
///, `enterSubmenu`, `setupShortcutBlink` / `blinkShortcut`
///, `getGreyoutColsAndRows`, and the `MultiRange` redirect
/// (`Permission::MustSelectRange`).
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::widgets::oled` widget, so
/// the layout rules live in one place and the count of primitive calls here stays at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "base/notify/dirty_set.h"
#include "project/mapping_table.h"
#include "ui/gfx/oled/icon.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/learn.h"
#include "ui/scenes/menu/menu_core.h"
#include "ui/scenes/menu/menu_state.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/pads.h"
#include "ui/scenes/menu/page_buttons.h"
#include "ui/scenes/menu/present.h"
#include "ui/scenes/menu/shortcut_blink.h"
#include "ui/scenes/menu/shortcut_overlay.h"
#include "ui/scenes/menu/transition.h"
#include "ui/scenes/menu/value_session.h"
#include "ui/scenes/menu/view.h"
#include "ui/scenes/menu/voice_usage.h"
#include "ui/scenes/menu/walk.h"
#include "ui/shell/scene.h"
#include "ui/widgets/core/encoder_acceleration.h"
#include "ui/widgets/core/response.h"
#include "ui/widgets/core/state.h"
#include "ui/widgets/grid/effects.h"
#include "ui/widgets/oled/columns.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::ui::scenes::menu {

/// @brief The param an automation item edits, in its plugin: a cable's is its depth id, the cable
///        `PatchCableStrength::getModelStack` (`origin/main:src/deluge/gui/menu_item/patch_cable_strength.cpp`)
///        addresses. The automation view's selection names it in the runtime's numbering, through the bridge.
using AutomationParam = deluge::project::PluginParam;

/// @brief The `SceneKind` this scene is registered under: `FirstApp` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kMenuSceneKind = deluge::ui::shell::SceneKind::FirstApp;

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
	/// @warning **`root` is dead weight for every caller that then pushes with `arg == 0`.**
	///          `GlobalControls::handle_settings_menu` always does, and `openGuest` (`gui/ui/menu_entry.cpp`)
	///          does when it is given no `arg` and arms no landing. `on_enter` re-roots unconditionally: from
	///          `node_by_id(arg)` when `arg != 0`, and otherwise from `ctx_.root` -- the copy this
	///          function just installed -- so whichever node was passed here is overwritten one
	///          call later. Measured, not reasoned: mutating
	///          `gui/ui/menu_entry.cpp`'s call to pass `roots[0]` instead of the resolved
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

	/// @brief Forget what the menu was open on, for its host's teardown of every UI: the song may go next, and the
	///        next `open()` installs a fresh context anyway. What the host published from the context goes with it
	///        (`install_context`).
	void forget_context() { install_context(MenuContext{}); }

	/// @brief Back to what a boot leaves: every submenu's remembered row forgotten (each legacy `Submenu` keeps its
	///        `current_item_` for the life of the firmware, and this scene is as long-lived), the horizontal encoder's
	///        acceleration and the voice-usage clock reset.
	void reset_process_state();

	deluge::ui::shell::SceneKind kind() const override;
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;
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
	/// @brief Select acts on its release, as `SoundEditor::buttonAction` does
	/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	/// @return True.
	bool select_acts_on_release() const override { return true; }
	/// @brief True where `SoundEditor::buttonAction`'s Select arm does not act: learn mode, and any UI mode the host
	///        reports outside the five SELECT acts in (`set_host_mode_gates`).
	bool select_release_blocked() const override { return !select_and_back_act(); }
	/// @brief Whether LEARN holds the menu in learn mode, legacy's `UI_MODE_MIDI_LEARN`, which the menu keeps itself
	///        rather than in `currentUIMode`; the guest refuses select turns in it.
	bool learn_mode() const { return visit_.learn.mode(); }
	void on_model_changed(const deluge::base::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief The main pads while the SHIFT overlay shows (`SoundEditor::renderMainPads` answers for them then);
	///        otherwise none, and the pads are the root's.
	deluge::ui::shell::PadRegions pad_regions() const override {
		return overlay_up() ? deluge::ui::shell::PadRegions::Main : deluge::ui::shell::PadRegions::None;
	}
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief `SoundEditor::padAction`'s gate in `origin/main:src/deluge/gui/ui/sound_editor.cpp`, as a declaration.
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

	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override;
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
	/// @brief Advance the voice usage page's sampling clock (`VoiceUsage::timerCallback`,
	/// `origin/main:src/deluge/gui/menu_item/cpu/voice_usage.h`).
	void tick(uint32_t dt_ms) override;
	/// @brief The voice usage page's counters.
	const VoiceUsageSampler& voice_usage() const { return voice_usage_.sampler(); }

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;

	/// @brief The value node the select/gold encoders would actually edit right now.
	///
	/// On a `Layout::Horizontal` submenu this is the focused column, *if* that column is not itself a submenu --
	/// `Submenu::shouldForwardButtons` (`origin/main:src/deluge/gui/menu_item/submenu.cpp`): legacy's same-param popup
	/// test (the guard in `View::modEncoderAction_existentParam`, `origin/main:src/deluge/gui/views/view.cpp`) forwards
	/// through a horizontal page to whichever item the buttons would actually reach, not the page itself. On any other
	/// leaf this is `focused()` unchanged -- there the leaf IS what the encoders edit. On a vertical submenu, or a
	/// horizontal page focused on a submenu column, there is nothing to edit and this is null.
	///
	/// Exists for `services::GuestKnobPresentation::suppress_value_popup`'s
	/// same-param test: `focused()` stops at the page itself on a horizontal menu, which is wrong by
	/// the same measure `shouldForwardButtons` names.
	const Node* focused_value() const;

	/// @brief Replace the facts about the root UI under the menu that the opener set: its greyout arm, and
	///        whether it is the keyboard screen or the automation view. The host calls it whenever the root, or
	///        the automation view's editor, changes while the menu is open; legacy re-reads them on every
	///        greyout (`SoundEditor::getGreyoutColsAndRows` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	void set_root_facts(MenuContext::RootGreyout greyout, bool root_is_keyboard, bool root_is_automation);
	/// @brief Re-root the open menu at @p root on @p ctx, landed on @p column, which @p root lists: `setup()` and
	///        `beginScreen` under the open sound editor (`enterNoteRowEditor` from inside it). The move is a `Replace`
	///        at the next tick, as a shortcut's jump is.
	void reroot(const Node& root, const MenuContext& ctx, const Node& column);
	/// @brief Re-root the open menu at @p root on @p ctx and land it as a fresh open lands: where @p shortcut lands
	///        (`arm_shortcut_landing`) when it is given, else on the node @p arg names, else (0) on the root itself.
	///        `setup()` on the open sound editor and `enterOrUpdateSoundEditor`'s already-open `beginScreen`, for an
	///        entry that reaches the menu while it is up. The move is a `Replace` at the next tick, as a shortcut's
	///        jump is.
	void reroot_at(const Node& root, const MenuContext& ctx, uint16_t arg, const Node* shortcut);
	/// @brief Whether, this visit, a scene the menu pushed for an item refused its enter (the browser or the recorder):
	///        legacy's failed `openUI(...)`, after which the drum creator opens the editor. Reset by `open`.
	[[nodiscard]] bool pushed_scene_refused() const { return pushed_scene_refused_; }
	/// @brief Whether AFFECT ENTIRE is this menu's hold rather than the clip's toggle: a kit row's menu
	///        (`editingKitRow()`) on an item that uses it (`menu::uses_affect_entire`).
	bool takes_affect_entire() const;
	/// @brief Enter or leave `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR`, which the host decides: while held a
	///        param write reaches every drum (`MenuContext::affect_entire_held`) and AFFECT ENTIRE blinks fast.
	void set_affect_entire_hold(bool on);
	/// @brief `SoundEditor::buttonAction`'s two UI-mode gates, as a host's own UI mode answers them at a button.
	struct HostModeGates {
		bool learn = true;           ///< `!currentUIMode`: a LEARN press may refuse, unlearn or start learn mode.
		bool select_and_back = true; ///< `currentUIMode` is exactly one of the five modes SELECT and BACK act in.
	};
	/// @brief Set by a host that keeps a UI mode the shell does not see, before it feeds each button. Unset, the
	///        menu's own learn mode alone gates SELECT and BACK, and held pads stand in for the mode at LEARN.
	void set_host_mode_gates(const HostModeGates& gates) { host_gates_ = gates; }
	/// @brief The clip's affect-entire changed under the open menu (the clip minder's toggle):
	/// `MenuContext::affect_entire`.
	void set_clip_affect_entire(bool on) { ctx_.affect_entire = on; }
	/// @brief The HorizontalMenus community feature changed under the open menu (`MenuContext::horizontal_menus`):
	///        legacy reads it on every draw, so a page entered after the write lays out as it now says.
	void set_horizontal_menus(bool on) { ctx_.horizontal_menus = on; }
	/// @brief The host's answer to "is a shortcut press on a kit's row now the kit's own effects", and the context
	///        that press resolves against: legacy's `setupKitGlobalFXMenu`, which `potentialShortcutPadAction`'s table
	///        (`get_basic_shortcut_action(editingKitAffectEntire())`), the `setup()` after it, the SHIFT overlay and
	///        the horizontal-menu chain all read live. Asked afresh at each of those, never stored.
	/// @param open The menu's context now.
	/// @return The kit's own effects while the host's latch is set, or `nullopt`.
	using KitGlobalShortcutFn = std::optional<MenuContext> (*)(const MenuContext& open);
	/// @brief Install the host's `KitGlobalShortcutFn`; null answers `nullopt` always.
	void set_kit_global_shortcut_source(KitGlobalShortcutFn source) { kit_global_shortcut_source_ = source; }
	/// @brief Run @p hook whenever the menu's context changes (`install_context`): every `open` and re-root, a shortcut
	///        press moving the menu onto the kit's own effects, `forget_context` and a song swap. The host republishes
	///        what the menu edits from it, before anything else of the tick reads the editor's context, as legacy's
	///        `setup()` re-points its editor at once.
	/// @param hook The host's adopt, or null for none.
	void set_context_adopted_hook(void (*hook)()) { context_adopted_hook_ = hook; }
	/// @brief Run @p hook after a shortcut jump from a scene over the menu (`shortcut_jump`, the marker editor's):
	///        legacy re-enters its editor there through `SoundEditor::enterOrUpdateSoundEditor`
	///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), whose write to `setupKitGlobalFXMenu` is the host's
	///        to make.
	/// @param hook The host's write, or null for none.
	void set_reentry_hook(void (*hook)()) { reentry_hook_ = hook; }

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
	/// The value is re-read and the cursor kept (`ValueSession::read_again`). Callers: the gold-knob push
	/// (`services::ModKnobs`), the note modes' re-reads, `View::displayAutomation` and the MPE refresh
	/// (`util/sound_menu_open.h`). A no-op when nothing is focused.
	void read_value_again();
	/// @brief The param a knob learn binds to on the learn node (`MenuLearn::node`): the row, column or item under the
	///        cursor.
	///
	/// @note **Param learn, not global-command learn.** This is `PatchedParam`/`UnpatchedParam`'s
	///       `getLearningThing()` (`bindings.h`'s `learn_target`): "which automatable parameter
	///       would a CC bind to". `CustomItem`'s learn hooks are the other learn path
	///       entirely -- `midi::Command`'s, which binds a whole MIDI *message* to a
	///       `GlobalMIDICommand` transport action and never touches a param. The twelve
	///       `midi_command_*` nodes have no `bind.param`, so this returns none for every one of them.
	std::optional<deluge::project::OwnerParamTarget> learn_target() const;

	/// @name MIDI learn -- `SoundEditor`'s learn members.
	///
	/// In learn mode a MIDI message reaches the learn node's item through `handle()` (`MenuLearn::offer`), as
	/// `getCurrentMenuItem()->learnX(...)` does, and `offer` decides whether the message was used; `unlearn` is
	/// `unlearnAction()`.
	///
	/// @warning **`wants_learn_blink()` is NOT the learn gate.** Legacy enters learn mode on
	///          `getCurrentMenuItem()->allowsLearnMode()` (`SoundEditor::buttonAction`'s LEARN arm), ported as
	///          `MenuLearn::allows_mode`, and only then asks `shouldBlinkLearnLed()` for the LED
	///          (`SoundEditor::buttonAction` and `SoundEditor::updatePadLightsFor`). They are different predicates over
	///          different sets: `allowsLearnMode` is true on every `MenuItemWithCCLearning` -- all the
	///          patched/unpatched params, the transpose items, the MIDI-follow items -- every one of which blinks
	///          *false*.
	/// @{
	/// @brief Ask the learn node's item to forget what it learned.
	void unlearn();
	/// @brief Does the focused item want the LEARN LED blinking? `MenuItem::shouldBlinkLearnLed`
	/// (`origin/main:src/deluge/gui/menu_item/menu_item.h`).
	///
	/// Read by `leds()`, which is the port of `SoundEditor::beginScreen`'s own tail (`SoundEditor::updatePadLightsFor`,
	/// reached from it). Public because the LED is also legacy's own answer in `SoundEditor::buttonAction`,
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
		MidiDeviceChoice,
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
	/// entry points resolve this context themselves (`gui/ui/menu_entry.cpp`), and two of its
	/// fields -- `is_session_view` and `is_performance` -- change nothing about which root is opened
	/// and everything about which items are relevant inside it. Without this accessor a caller
	/// passing the wrong one is invisible from outside the scene, so "the session and the arranger
	/// open the same root" could not be told apart from "the session and the arranger are the same
	/// thing". Read-only, like `state()`.
	///
	/// @note Spelled `menu_context` rather than `context` on purpose: CppSpec's header
	///       defines `context` as a macro (`#define context self.context`), so a member of that
	///       name is unusable from every spec in `tests/integration/ui/scenes` -- which is precisely where this
	///       accessor is meant to be read.
	const MenuContext& menu_context() const { return ctx_; }

	/// @brief Collect the root-UI action a menu item asked for, if any, and clear it.
	///
	/// The application-facing half of the action seam (`ui/scenes/menu/action.h`). `after_render`
	/// drains `MenuWalkState::pending_action` into a one-slot outbox here -- beside where it drains
	/// `pending_scene` into a `Push`, and for the same reason: an item's hook has no `Result` to put
	/// anything in. `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`) polls this
	/// after ticking the shell, and is where each verb's legacy statement lands.
	///
	/// @note **Every one of these actions is performed.** The consumer polls once per guest tick and
	///       has a body for each: `OpenPerformanceView`, `StartStemExport` and the three one-line verbs
	///       (`SetClipLengthToSampleLength`, `OpenSongMidiLearn`, `OpenSongMacros`). A settings write's
	///       side effect is no action: its setter calls the host through a port (`ui/scenes/bind/ports/`).
	///
	/// @warning **This is a one-slot mailbox with a silent overwrite, not an exactly-once queue.**
	///          Two requests filed between two polls lose the first, and a request nobody polls for
	///          is delivered whenever someone finally does -- arbitrarily later, against whatever
	///          state exists then. `after_render` returns no `Action` announcing that one is
	///          waiting, so nothing enforces the polling: **poll after every tick, or requests are
	///          dropped or stale.** One route fills it, a `CustomItem` writing `pending_action` from
	///          `on_focus` or `render`; each runs at most once per `after_render`, and no shipped item
	///          files from both, so no shipped item can lose a request. A queue would need an overflow
	///          policy of its own, or an `Action` kind that carries a root-UI call into `ui/shell/scene.h`;
	///          the mailbox and this warning are the smaller answer.
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
	/// @brief A folder import from the browser this menu opened made range @p index of the menu's source current, as
	///        `SoundEditor::setCurrentMultiRange` does. The resume that brings the menu back applies it before its step
	///        up, and the level the step lands on keeps or drops it, as main's does (`begin_submenu_range`). Dropped by
	///        `open`.
	void import_chose_range(int32_t index) { import_range_ = static_cast<int16_t>(index); }
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

	/// @brief Re-root the menu onto the horizontal page or group that lists `MenuWalkState::scene_opener`, with
	///        that item focused -- the sample claim (`SampleClaim`)'s return affordance
	///        (`SampleBrowser::claimCurrentFile` in `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`).
	///
	/// **The one thing `MenuState::path`/`depth` may be written for from outside this class**, and it exists because
	/// legacy's answer to "where does accepting a file land you" is spelled in two `MenuItem*`s no port carries.
	/// Legacy's three statements in `SoundEditor::setup`, in order, and what each becomes:
	///
	///   * `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` -> `columns[0].focused`
	///     set to the item's index among the page's visible children. Legacy's `focusChild` writes
	///     `Submenu::current_item_`, a member of the page object (`origin/main:src/deluge/gui/menu_item/submenu.h`);
	///     the port's focus is per *depth* (`MenuState::columns`), so moving the page to depth 0
	///     is what makes this a separate statement rather than something the move carries along.
	///   * `soundEditor.menuItemNavigationRecord[0] = parentMenuHeadingTo` -> `path[0]`.
	///   * `soundEditor.navigationDepth = 0` -> `depth = 0`. This is the whole of the
	///     "one extra BACK press" it would otherwise cost: without it the user is left on the file
	///     selector at whatever depth it was opened from, one level further in than legacy leaves
	///     them -- and, on the shortcut-pad route, out of the menu altogether.
	///
	/// @par Why the page is resolved here rather than carried in
	///
	/// Legacy writes `parentMenuHeadingTo` on the way in because `SoundEditor::maybeGetParentMenu`
	/// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) is the only place that has the chain in scope. Its body is
	/// `find_if(chain, [](HorizontalMenu* m) { return m->hasItem(item); })` over
	/// `getCurrentHorizontalMenusChain(false)` (which dispatches on the root).
	/// This scene has the root (`MenuContext::root`) and the chains (`chain_for`), so it answers the
	/// same question: the first chain entry that lists the item, which for an oscillator's file
	/// selector is the whole `source_group` (`sourceMenuGroup`), paged across its five members. A
	/// root whose chain lists the item nowhere falls back to the horizontal page in its subtree that
	/// does (`horizontal_page_listing`). Carrying a resolved page in would have meant paying that
	/// search on every scene push, including the four that have no return affordance at all.
	///
	/// @par Why an immediate mutator
	///
	/// Its caller is the application, between ticks (`gui::ui::shellSampleClaimDone`), and the shape
	/// for that is `open()` -- which already installs a `MenuContext` and re-roots `path`/`depth`
	/// from `gui/ui/menu_entry.cpp` with no mailbox in sight. A mailbox would also be wrong
	/// rather than merely heavier: a pending re-root would sit in the field until the next
	/// `after_render`, one frame later, and `retarget` clears the mailboxes (see its body), so any
	/// re-root filed across a re-root would be silently dropped.
	///
	/// @warning Re-rooting abandons the walk that reached the item, exactly as `retarget` does for
	///          `open()` and the shortcut-pad jump -- every per-visit mailbox goes with it.
	///
	/// @return True if the menu moved. False when there is nothing to move: the `HorizontalMenus` feature off
	///         (`MenuContext::horizontal_menus`), no scene opener recorded, no path (`on_model_changed`'s song swap), a
	///         root out of range, or an item no horizontal page in this root lists. On success it raises
	///         `Dirty::Oled`, since its caller runs between the shell's ticks.
	bool reroot_to_scene_opener_page();

	/// @brief Where the menu goes when it resumes from a recorder push that refused, in place of going up: legacy's
	///        `enterDrumCreator`, whose fallback after a refused `openUI(&audioRecorder)` opens the editor on the file
	///        selector. Spent by the next resume that follows a push; dropped by `open` and `reset_process_state`.
	/// @param landing The node to land on.
	void arm_refused_recorder_landing(const Node& landing) { refused_recorder_landing_ = &landing; }

	/// @brief Where a Shift + main pad from a scene the menu opened (the sample marker editor) jumps:
	///        `SoundEditor::potentialShortcutPadAction` asked from outside the editor, on the plain shortcut table
	///        (the second layer and the patching-source column are the editor's own screen's).
	///
	/// Arms the landing exactly as the menu's own Shift + pad does, so the `Replace` the caller files lands the
	/// same way (`on_enter`). An item the context refuses raises "PARAMETER NOT APPLICABLE", as legacy's `setup`.
	/// Unlike the menu's own Shift + pad, a non-param item is taken over the automation view too: main makes that
	/// refusal only while its editor is the current UI.
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

	/// @brief Prepare the next `on_enter` to open @p page with @p column focused: `reroot`'s landing, for an open
	///        that goes through the shell's own `SceneId` (`open()` must already have run).
	/// @return The `SceneId::arg` to open: @p page's id.
	uint16_t arm_page_landing(const Node& page, const Node& column);

	/// @brief Do SELECT and BACK act now? Legacy's five-mode test, of which learn mode (`UI_MODE_MIDI_LEARN`) is not
	///        one.
	bool select_and_back_act() const { return !learn_mode() && (!host_gates_ || host_gates_->select_and_back); }
	/// @brief The inputs `render_oled` claims for the page focused now: the claim sites of `render_oled` and the
	///        `render_*` functions it dispatches to, answered before they run.
	/// @return The claim set.
	deluge::ui::widgets::core::InputClaims owned_input() const;
	/// @brief The focused horizontal page's paging (empty off one): the page counter and the SCALE/CROSS_SCREEN rule
	///        read the same.
	deluge::ui::widgets::oled::Paging horizontal_paging() const { return menu::horizontal_paging(ctx_, st_); }
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
	/// @brief The kit's own effects for a shortcut press on a kit's row root: the menu's own context when it was opened
	///        on them, else the host's (`set_kit_global_shortcut_source`); `nullopt` off a kit row or while the host's
	///        latch is clear. The note editors and Settings take no
	///        shortcut press, as `SoundEditor::padAction` skips them.
	std::optional<MenuContext> kit_global_shortcut_context() const;
	/// @brief The root whose shortcut table and horizontal chain a press reads: the kit's own while
	///        `kit_global_shortcut_context` answers, else the menu's.
	MenuContext::Root shortcut_root() const;
	/// @brief Make @p ctx the menu's context and hand it to the host (`set_context_adopted_hook`). Every write of the
	///        context goes through here.
	void install_context(const MenuContext& ctx);
	/// @brief The child the current layout has focus on, or `nullptr`.
	const Node* selected_child(const Node& node) const;
	bool on_horizontal_page() const { return menu::on_horizontal_page(ctx_, st_); }
	bool is_horizontal(const Node& node) const { return menu::is_horizontal(ctx_, node); }
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target), and begin the voice usage
	///        page's session afresh.
	void retarget(const Node& node);
	/// @brief Re-root onto the horizontal page or group that lists @p item, focused on it -- the body
	///        `reroot_to_scene_opener_page` and the recorder's return share.
	/// @param item The item to land on.
	/// @return As `reroot_to_scene_opener_page`.
	bool reroot_to_page_listing(const Node& item);

	/// @brief `toggleValue()`: write @p node's logical negation. A list row and a horizontal column flip alike.
	void flip_in_place(const Node& node);

	/// @brief `SoundEditor::markInstrumentAsEdited` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) after a select
	///        turn: the sound's instrument (a synth, a kit row's kit) when the menu edits a sound, outside the settings
	///        menu, and the focused item edits the instrument (`select_turn_edits_instrument`).
	void mark_edited_after_turn();

	/// @brief After a learn or an unlearn: when the focused screen is a value leaf that learns its own value (a follow
	///        channel, the kit root note), read the value again. `FollowChannel::learnCC`/`learnNoteOn`/`unlearnAction`
	///        and `FollowKitRootNote::learnNoteOn`
	///        (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_kit_root_note.h`) call `setValue` as well as
	///        storing the input, and redraw.
	void read_learned_value_again();

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
	/// @brief The host's live kit-global answer (`set_kit_global_shortcut_source`).
	KitGlobalShortcutFn kit_global_shortcut_source_ = nullptr;
	/// @brief The host's hook, run on every change of the context (`set_context_adopted_hook`).
	void (*context_adopted_hook_)() = nullptr;
	/// @brief The host's write after a jump from a scene over the menu (`set_reentry_hook`).
	void (*reentry_hook_)() = nullptr;
	/// @brief The host's UI-mode gates (`set_host_mode_gates`); unset, the menu's own learn mode alone gates.
	std::optional<HostModeGates> host_gates_;
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
	/// `import_chose_range`'s range, until the next resume spends it; -1 for none.
	int16_t import_range_ = -1;
	/// `arm_refused_recorder_landing`'s node, until the next resume from a push spends it.
	const Node* refused_recorder_landing_ = nullptr;
	/// The kind of the scene the last `pending_scene` drain pushed, until the resume that follows it.
	deluge::ui::shell::SceneKind pushed_kind_ = deluge::ui::shell::SceneKind::Null;
	/// `pushed_scene_refused`'s answer.
	bool pushed_scene_refused_ = false;
	/// A node the next `after_render` re-targets the menu to with a `Replace` (`dx_column_press`), or 0 for none.
	uint16_t pending_retarget_ = 0;
	/// The host's hold mode (`set_affect_entire_hold`): survives a shortcut's `Replace`, as legacy's UI mode does.
	bool holding_affect_entire_ = false;
	/// Restart the fast blink at the next `leds`: the press, and a select or BACK press or a select turn while held.
	bool affect_entire_blink_restart_ = false;
	/// The clock of the last grid render, which `next_timer` measures the DX column's next flip from.
	uint32_t render_ms_ = 0;
	/// Whether the keyboard DX column flashes: the parameter page over the keyboard, drawn at least once.
	bool dx_column_flashing() const;

	/// @brief What belongs to one open of the scene: reset by value in `open()` and `on_enter()`, which is legacy's
	///        `beginScreen` -- a new screen.
	struct Visit {
		/// @brief Use up the current Select press at the next `after_render`: legacy's `SoundEditor::opened` and
		///        `focusRegained` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), so the release of a press that
		///        opened or returned to the menu does not act.
		bool use_up_select = true;
		/// Whether the next `leds()` restarts the shared normal blink, BACK's and LEARN's (`blinkLed`), and the screen
		/// the render last checked that against (`beginScreen` restarts it for a LEARN-blinking item).
		bool blink_restart = true;
		/// Learn mode and its LED.
		MenuLearn learn;
		/// @brief Select was held during the last render: a value edited under it uses the press up
		///        (`Value::selectEncoderAction`, `origin/main:src/deluge/gui/menu_item/value.h`).
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
		/// A MIDI device-definition browser opened over this visit: LOAD blinks on under the menu, since the sound
		/// editor's `setLedStates` never stops the blink its `LoadUI` started; leaving the menu does (`View`).
		bool load_blink = false;
	};
	Visit visit_{};
	/// The walk through the tree, and the rows each submenu was last left on (process lifetime).
	MenuWalk walk_;

public:
	/// @name The two navigation steps a caller outside the scene takes
	///
	/// They belong beside `read_value_again()` in spirit.
	/// @{
	/// @brief One level up the path -- `SoundEditor::goUpOneLevel`
	///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), for a caller outside the scene.
	///
	/// For the note-mode handlers that call `soundEditor.goUpOneLevel()` themselves
	/// (`gui/views/instrument_clip_view.cpp`'s `nudgeNotes`, and `SoundEditor::toggleNoteEditorParamMenu`'s
	/// release arm). The step is `ascend()`, which is already this scene's port of `SoundEditor::goUpOneLevel`'s
	/// permission loop and of its closing `beginScreen(oldItem)`.
	///
	/// @return **false where legacy exits completely instead**, having moved nothing in the first case:
	///         the loop's `if (navigationDepth == 0) { exitCompletely(); return; }` fires either on the first
	///         pass, at depth 0, or on a later pass, when the loop has stepped down to depth 0 and the permission
	///         check refuses that level too. Closing the whole editor is the
	///         application's act, not the scene's -- `gui::ui::closePortedMenu()` -- so the caller does it
	///         on false. `ascend()` at depth 0 asks for the scene's own close instead, which these callers
	///         do not want, so this takes that request back on false.
	bool go_up_one_level();
	/// @brief Push @p node one level below the focused node -- `SoundEditor::enterSubmenu`
	///        (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), for a caller outside the scene.
	///
	/// For `SoundEditor::toggleNoteEditorParamMenu`'s press arm. Legacy's body is `endScreen()`, `navigationDepth++`,
	/// the record write and `beginScreen(navigatedBackwardFrom)` (`SoundEditor::enterSubmenu`), and `beginScreen`
	/// begins the item's session without asking `checkPermissionToBeginSession` (`SoundEditor::beginScreen`) -- so this
	/// is `descend_to`, which asks no permission either, and nothing more. **`menu_context()` is untouched**: unlike
	/// `open()`, this never replaces `ctx_`.
	///
	/// @note **A recorded divergence: the focus the entered page lands on.** Legacy's `beginScreen` begins the entered
	///       item's session (`SoundEditor::beginScreen`), and the toggle names no item to navigate back from
	///       (`SoundEditor::toggleNoteEditorParamMenu`). For a `HorizontalMenu` that is `HorizontalMenu::beginSession`
	///       (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`) -> `Submenu::beginSession`
	///       (`origin/main:src/deluge/gui/menu_item/submenu.cpp`) -> `focusChild(nullptr)`, which keeps the
	///       process-lifetime object's `current_item_`: a second toggle hold lands on the column last focused.
	///       `descend_to` resets `st_.list[depth]` and `st_.columns[depth]` (`MenuWalk::descend_to`, `menu/walk.cpp`),
	///       so this always lands on the first. The port keeps focus per depth for a session, not per menu object for
	///       the process, and meets it by a descend rather than a re-open.
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

} // namespace deluge::ui::scenes::menu
