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
/// `preparePaging` (`gui/menu_item/horizontal_menu.cpp:477`, called at `:114` and `:378`) -- all four already
/// pinned by the `list` / `columns` widgets, which this scene composes rather than re-implements;
/// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp:1072`), `buttonAction` (`:371`,
/// back -> `goUpOneLevel` `:710`, which exits at depth 0 via `exitCompletely` `:716`),
/// `horizontalEncoderAction` (`:1046`), `padAction` (`:1460`) with `potentialShortcutPadAction`
/// (`:1166`), `enterSubmenu` (`:362`), `setupShortcutBlink` (`:997`) / `blinkShortcut`
/// (`:1012-1038`), `getGreyoutColsAndRows` (`:252`), and the `MultiRange` redirect -> spec E
/// §4.1's `Permission::MustSelectRange`.
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::oled` widget, so
/// the layout rules live in one place and Task 12's ratchet can count primitive calls at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "deluge-project/mapping_table.h"
#include "gfx/oled/icon.h"
#include "notify/dirty_set.h"
#include "scenes/menu/action.h"
#include "scenes/menu/chains.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"
#include "scenes/menu/present.h"
#include "scenes/menu/shortcut_blink.h"
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

/// @brief The debounce shared by SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page --
///        `HorizontalMenu::buttonAction`'s `navigation_buttons_debounce_threshold`
///        (`horizontal_menu.cpp:78`), 0.14 s as milliseconds. A press is accepted only when
///        strictly more time than this has passed since the last accepted one
///        (`MenuState::nav_press_ms`), matching legacy's `time - last > threshold` (`:81-82`)
///        rather than `>=`.
inline constexpr uint32_t kNavButtonDebounceMs = 140;

/// @brief `cpu::VoiceUsage`'s counters (`cpu/voice_usage.h:94-111`): the last sample, the maximum, and a whole-number
///        average over every sample since the page opened.
class VoiceUsageSampler {
public:
	/// @brief Forget every sample (`VoiceUsage::beginSession`, `:70-73`).
	void reset() { *this = {}; }
	/// @brief Take one sample of @p live voices (`VoiceUsage::sample`, `:94-99`).
	void sample(int32_t live) {
		live_ = live;
		total_ += static_cast<uint64_t>(std::max<int32_t>(live, 0));
		count_++;
		maximum_ = std::max(maximum_, live);
	}
	[[nodiscard]] int32_t live() const { return live_; }
	[[nodiscard]] int32_t maximum() const { return maximum_; }
	/// @brief `averageVoiceCount` (`:101-103`): 0 before any sample.
	[[nodiscard]] int32_t average() const { return count_ == 0 ? 0 : static_cast<int32_t>(total_ / count_); }
	[[nodiscard]] uint32_t samples() const { return count_; }

private:
	int32_t live_ = 0;
	int32_t maximum_ = 0;
	uint64_t total_ = 0;
	uint32_t count_ = 0;
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
	///          call later. Measured, not reasoned: F1 task 9 mutated
	///          `gui/ui/shell_menu_entry.cpp`'s call to pass `roots[0]` instead of the resolved
	///          root and **nothing in the suite noticed**. The parameter is kept because this is
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
	/// @brief `reset_process_state`.
	void forget_rows() { reset_process_state(); }

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
	bool select_release_blocked() const override { return visit_.learn_mode; }
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief `SoundEditor::padAction`'s gate at `gui/ui/sound_editor.cpp:1467`, as a declaration.
	///
	/// Three predicates there, `!inSettingsMenu() && !inNoteEditor() && !inNoteRowEditor()`, decide
	/// whether `potentialShortcutPadAction` even runs; on any of the three failing, every pad --
	/// shift or not -- falls through to the forwarding switch and the tail instead. `inSettingsMenu`
	/// is `Root::Settings`; Task 6 gives the other two their roots.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return (ctx_.root == MenuContext::Root::Settings || ctx_.root == MenuContext::Root::NoteEditor
		        || ctx_.root == MenuContext::Root::NoteRowEditor)
		           ? deluge::ui::shell::GridPadClaim::None
		           : deluge::ui::shell::GridPadClaim::ShortcutsOnly;
	}
	/// @brief False for every role: a menu covers the Main panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	// `input_transparent()` stays at its default false: `MenuScene` IS the `input_top()`
	// (`shell/scene.h:445`), which is what makes its frames the focused ones.

	deluge::ui::grid::GreyoutMask greyout_mask() const override;
	void save_state(deluge::ui::shell::SceneState& state) const override;
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief Grid only (E3 task 10 fix round 1): `next_timer()`'s deadline here is purely the
	///        shortcut/source pad blink cadence -- a grid concern -- and the submenu's OLED content
	///        never depends on it. Arming the OLED gates from it too (the fold's original default)
	///        cost nothing in pixels but blitted both panels unconditionally, forever, whenever the
	///        focused node had shortcut or source pads. See `Scene::timer_repaints()`'s own doc.
	/// On the voice usage page the timer is also its 500 ms sample, which the panel shows.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override;
	/// @brief Advance the voice usage page's sampling clock (`VoiceUsage::timerCallback`, `cpu/voice_usage.h:77-87`).
	void tick(uint32_t dt_ms) override;
	/// @brief The voice usage page's counters.
	const VoiceUsageSampler& voice_usage() const { return voice_usage_; }

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;

	/// @brief The value node the select/gold encoders would actually edit right now.
	///
	/// On a `Layout::Horizontal` submenu this is the focused column, *if* that column is not
	/// itself a submenu -- `Submenu::shouldForwardButtons` (`gui/menu_item/submenu.cpp:181-185`):
	/// legacy's same-param popup test (`View::modEncoderAction_forSomeReason`'s guard,
	/// `gui/views/view.cpp:893-900`) forwards through a horizontal page to whichever item the
	/// buttons would actually reach, not the page itself. On any other leaf this is `focused()`
	/// unchanged -- there the leaf IS what the encoders edit. On a vertical submenu, or a
	/// horizontal page focused on a submenu column, there is nothing to edit and this is null.
	///
	/// Added for `services::ModKnobs::popup_suppressed` (`scenes/services/mod_knobs.cpp`), whose
	/// same-param test named `focused()` before this task and forwarded no further than the page
	/// itself on a horizontal menu -- wrong by the same measure `shouldForwardButtons` names.
	const Node* focused_value() const;

	/// @brief Re-read the focused node's value from the model -- `MenuItem::readValueAgain`.
	///
	/// The port of the one call legacy makes to it from outside the menu:
	/// `View::modEncoderButtonAction_changeModControllable`'s
	/// `soundEditor.getCurrentMenuItem()->readValueAgain()` (`gui/views/view.cpp:1391-1393`), run
	/// after a gold-knob **push** has changed a `ModControllable` param behind the menu's back. The
	/// edit storage this scene keeps (`MenuState::edit_int`, `enum_index`, …) is loaded on focus,
	/// so without this the menu would keep showing the value it read on the way in.
	///
	/// Spelled as `enter_leaf(node, FocusCause::NoRowPress)` because that is exactly what
	/// `readValueAgain` is -- reload the storage, with nothing pressed on this row (legacy's
	/// `beginScreen`/`focusRegained` shape). A no-op when nothing is focused.
	void read_value_again();
	/// @brief The `deluge::synth::CableTarget` MIDI/CC learn (E3) binds to the focused node.
	///
	/// @note **Param learn, not global-command learn.** This is `PatchedParam`/`UnpatchedParam`'s
	///       `getLearningThing()` (`bindings.h`'s `learn_target`): "which automatable parameter
	///       would a CC bind to". The `learn_*` entry points below are the other learn path
	///       entirely -- `midi::Command`'s, which binds a whole MIDI *message* to a
	///       `GlobalMIDICommand` transport action and never touches a param. The twelve
	///       `midi_command_*` nodes have no `bind.param`, so this returns none for every one of them.
	std::optional<deluge::project::OwnerParamTarget> learn_target() const;

	/// @name MIDI-learn entry points -- the port of `SoundEditor`'s own three delivery members.
	///
	/// `SoundEditor::noteOnReceivedForMidiLearn` / `midiCCReceived` / `pcReceivedForMidiLearn`
	/// (`gui/ui/sound_editor.cpp:1605-1625`) each forward to `getCurrentMenuItem()->learnX(...)`
	/// and report whether the message was used; `unlearn` is `unlearnAction()` at `:478`. These do
	/// the same, to `focused()`'s `CustomItem` -- the item decides, exactly as legacy's virtual
	/// dispatch let it.
	///
	/// @warning **Nothing calls these yet.** Routing MIDI into the scene stack belongs to the
	///          wiring plan; legacy's `currentUIMode == UI_MODE_MIDI_LEARN` gate and its shift
	///          exclusion (`:1550`, `:1562`) are that plan's, not this layer's -- a scene may not
	///          read a legacy UI mode. What is here is the half a menu owns: which item the message
	///          reaches, and what that item does with it.
	///
	/// @warning **`wants_learn_blink()` is NOT the learn gate.** Legacy enters learn mode on
	///          `getCurrentMenuItem()->allowsLearnMode()` (`sound_editor.cpp:511`) and only then
	///          asks `shouldBlinkLearnLed()` for the LED (`:488`, `:911-916`). They are different
	///          predicates over different sets: `allowsLearnMode` is true on every
	///          `MenuItemWithCCLearning` -- all the patched/unpatched params, the transpose items,
	///          the MIDI-follow items -- every one of which blinks *false*. A wiring author who
	///          gates delivery on `wants_learn_blink()` would silently drop param CC learn and its
	///          `CANT_LEARN` popup along with it. The port has no `allows_learn_mode()` hook yet
	///          because nothing needs one until that plan exists; adding it is that plan's first
	///          job, not a synonym for this.
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
	/// (`sound_editor.cpp:949-954`). Public because the LED is also legacy's own answer at `:488`,
	/// on the way *out* of learn mode -- **not** because it is the gate into it. See the second
	/// `@warning` above: that gate is `allowsLearnMode()`, which this is not a synonym for.
	bool wants_learn_blink() const;
	/// @}
	/// @brief The application's root menus, indexed by `MenuContext::Root`.
	///
	/// @note All twelve entries are filled in (`menu_scene.cpp`'s `kRoots`, Tasks 6-8): the sound
	///       root, the drum/MIDI-drum/gate-drum roots, MIDI-or-CV, the audio-clip, performance,
	///       song and kit-global-FX roots, the settings root, and (Task 6) the note-editor and
	///       note-row-editor roots. `on_enter(arg == 0)` therefore resolves a real root from
	///       `MenuContext::root`; a caller may still name a node directly through `open()` or
	///       through `SceneId::arg`.
	static std::span<const Node* const> roots();

	/// @brief The live interpreter state.
	///
	/// Exposed for specs and for E2's range picker (which has to see `range_picker_pending`);
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
	/// @return The arms the last `after_render` acted on, in the order they acted: the witness of the drain order.
	std::span<const DrainBand> drain_trace() const { return {drain_trace_.data(), drain_trace_len_}; }

	/// @brief What the menu is currently editing -- the `MenuContext` the last `open()` installed.
	///
	/// Added by F1 task 9, and the reason is a real gap rather than convenience: the application's
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
	/// @note **All seven of these actions are performed as of F2 task 7.** The consumer arrived with
	///       F2 task 1 and polls once per guest tick; task 2 gave `OpenPerformanceView` a body,
	///       task 3 `StartStemExport`, and task 4 the three one-line verbs
	///       (`SetClipLengthToSampleLength`, `OpenSongMidiLearn`, `OpenSongMacros`). Tasks 6 and 7
	///       then added two enumerators that did not exist when this note first said "five" --
	///       `SetShortcutsVersion` and `ScreensaverSettingsChanged` -- each with its arm. What each
	///       arm still owes is narrower than "an empty arm" -- it is a named residue per arm -- and
	///       is recorded at the arms themselves (`gui/ui/shell_action_dispatch.h`) rather than here.
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
	///          `shell/scene.h`. An honest mailbox plus this warning was judged better than a
	///          guarantee that only half holds. **No shipped item can lose a request today, but the
	///          reason is narrower than it used to be and the arithmetic does not carry it.** This
	///          warning used to argue "one tick delivers at most one select press", which was a
	///          complete argument while a select press was the only route in. It is not any more.
	///          There are two: a `CustomItem` writing `pending_action` from `on_focus`/`render`, and
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

	/// @brief Re-root the menu onto the horizontal page that lists `MenuWalkState::scene_opener`, with
	///        that item focused -- the sample claim (`SampleClaim`)'s return affordance
	///        (`gui/ui/browser/sample_browser.cpp:1116-1118`).
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
	///     "one extra BACK press" this port used to cost: without it the user is left on the file
	///     selector at whatever depth it was opened from, one level further in than legacy leaves
	///     them -- and, on the shortcut-pad route, out of the menu altogether.
	///
	/// @par Why the page is resolved here rather than carried in
	///
	/// Legacy writes `parentMenuHeadingTo` on the way in because `SoundEditor::maybeGetParentMenu`
	/// (`gui/ui/sound_editor.cpp:2073`) is the only place that has the chain in scope. Its body is
	/// `find_if(chain, [](HorizontalMenu* m) { return m->hasItem(item); })` (`:2084`) over
	/// `getCurrentHorizontalMenusChain(false)` (`:2079`, dispatched on the root at `:2108-2130`).
	/// This scene has the root (`MenuContext::root`) and the tables, so it answers the same
	/// question by walking the current root's own subtree for the `Layout::Horizontal` submenu that
	/// lists the item -- which is `hasItem` with the chain spelled as reachability instead of as a
	/// curated `deluge::vector` (`gui/ui/menus.cpp:2270`). Carrying a resolved page in would have
	/// meant paying that walk on every scene push, including the four that have no return
	/// affordance at all.
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
	/// @brief True when `focused()` is a `Layout::Horizontal` submenu -- the gate
	///        `HorizontalMenu::buttonAction`'s six-button arm ports (`handle()`'s button code).
	bool on_horizontal_page() const;
	/// @brief True when @p node renders and edits as a horizontal page: a `Layout::Horizontal` submenu with
	///        `MenuContext::horizontal_menus` on (`HorizontalMenu::renderingStyle()`).
	bool is_horizontal(const Node& node) const;
	/// @brief SHIFT + CROSS_SCREEN/SCALE at depth 0: replace the page with the next (or previous) menu of the root's
	///        chain that has columns, landing on its first page -- mod FX's second -- as `switchHorizontalMenu` does.
	/// @param direction +1 or -1.
	/// @return Whether the page changed.
	bool switch_chained_page(int32_t direction);

public:
	/// @brief The focused horizontal page's paging (empty off one): the page counter and the SCALE/CROSS_SCREEN rule
	///        read the same.
	/// @return Page, total pages and position, as `ui::oled::paging_of` answers for the focused column.
	deluge::ui::oled::Paging horizontal_paging() const;
	/// @brief The focused page's title: on a group (`Node::is_group`) the focused column's member's, as
	///        `HorizontalMenuGroup::getTitle` (`horizontal_menu_group.cpp:77-79`).
	/// @param scratch Storage the returned view may point into.
	/// @return The title text.
	std::string_view page_title(NodeText& scratch) const;

private:
	/// @brief Turn @p node's visible children into a `Column` span carrying label, slots,
	///        relevance and `is_submenu` -- the part of a horizontal page's presentation
	///        `handle()`'s button code needs as well as `render_horizontal`'s full render, factored
	///        so the two read one list rather than two copies of the same rule
	///        (`kid.relevant == nullptr || kid.relevant(ctx_)`, `kid.kind == Kind::Submenu`).
	///        `render`/`ctx` are left null; only the renderer fills those in, and only when
	///        actually drawing.
	/// @param node          The horizontal submenu to list.
	/// @param kids          Written with @p node's visible children, parallel to @p cols.
	/// @param cols          Written with one `Column` per child.
	/// @param label_scratch Backing storage for each column's label text; must outlive @p cols.
	/// @return How many children were written (`visible_children`'s own count).
	size_t build_column_list(const Node& node, std::span<const Node*> kids, std::span<deluge::ui::oled::Column> cols,
	                         std::span<NodeText> label_scratch) const;
	/// @brief Emit the horizontal-page notification for @p column -- the pair
	///        `HorizontalMenu::displayNotification` prints (`horizontal_menu.cpp:335-343`), name
	///        and formatted value on one line. Shared by `after_render`'s value-edit arm and
	///        `handle()`'s button-focus code, which is where legacy's own three call sites are:
	///        `selectEncoderAction` (`:328`), `handleInstrumentButtonPress`'s new-focus tail
	///        (`:437`), and `handleItemAction`'s plain-column arm (`:666`). `handleItemAction`'s
	///        other arm -- a submenu, or an `allowToBeginSessionFromHorizontalMenu` column, already
	///        focused -- calls `enterSubmenu` instead (`:677`) and never reaches
	///        `displayNotification` at all; that is `enter_horizontal_item`, not this method.
	void notify_column(const Node& column);
	/// @brief `checkPermissionToBeginSessionForRangeSpecificParam`'s allocation
	///        (`sound_editor.cpp:1993-1997`) plus legacy's `MUST_SELECT_RANGE` substitution --
	///        `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`
	///        (`:1857-1858`) -- the one piece `enter_child` (a descent) and `handle()`'s button
	///        focus (legacy's `initializeItem` call, `horizontal_menu.cpp:424`, `:669`) both run,
	///        because both are ways into `MenuItem::checkPermissionToBeginSession`'s range branch.
	///
	///        **Not `Permission::No`.** `enter_child`'s own switch still owns that arm: a descent
	///        refuses outright on `No`, but `HorizontalMenu::handleInstrumentButtonPress` never
	///        consults `initializeItem`'s return for anything but `MUST_SELECT_RANGE` -- a button
	///        focus proceeds regardless of `Yes`/`No`, which is why this helper answers only the
	///        range question and leaves `No` to whichever caller cares.
	/// @param child               The node a session is about to begin on.
	/// @param suppress_when_allowed When true, and `child.allow_session_from_horizontal` is set,
	///         skip the `MUST_SELECT_RANGE` check entirely and answer false -- the port of
	///         `handleInstrumentButtonPress`'s own extra condition
	///         (`!(*current_item_)->allowToBeginSessionFromHorizontalMenu()`, `:427-428`), which
	///         no other caller of this helper has. See `Node::allow_session_from_horizontal`'s own
	///         doc for the two legacy call sites and why only one of them suppresses.
	/// @return True if @p child must not be entered/focused as itself this tick -- the range
	///         picker was opened instead, or the allocation failed and a notice was shown. False
	///         (permission `Yes` or `No`, or the range check was suppressed) means the caller
	///         proceeds with its own next step.
	bool enter_range_picker_if_needed(const Node& child, bool suppress_when_allowed);
	/// @brief `HorizontalMenu::handleItemAction`'s else-path (`horizontal_menu.cpp:669-677`): a
	///        submenu, or an `allow_session_from_horizontal` value column, pressed while already
	///        focused. Runs the range check unconditionally (unlike the button-focus arm) and then
	///        always enters -- `handleItemAction` never reads `Permission::No` at all, unlike
	///        `enter_child`'s general descend.
	/// @param child The already-focused column being pressed again.
	void enter_horizontal_item(const Node& child);

	void render_vertical(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_horizontal(deluge::ui::oled::OledFrame& f, const Node& node);
	// One helper per kind: each owns the widget's own argument shapes rather than sharing an
	// `edit_value` the widgets do not agree on.
	void render_leaf_int(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_decimal(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_enum(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_toggle(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_sync_level(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_range(deluge::ui::oled::OledFrame& f, const Node& node);

	/// @brief Why a leaf is being focused -- the discriminator legacy spells as
	///        `soundEditor.getCurrentMenuItem() == this` (`gui/menu_item/toggle.h:35-37`).
	///
	/// Legacy reaches a bespoke item's work by two routes and treats them differently:
	/// `selectButtonPress()` (a press on the parent's row) *acts*, while `beginSession()` -- which
	/// `focusRegained` -> `beginScreen` also runs on every re-entry, `sound_editor.cpp:330`,
	/// `:1015` -- only (re-)initialises. `CustomItem::on_focus` is `beginSession`, so a port that
	/// ran it on every focus transition ran *both* meanings, and an item whose `on_focus` stands in
	/// for a press fired without one. This is that lost distinction, restored.
	/// **The question is "a select press on THIS node's own row?", not "a press at all"** (E3 final
	/// review, A6). The values used to be `Descended`/`Restored`, and `Restored` was a lie for one
	/// of the routes it covers: a **shortcut-pad jump is a real press**, just not one on this node's
	/// row. The behaviour was right -- legacy reaches a shortcut jump through `beginSession`, never
	/// `selectButtonPress` -- so the names moved to say what the two values have always meant,
	/// rather than a third cause being invented for a distinction nothing acts on.
	/// `CustomItem::on_focus_is_press()`'s whole contract is then readable from this enum alone:
	/// such an item is given `on_focus` for `RowPress` and withheld it for `NoRowPress`.
	enum class FocusCause : uint8_t {
		/// A select press on this node's own row in its parent's list: `enter_child` ->
		/// `descend_to`, the one path with a press behind it. Legacy's `selectButtonPress` path.
		RowPress,
		/// Focus reached this node by any other route, so nothing pressed its row: `retarget` (a
		/// `SceneId` deep link, the shortcut-pad jump, `open()`), `restore_state`'s rebuild, and
		/// `ascend`'s re-seed. Legacy's `beginScreen`/`focusRegained` path, and exactly the case
		/// `Toggle::selectButtonPress`'s guard exists for. A shortcut jump belongs here **because
		/// its press was on a grid pad, not on this row** -- it is not a "restore", which is what
		/// the old name claimed.
		NoRowPress,
	};

	/// @brief Load @p node's value into the edit storage its kind uses.
	/// @param cause Why this focus is happening -- see `FocusCause`. A `Kind::Custom` node whose
	///              item answers `CustomItem::on_focus_is_press()` is given `on_focus` only for
	///              `FocusCause::RowPress`.
	void enter_leaf(const Node& node, FocusCause cause);
	/// @brief Commit the edit storage for @p node through `bindings::write`.
	void commit_value(const Node& node);
	/// @brief Descend into @p child if its permission allows, opening the range picker for
	///        `MustSelectRange`.
	void enter_child(const Node& child);
	/// @brief Push @p child onto the path with no permission question asked -- the half of
	///        `enter_child` the range-picker substitution reuses.
	void descend_to(const Node& child);
	/// @brief Go up one level, skipping levels whose permission is now `No`
	///        (`sound_editor.cpp:710-721`).
	void ascend();
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target).
	void retarget(const Node& node);

	MenuState st_{};
	/// What the last `after_render` drained, in order (`drain_trace`).
	std::array<DrainBand, 8> drain_trace_{};
	size_t drain_trace_len_ = 0;
	void trace_drain(DrainBand band) {
		if (drain_trace_len_ < drain_trace_.size()) {
			drain_trace_[drain_trace_len_++] = band;
		}
	}
	/// The voice usage page's counters and the time since their last sample; `voice_usage_at_` is where the last tick
	/// found the focus, so arriving on the page or a counter begins its session.
	VoiceUsageSampler voice_usage_;
	uint32_t voice_usage_elapsed_ms_ = 0;
	/// Where the focus is relative to the voice usage page: its counters sample there.
	enum class VoiceUsagePlace : uint8_t { None, Page, Counter };
	VoiceUsagePlace voice_usage_place() const;
	VoiceUsagePlace voice_usage_at_ = VoiceUsagePlace::None;
	/// What `render_horizontal`'s columns draw from, here rather than on the stack: a group page has up to
	/// `kMaxMenuColumns` of them, and the scene lives in SDRAM.
	ColumnSlot column_slots_[kMaxMenuColumns];
	/// `HorizontalMenu::acceleration()`: one select-turn acceleration for every horizontal page, as legacy's static is.
	deluge::ui::EncoderAcceleration horizontal_acceleration_{};
	MenuContext ctx_{};

	/// @brief The one-slot outbox `take_action()` empties. Not part of `MenuState`: `retarget`
	///        clears the whole of that on a re-root, and an action already filed must still be
	///        delivered even if the path moves in the same tick.
	MenuActionRequest taken_action_{};

	/// @brief The column the next `on_enter` focuses on the page it opens, set by
	///        `arm_shortcut_landing` (from `handle()`'s jump, alongside the `Replace` it returns, or
	///        from the application before it opens the guest). Consumed by `on_enter`, cleared by
	///        `open()` and `on_resume()`. Not part of `MenuState`: the shell restores that between the
	///        `Replace` and `on_enter`.
	const Node* landing_column_ = nullptr;

	/// @brief The child each submenu was last left on: legacy's per-`Submenu` `current_item_` (`submenu.cpp:9-18`),
	///        which a fresh visit lands on. Once all are in use the least recently recorded entry is overwritten; the
	///        submenus on the current path are recorded on every render, so a root is never the one evicted.
	struct RememberedRow {
		const Node* submenu = nullptr;
		const Node* child = nullptr;
		uint32_t recorded = 0; ///< `remembered_clock_` when last recorded; 0 for an empty entry
	};
	static constexpr size_t kRememberedRows = 128;
	RememberedRow remembered_[kRememberedRows]{};
	uint32_t remembered_clock_ = 0;
	/// @brief What belongs to one open of the scene: reset by value in `open()` and `on_enter()`, which is legacy's
	///        `beginScreen` -- a new screen.
	struct Visit {
		/// @brief Use up the current Select press at the next `after_render`: legacy's `SoundEditor::opened` and
		///        `focusRegained` (`sound_editor.cpp:513-516`, `:531-533`), so the release of a press that opened or
		///        returned to the menu does not act.
		bool use_up_select = true;
		/// Whether the next `leds()` restarts the shared normal blink, BACK's and LEARN's (`blinkLed`), and the screen
		/// the render last checked that against (`beginScreen` restarts it for a LEARN-blinking item).
		bool blink_restart = true;
		/// @brief Legacy's `UI_MODE_MIDI_LEARN`: entered on a LEARN press with Shift up, no other UI mode on, and an
		///        item that allows learn; left on LEARN's release (`sound_editor.cpp:716-745`). Select's release does
		///        nothing in it.
		bool learn_mode = false;
		/// Whether the next `leds()` restarts the shared fast blink, as learn mode's `blinkLed(LEARN, 255, 1)` does.
		bool learn_restart = false;
		/// @brief `ascend()` went up from depth 0: legacy's `exitCompletely()`, drained by `after_render`.
		bool close_requested = false;
		/// @brief Select was held during the last render: a value edited under it uses the press up
		///        (`Value::selectEncoderAction`, `menu_item/value.h:64-67`).
		bool select_held = false;
		const Node* blink_screen = nullptr;
		/// The value the last render's session began on (`focused_value()`); a change is a new session.
		const Node* session_value = nullptr;
	};
	Visit visit_{};

	/// @brief `getCurrentMenuItem()->allowsLearnMode()`: on a page, its selected child's (`Submenu::allowsLearnMode`,
	///        `submenu.cpp:233-238`), which a child page never allows.
	bool allows_learn_mode() const;

	/// @brief Record the selected child of every submenu on the current path; only while `ctx_` is the one the path
	///        was walked with, since reading which children show runs their relevance tests.
	void remember_path();
	/// @brief Put the submenu at the current depth on the child it was last left on, if it still shows it.
	void restore_row();

	/// @brief The node whose shortcut pad blinks and toggles the second layer: a horizontal page's
	///        focused column (`Submenu::updatePadLights`), else `focused()`.
	const Node* pad_subject() const;

	/// @brief Focus @p column on the page `focused()` shows, when the page lists it visibly;
	///        otherwise leave the focus where it is (`Submenu::focusChild`).
	void focus_column(const Node& column);

	/// @brief `Direction::checkPermissionToBeginSession`'s latch (`sequence/direction.h:117-119`): when @p value is
	///        Play Direction on a non-kit clip, `MenuContext::selected_note_row` takes whether a row is auditioning.
	///
	/// Taken where the item begins: a re-root, a row press, and a horizontal column gaining focus. Legacy's horizontal
	/// page also re-takes it on the way back from the item and after each column move (`HorizontalMenu::beginSession`
	/// and `selectEncoderAction` -> `focusChild`); Play Direction is a column only in the note-row editor, whose root
	/// answers for the latch (`bindings.cpp`'s `note_row_bridge_direction`), so those re-takes change nothing.
	void begin_value_session(const Node* value);

public:
	/// @name The two navigation steps a caller outside the scene takes (note-family Task 7a)
	///
	/// Declared here, after the members, only so that adding them moved no line of this header that
	/// the tree already cites. They belong beside `read_value_again()` in spirit.
	/// @{
	/// @brief One level up the path -- `SoundEditor::goUpOneLevel` (`gui/ui/sound_editor.cpp:710-729`), for a
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
	///        (`gui/ui/sound_editor.cpp:362-369`), for a caller outside the scene.
	///
	/// Added by note-family Task 7a for `SoundEditor::toggleNoteEditorParamMenu`'s press arm
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
	///       `st_.columns[depth]` (`scenes/menu/menu_scene.cpp:1510-1511`), so this always lands on the
	///       first. It is `src/scenes/README.md:1709-1735`'s carry 5 -- focus kept per depth for a
	///       session, not per menu object for the process -- met by a descend rather than a re-open.
	///       Recorded, not changed.
	void enter_submenu(const Node& node);
	/// @}
};

/// @brief Whether shift + select on @p item deletes its param's automation: a param node, a node whose value is learned
///        as a param (the transposes), or a custom item that says so (the cable strength pages) -- legacy's
///        `Automation` items.
bool is_automation_item(const Node& item);

/// @brief The address of the param whose automation shift + select on @p item deletes, on the owner @p ctx edits;
///        nullopt where it edits none.
std::optional<deluge::project::ParamAddress> automation_address(const Node& item, const MenuContext& ctx,
                                                                const MenuState& state);

} // namespace deluge::scenes::menu
