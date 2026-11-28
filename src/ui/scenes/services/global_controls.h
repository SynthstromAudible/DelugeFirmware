#pragma once

/// @file
/// @brief `GlobalControls` -- the transport-and-global half of `View`, plus the tail of
///        `Buttons::buttonAction`, as a `shell::Service`.
///
/// The port of `View::buttonAction`'s global arms (`gui/views/view.cpp`) and of the
/// button tail `Buttons::buttonAction` runs *after* the current UI has declined an event
/// (`hid/buttons.cpp`), together with the LEDs those two decide:
/// `View::setLedStates`, `View::setTimeBaseScaleLedState` and
/// `PlaybackHandler::setLedStates`. The clip-arm
/// flash -- `View::flashPlayEnable`, `View::flashPlayDisable`
/// and `View::flashPlayRoutine` -- is this service's `tick()` and
/// `next_timer()`.
///
/// @par Why a service in `src/ui/scenes/` and not in `src/ui/shell/`
///
/// `shell::Service` is a shell interface, but `src/ui/shell/**` may not include `ui/scenes/**` or `project/**`
/// (`scripts/layers.toml`'s `shell-includes-outside` rule), and every arm here acts on the song through the
/// scenes' ports and reads the song's record -- the transport, the sync-scaling clip's write, the undo history. So the
/// shell's interface is implemented from inside the scenes library, which may see both, as `ModKnobs` and `MidiLearn`
/// do.
///
/// @par Its ports
///
/// The host installs its port set (`set_ports`) and the song's record (`set_model`). The service reads the transport
/// (`bind::Transport`: playback, the clocks, recording, the metronome, what undo allows and pending overdubs), the
/// foreground recorder's source and resampling (`bind::AudioFacts`), the clips' play state (`bind::ClipFacts`) and
/// the two features the TAP TEMPO and SYNC_SCALING arms branch on (`bind::DeviceSettings`). It acts through
/// `bind::TransportActions` (the buttons' transport calls, each with the popup it draws), writes the sync-scaling
/// clip through `bind::WriteSink`, forgets the undo history through `bind::DeviceActions`, and opens the settings
/// menu over the installed set. With no set installed every read answers an idle device's default and every action
/// is skipped; each press is still handled as legacy handles it.
///
/// @par Where this sits in the chain, and why that is legacy's own shape
///
/// `Router::route`'s chain is **top scene -> `mod_knobs` -> `global_controls`**
/// (`ui/shell/router.cpp`'s `run_chain`), and this service is deliberately last. That is
/// `Buttons::buttonAction` exactly: it calls `getCurrentUI()->buttonAction(b, on, inCardRoutine)`
/// at `hid/buttons.cpp`, returns early if the UI dealt with it, and only then
/// runs the tail. So a scene gets first refusal and this service is the last word,
/// which is what "routed last" means.
///
/// @par Two legacy fallbacks, two reachability rules, and only one of them is gated
///
/// The tail runs for **any** current UI that declined the event. `View::buttonAction` runs only for
/// a **root view**: `UI::buttonAction`'s base implementation is `return
/// ActionResult::NOT_DEALT_WITH` (`gui/ui/ui.h`) and does **not** forward to `view`, unlike
/// `UI::modEncoderAction`/`modButtonAction`/`modEncoderButtonAction`, which hand every root view's
/// gold knobs and mod buttons on (`gui/ui/root_knobs`), which is why `ModKnobs` really is a
/// base-class fallback. `View::buttonAction` has exactly
/// two callers, `TimelineView::buttonAction`'s final `else` (`gui/views/timeline_view.cpp`)
/// and `KeyboardScreen::buttonAction` (`gui/ui/keyboard/keyboard_screen.cpp`) -- and
/// `grep "public RootUI"` finds exactly those two classes (`gui/views/timeline_view.h`,
/// `gui/ui/keyboard/keyboard_screen.h`). So "the current UI is the root UI" is not an
/// approximation of the rule; it **is** the rule.
///
/// Hence `handle()` gates the `View`-arm group on `stack_.input_top().is_root()`
/// (`ui/shell/scene.h`'s `Scene::is_root`, added for this) and leaves the tail ungated. With a
/// non-root scene on top -- a menu, a browser, the marker editor -- `SAVE`, `LOAD`,
/// `SYNC_SCALING`, `TAP_TEMPO`, `BACK` and shift+`SELECT_ENC` do nothing here, exactly as they do
/// nothing in legacy: `SoundEditor::buttonAction` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`) ends by
/// delegating to `currentMenuItem->buttonAction`, whose base
/// (`origin/main:src/deluge/gui/menu_item/menu_item.h`) returns `NOT_DEALT_WITH`, and nothing on that path names
/// `view`.
///
/// @warning **Before the gate, the divergence was live and its only protection was an accident.**
///          `ButtonId::Back` is claimed unconditionally by the `BACK` arm, so an ungated service
///          would fire undo/redo from inside any non-root scene that does not claim `Back` itself.
///          What masked it was `ActionLogger::allowedToDoReversion` (`model/action/action_logger.cpp`),
///          which *itself* tests `getCurrentUI() == getRootUI()` -- an undocumented legacy-global
///          read that goes stale the moment the shell drives the device and `numUIsOpen` stops
///          tracking the scene stack. The gate is what makes the port's own answer load-bearing;
///          do not remove it on the grounds that "the action logger refuses anyway".
///
/// @note The chain runs the scene first and `mod_knobs` after it, which is *why* this service can
///       be written as legacy's tail rather than as a pre-filter.
///
/// @par The two halves of `handle()`, in legacy's order
///
/// `handle()` runs `View::buttonAction`'s arms first and the `Buttons::buttonAction` tail second,
/// because that is the order legacy runs them in and the first can suppress the second: an arm
/// that reaches `View::buttonAction`'s `return ActionResult::DEALT_WITH` makes `Buttons::buttonAction` (`buttons.cpp`)
/// jump straight to `dealtWith` and the tail never runs. The one place that matters is `PLAY` and
/// `RECORD`: under MIDI learn `View` claims them (`View::buttonAction`'s PLAY and RECORD arms) and the transport tail
/// is skipped; outside learn the arm's `&& currentUIMode == UI_MODE_MIDI_LEARN` fails, the chain
/// falls to `gui/views/view.cpp`'s `return ActionResult::NOT_DEALT_WITH`, and the tail runs.
///
/// @par The MIDI-learn boundary
///
/// Four arms of `View::buttonAction` are learn-mode work end to end and belong to `MidiLearn`,
/// not here: the `TAP_TEMPO` learn branch, the `LEARN` button,
/// `PLAY` under learn and `RECORD` under learn. The rule
/// is: **the service that owns the mode owns the arm, and the other asks
/// it.** This service asks, through `LearnMode::active()` (`services/learn_mode.h`) -- one
/// virtual call, no second copy of the mode.
///
/// @warning **The four arms above are returned `unhandled` rather than swallowed, and that is
///          load-bearing.** Swallowing them would look like a working port *and* would stop
///          `MidiLearn` -- routed one step further on -- from ever seeing them. `Router::run_chain` is top scene ->
///          `mod_knobs` -> `global_controls` -> `midi_learn` -> drop (`ui/shell/router.cpp`), and `MidiLearn::handle`
///          claims exactly `LEARN`, and `PLAY`/`RECORD`/`TAP_TEMPO` while its own mode is on. So a
///          `ViewArm::LearnOwns` result is a **hand-off, not a drop** -- do not read it as one, and
///          in particular do not conclude from it that the chain wants re-ordering. (Were `MidiLearn` absent
///          these events would fall off the end and `Router::dropped_events()` would count them.)
///
/// @par What is deliberately NOT ported, and why -- read this before adding to it
///
/// Every entry is a call into something `src/ui/scenes` may not name, with no seam that would let it
/// be spelled honestly. The rule for all of them, as for the load/save pair: **port the arm's
/// decision and leave the call as a documented no-op with a `@warning`, not as a new outbox.** A
/// second outbox beside `menu/action.h`'s `MenuAction` mailbox would leave two seams for
/// `gui::ui::dispatchShellActions` to drain; the mailbox has seven arms and that one consumer.
/// What survives it is the half that was never about timing: `take_action()` is a **one-slot**
/// mailbox with a silent overwrite (`menu/menu_scene.h`), a second outbox would need its own
/// polling contract and its own dispatcher arm, and nothing in this service's SAVE/LOAD arms is
/// waiting on one -- the load/save browser family has no port at all,
/// so there would be nothing for a request to ask for. Whoever ports that family decides this
/// again, on those terms rather than on the mailbox's.
///
///   * **`openUI(&saveSongUI)`** (`View::buttonAction`'s SAVE arm) and **`openUI(&loadSongUI)`** (its LOAD arm). The
///     load/save browser family is not ported, and
///     `saveSongUI`/`loadSongUI` are both on `scenes-no-root-ui`'s forbidden list. The *decision*
///     -- the four-button guard, the hold, the short-press test, the overdub refusal -- is ported
///     in full and specced; only the open is missing. See `handle_save`/`handle_load`.
///   * **`context_menu::clearSong`** (`View::buttonAction`'s LOAD arm), shift+LOAD. `ContextMenuScene` exists
///     (`ui/scenes/context_menu/`), but the *clear-song* menu is not among the context menu
///     scenes, and its `setupAndCheckAvailability()` is a `gui/` call. Decision ported, open
///     missing.
///   * **`display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)`** and the `cancelPopup()`
///     it guards (`Buttons::buttonAction` in `hid/buttons.cpp`). `scenes-no-display` forbids `display->`, and the
///     seam that would replace it does not reach: that popup is raised by *model* code straight to
///     the legacy display (`model/song/song.cpp`), never through `notify::` or
///     `shell::PopupScene`, so even an injected `PopupScene` could not see it. **This one is a
///     behaviour difference, not merely a missing call**: with that popup up, legacy's short
///     RECORD tap dismisses it and does nothing else, while this port falls through to
///     `bind::TransportActions::record()` and toggles recording.
///   * **The `SYNC_SCALING` fill-mode arm's note halves** (`View::buttonAction` in `gui/views/view.cpp`). The
///     note-editor test needs no port here: it holds only in a note mode, and the note mode never lets this
///     service see the press -- `gui::ui::forwardNoteButton` hands it to the mode's own handler, whose
///     `return buttonAction(b, on, inCardRoutine)` reaches legacy's test itself, which asks
///     `gui::ui::noteEditorOpen()`/`noteRowEditorOpen()` . The two fill adjustments name `instrumentClipView`, on
///     `scenes-no-root-ui`'s list, and `InstrumentClipScene` is not ported. What survives is the arm's "otherwise",
///     fill mode, `bind::TransportActions::fill`.
///   * **`deluge_signal_write(DELUGE_SIGNAL_SYNC_LED, ...)`** (`PlaybackHandler::setLedStates`).
///     The external-clock LED is a board *signal*, not an `IndicatorLED`, so it has no `ButtonId`
///     and therefore no `ui::LedId` (`ui/shell/profiles/deluge.h`: "LedId values = ButtonId values").
///     `LedFrame` cannot express it. Owed to whoever widens the LED surface.
///
/// @par The pre-UI section: verified, not re-ported
///
/// `Buttons::buttonAction`'s section above the UI call (`hid/buttons.cpp`) is not re-ported here: **two of its
/// five items are ported and three are not**:
///
///   * `considerShiftReleaseForSticky = false` on any press -- ported,
///     `InputLowering::on_button` (`ui/shell/input_lowering.cpp`). Its *cross-screen* twin on
///     the very next line is **not**.
///   * The mod-button loop -- ported, as `Router`'s `is_mod_or_gold` plus `ModKnobs`.
///   * The `AFFECT_ENTIRE` -> `display->cancelPopup()` -- **not ported**. It belongs to
///     the notice seam (`notify::Notice::Kind::CancelPopup`), not to this service.
///   * The `BACK` -> `TimerName::BACK_MENU_EXIT` long-press, whose callback is
///     `getCurrentUI()->exitUI()` (`UITimerManager::routine` in `gui/ui_timer_manager.cpp`) -- **not ported**. It is a
///     scene-exit gesture, so it belongs to whoever owns "hold BACK to leave", not here.
///   * The `CROSS_SCREEN_EDIT` arming `considerCrossScreenReleaseForCrossScreenMode` --
///     **not ported**. Cross-screen mode is a clip-view fact.
///   * `selectButtonPressUsedUp = false` -- ported, and generalised: `InputLowering` clears the used-up bit for
///     **every** button on its own press (`InputLowering::on_button`), which subsumes both this and the `RECORD` arm of
///     `Buttons::buttonAction` (`buttons.cpp`): `recordButtonPressUsedUp = false`.
///
/// And the tail's `SHIFT` arm (`Buttons::buttonAction` in `hid/buttons.cpp`, `Buttons::commandToggleShift`) is already
/// `InputLowering::toggle_shift` (`ui/shell/input_lowering.cpp`),
/// called from `on_button` *before* the event is ever emitted. `handle()` therefore reports
/// `ButtonId::Shift` handled -- legacy's tail does deal with it -- and does nothing.
///
/// @par Undo is the BACK arm
///
/// The dedicated undo and redo buttons of `View::buttonAction` (`origin/main:src/deluge/gui/views/view.cpp`) sit
/// inside `#ifdef undoButtonX`, a macro defined nowhere, so they never build. The `BACK` arm is the undo, or redo with
/// shift, and it is what this service ports: when `bind::Transport::undo_allowed`, `bind::TransportActions::undo`
/// runs the reversion before it returns.
///
/// @par The play flash is the clip-arm flash, and it is not a Play-LED flash
///
/// `View::flashPlayRoutine` is this service's `tick`/`next_timer`, but
/// **`flashPlayRoutine` does not touch the Play LED.** It toggles
/// `View::clipArmFlashOn` -- the phase armed clips blink at on the *pads*, read
/// by `SessionView::gridRenderClipColor` and `gridPulseSelectedClip`
/// (both `session_view.cpp`), `View::getClipMuteSquareColour` and `View::renderMacros`
/// (`origin/main:src/deluge/gui/views/view.cpp`) -- and then asks for a
/// render. The only time-driven thing in legacy that *does* set `IndicatorLED::PLAY` is
/// `View::midiLearnFlash`, which is `services::MidiLearn`'s
/// (`services/midi_learn.h`). So what this service owns is the
/// clip-arm phase and its 60 ms (`widgets::core::kFastFlashTime`, `ui/widgets/core/metrics.h`) deadline;
/// `clip_arm_flash_on()` is the fact, and the scenes that paint armed clips (the audio clip view's macros column
/// while SESSION is held) are its consumers.
///
/// @note `View::flashPlayRoutine` does **not** re-arm its own timer -- the re-arm lives in
///       `SessionView::flashPlayRoutine` (`gui/views/session_view.cpp`), which calls `view.flashPlayEnable()` again
///       only while some clip is actually armed. That is view-side and not ported, so here `tick()` toggles the phase
///       once per `flash_play_enable()` and stops. A future `SessionScene` re-arms it exactly where legacy does.
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` and `UiContext::now_ms` are the only
///          places a clock arrives (`shell-no-clock`). Legacy's sample-timer
///          press timings (`View::buttonAction`'s SAVE and LOAD arms, `Buttons::buttonAction`) become milliseconds
///          here; `kShortPressTime` is `kSampleRate / 2` (`definitions_cxx.hpp`), which is
///          exactly the 500 ms `LoweringConfig::short_press_ms` already carries
///          (`ui/shell/input_lowering.h`).

#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/services/learn_mode.h"

#include "project/song.h"
#include "ui/widgets/core/led_frame.h"

#include "ui/shell/scene.h"
#include "ui/shell/scene_stack.h"
#include "ui/shell/service.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::shell {
class InputLowering;
}

namespace deluge::ui::scenes::services {

/// @brief Transport, tempo, sync scaling, undo/redo, load/save holds -- and the LEDs they decide.
class GlobalControls final : public deluge::ui::shell::Service {
public:
	/// @brief The one `currentUIMode` value this service reads.
	///
	/// Legacy has a single `uint32_t currentUIMode` (`gui/ui/ui.h`), and this service asks it
	/// in seven places. `src/ui/scenes` may not include `gui/ui/ui.h`, so the value is reconstructed:
	/// `ViewState::ui_mode` carries what the root scene knows, and `effective_ui_mode()` folds in
	/// the two modes this service sets for itself (`HoldingSave`, `HoldingLoad`) and the one
	/// `MidiLearn` owns.
	///
	/// `Other` is every value not listed -- `UI_MODE_STUTTERING`, `UI_MODE_HOLDING_ARRANGEMENT_ROW`
	/// and the rest. It exists so a scene can say "some mode is active" without this enum having
	/// to grow a name for it: every test here is against a *specific* mode, so `Other` correctly
	/// fails all of them.
	enum class UiMode : uint8_t {
		None,          ///< `UI_MODE_NONE` (`gui/ui/ui.h`, which is 0).
		MidiLearn,     ///< `UI_MODE_MIDI_LEARN`. Never set through `ViewState`; see `LearnMode`.
		RecordCountIn, ///< `UI_MODE_RECORD_COUNT_IN` -- read by the `TAP_TEMPO` arm (`View::buttonAction`).
		Auditioning,   ///< `UI_MODE_AUDITIONING` -- read by the `LOAD` release arm of `View::buttonAction`.
		HoldingSave,   ///< `UI_MODE_HOLDING_SAVE_BUTTON` (`View::buttonAction` in `gui/views/view.cpp`). Owned here.
		HoldingLoad,   ///< `UI_MODE_HOLDING_LOAD_BUTTON` (`View::buttonAction` in `gui/views/view.cpp`). Owned here.
		Other,         ///< Any other mode: active, and none of the above.
	};

	/// @brief The root-UI facts this service cannot read for itself, injected exactly as
	///        `ModKnobs::ViewState` injects its four.
	///
	/// Every field is a legacy read `src/ui/scenes` has no way to make. **Nothing sets this yet** --
	/// the root scenes that would (`SessionScene`, `ArrangerScene`, `PerformanceScene`) are
	/// unported -- so today the defaults hold and the spec is the only caller. The defaults are the
	/// state of a plain root view with no mode active, which is what every scene ported so far is.
	struct ViewState {
		/// `currentUIMode`, as the root scene knows it. `MidiLearn` is deliberately not settable
		/// here: `effective_ui_mode()` takes that from `LearnMode::active()` instead, so there is
		/// one owner of the mode and not two.
		UiMode ui_mode = UiMode::None;
		/// `getCurrentUI()->toClipMinder() != nullptr` (`View::buttonAction` and `View::setTimeBaseScaleLedState` in
		/// `gui/views/view.cpp`). Decides whether the SYNC_SCALING LED blinks and whether sync scaling may be started
		/// at all.
		bool current_ui_is_clip_minder = false;
		/// `getRootUI() == &performanceView` (`View::buttonAction`'s SAVE and LOAD arms) -- the fifth term of the
		/// SAVE/LOAD guard, which lets KEYBOARD+SAVE through only in performance view.
		bool root_is_performance = false;
		/// `loadSongUI.isLoadingSong()` (`Buttons::buttonAction`). Suppresses the tempo popup while
		/// a song is loading.
		bool loading_song = false;
		/// `currentUI == &sessionView || currentUI == &arrangerView` (`Buttons::buttonAction`).
		/// Legacy's comment there: only show the tempo pop-up if we are not in Song / Arranger
		/// view.
		bool current_ui_is_song_or_arranger = false;
		/// The root view's greyout arm (`MenuContext::RootGreyout`), which the settings menu this service opens
		/// greys by.
		menu::MenuContext::RootGreyout root_greyout = menu::MenuContext::RootGreyout::Menu;
	};

	/// @param stack    The scene stack this service pushes the settings menu onto
	///                 (`gui/views/view.cpp`).
	/// @param lowering The input lowering it reads and writes the used-up bit and the press clock
	///                 on.
	/// @param learn    MIDI learn, asked before the `PLAY`/`RECORD`/`TAP_TEMPO` arms. A real
	///                 application passes the `services::MidiLearn` it also puts in
	///                 `Services::midi_learn`; the `no_learn_mode()` default is for a caller that
	///                 has none (and for a spec that wants a switchable one).
	///
	/// @note `lowering` is a **non-const** reference and `UiContext::lowering` is a `const`
	///       pointer (`ui/shell/scene.h`), so the context cannot serve: `Buttons::buttonAction`'s PLAY and RECORD arms
	///       *write* the used-up bit. `ModKnobs` takes the same argument for the same kind of
	///       reason.
	GlobalControls(deluge::ui::shell::SceneStack& stack, deluge::ui::shell::InputLowering& lowering,
	               LearnMode& learn = no_learn_mode())
	    : stack_(stack), lowering_(lowering), learn_(learn) {}

	/// @name The model this service edits
	/// @{

	/// @brief The port set the service reads and acts through: the transport and its actions, the audio facts, the
	///        clip facts, the device's settings and actions, and the write sink. The set is not owned and must outlive
	///        the service's use of it.
	/// @param ports The set, or null for none: every read answers its default and every action is skipped.
	void set_ports(const deluge::ui::scenes::bind::Ports* ports) { ports_ = ports; }
	/// @brief The record of the song the arms resolve against: its sync-scaling clip, and the model the settings menu
	///        opens over. Not owned.
	/// @warning A stale record makes every sync-scaling decision and the SYNC_SCALING LED read the wrong song; update
	///          it whenever the song changes.
	/// @param model The song's record, or null before one exists: the SYNC_SCALING and settings arms then do nothing.
	void set_model(const deluge::project::Song* model) { model_ = model; }
	/// @return What was last passed to `set_ports`.
	[[nodiscard]] const deluge::ui::scenes::bind::Ports* ports() const { return ports_; }
	/// @return What was last passed to `set_model`.
	[[nodiscard]] const deluge::project::Song* model() const { return model_; }

	/// @brief Tell this service what the root view is -- see `ViewState`.
	/// @param state The view's answers.
	void set_view_state(const ViewState& state) { view_ = state; }
	/// @return What was last passed to `set_view_state`.
	const ViewState& view_state() const { return view_; }

	/// @brief The single `currentUIMode` value, assembled from its three owners.
	///
	/// `MidiLearn` outranks everything (it is the mode legacy's own arms test first, in
	/// `View::buttonAction`), then this service's own two holding modes, then whatever the root scene
	/// said.
	/// @return The mode as legacy's `currentUIMode` would read.
	UiMode effective_ui_mode() const;

	/// @}

	/// @name The clip-arm flash -- `View::flashPlay*`
	/// @{

	/// @brief Arm the flash -- `View::flashPlayEnable`.
	///
	/// Legacy sets `TimerName::PLAY_ENABLE_FLASH` to `kFastFlashTime` when it is not already set; here that is a
	/// deadline `tick()` fires and `next_timer()` reports, and a call while one is armed leaves it where it is.
	/// @param now_ms The tick's wall clock to measure the deadline from.
	void flash_play_enable(uint32_t now_ms);
	/// @brief Disarm it and clear the phase -- `View::flashPlayDisable`.
	///
	/// Legacy's `flashPlayDisable` tail is a render request against `sessionView`/`performanceView`,
	/// which is view-side and not ported; a scene reading `clip_arm_flash_on()` sees the change on
	/// its next tick.
	void flash_play_disable();
	/// @brief `View::clipArmFlashOn` (`gui/views/view.h`) -- the phase armed clips blink at.
	///
	/// The consumers are the scenes that paint armed clips: the audio clip view's macros column.
	bool clip_arm_flash_on() const { return clip_arm_flash_on_; }

	/// @}

	/// @name `shell::Service`
	/// @{

	/// @brief `View::buttonAction`'s global arms, then `Buttons::buttonAction`'s tail.
	/// @param event The routed event.
	/// @param ctx   The world around it.
	/// @return Handled exactly where legacy reaches `DEALT_WITH`; see the class doc.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;

	/// @brief Declare Play, Record, Tap-tempo, Sync-scaling, Save, Load and the sync-scaling
	///        alert.
	///
	/// `PlaybackHandler::setLedStates` and
	/// `View::setTimeBaseScaleLedState`, which is all `View::setLedStates`
	/// is. Legacy calls these from wherever something changed; here they are
	/// recomputed from the model every tick, which is what an `LedFrame` is.
	/// @param frame The tick's declarations.
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;

	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief The grid while the clip-arm flash is armed: its phase's readers are pad renderers, and
	///        `View::flashPlayRoutine` asks the current UI to redraw its sidebar when it toggles. Nothing otherwise.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override {
		return {.oled = false, .grid = clip_arm_flash_deadline_ms_.has_value()};
	}
	/// @}

	/// @name Observable state, for specs and for the root scenes that will read it
	/// @{
	/// @brief Is the SAVE button being held down for the save gesture? `UI_MODE_HOLDING_SAVE_BUTTON`
	///        (`View::buttonAction`).
	bool holding_save() const { return holding_save_; }
	/// @brief Is the LOAD button being held down? `UI_MODE_HOLDING_LOAD_BUTTON` (`View::buttonAction` in
	///        `gui/views/view.cpp`, which also clears it).
	bool holding_load() const { return holding_load_; }
	/// @brief The three call sites this port reaches but cannot make -- see the class doc's
	///        "deliberately NOT ported".
	///
	/// One enumerator per *site*, not one per kind of thing: the LOAD arm reaches two different opens
	/// under two different conditions, and a single total could not tell them apart -- a spec
	/// asserting one open after a LOAD gesture could be satisfied by the other, which would make the
	/// counter's only claim -- "the arm's decision reached the point legacy opens a UI at" --
	/// unfalsifiable.
	///
	/// @note The doc for each enumerator is on its own line above it, not trailing: a trailing
	///       `///<` shares a line with code, and `scripts/check_layer_rules.py`'s `is_comment_line`
	///       skips only a line that is *nothing but* comment -- so the forbidden global names these
	///       docs necessarily quote would have counted as `scenes-no-root-ui` violations.
	enum class UnportedOpen : uint8_t {
		/// The song saver's open (`View::buttonAction`'s SAVE arm).
		SaveSong,
		/// The song loader's open (`View::buttonAction`'s LOAD arm).
		LoadSong,
		/// The clear-song context menu's open (`View::buttonAction`'s LOAD arm).
		ClearSong,
		Count,
	};

	/// @brief How many times @p which was reached.
	///
	/// Not a mailbox and not an outbox: nothing consumes these and nothing may. They are counters,
	/// so a spec can prove an arm's *decision* reached the point legacy opens a UI at, which is the
	/// only part of those arms this port can assert.
	/// @param which The site.
	uint32_t unported_opens(UnportedOpen which) const {
		const size_t i = static_cast<size_t>(which);
		return i < static_cast<size_t>(UnportedOpen::Count) ? unported_opens_[i] : 0;
	}
	/// @brief Every site's count added up -- for a "nothing ran away" assertion, never for
	///        identifying which arm fired.
	uint32_t unported_opens_total() const;
	/// @}

	/// @brief `kShortPressTime` (`definitions_cxx.hpp`, `kSampleRate / 2`) in milliseconds.
	///
	/// The same 500 ms `LoweringConfig::short_press_ms` already carries; named here because the
	/// SAVE and LOAD arms measure against their own press clock (`View::buttonAction`) rather
	/// than through `InputLowering::was_short_press`, which only knows the button's own press.
	static constexpr uint32_t kShortPressMs = 500;
	/// @brief `kFastFlashTime` (`ui/widgets/core/metrics.h`) -- the clip-arm flash's half-period.
	static constexpr uint32_t kClipArmFlashMs = 60;
	/// @brief How long `indicator_leds::indicateAlertOnLed` blinks for.
	///
	/// `indicateAlertOnLed` is `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp`), which sets
	/// `blinksLeft = numBlinks * 2` == 6 and then calls `updateBlinkingLedStates` **once, at arm time**, which
	/// decrements it to 5 (`blinksLeft--` in its body). The fold-back in its `blinksLeft == 0` branch therefore happens
	/// after **five** further `kFastFlashTime` timeouts (`ledBlinkTimeout`), not six: 300 ms, not 360. Pinned against
	/// legacy's own countdown in `global_controls_spec.cpp`.
	static constexpr uint32_t kAlertMs = (3 * 2 - 1) * kClipArmFlashMs;

private:
	/// @brief What `View::buttonAction` did with a button, in the three shapes that matter here.
	///
	/// Legacy has only two answers -- `DEALT_WITH` and `NOT_DEALT_WITH` (`View::buttonAction`'s two returns) --
	/// and the tail runs only for the second (`Buttons::buttonAction` in `hid/buttons.cpp`). The port needs a third,
	/// because four of the arms are `MidiLearn`'s: legacy *deals with* them, so the tail must be
	/// skipped, but this service does none of the work, so the event must still be reported
	/// unhandled to the router. Collapsing the two would either run the transport tail under MIDI
	/// learn (wrong) or claim an event nothing acted on (invisible).
	enum class ViewArm : uint8_t {
		NotAnArm, ///< `View::buttonAction`'s `NOT_DEALT_WITH` return -- run the tail.
		Handled,  ///< `View::buttonAction`'s `DEALT_WITH` return -- skip the tail, report handled.
		/// `View::buttonAction`'s `DEALT_WITH` return for an arm `services::MidiLearn` owns: skip the tail, report
		/// unhandled, and let the next step in the chain claim it.
		LearnOwns,
	};

	/// @brief `View::buttonAction`'s arms (`gui/views/view.cpp`).
	/// @param button The button.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param r      Where an arm's popup or push is appended.
	/// @return Which of the three shapes above the arm was.
	ViewArm view_button_action(deluge::ui::shell::ButtonId button, bool on, const deluge::ui::shell::UiContext& ctx,
	                           deluge::ui::shell::Result& r);
	/// @brief `Buttons::buttonAction`'s tail (`hid/buttons.cpp`).
	/// @param button The button.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @return `handled` for every button the tail names.
	deluge::ui::shell::Result tail_button_action(deluge::ui::shell::ButtonId button, bool on,
	                                             const deluge::ui::shell::UiContext& ctx);

	/// @brief The `SAVE` arm of `View::buttonAction`.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param result Where the pending-overdubs refusal popup is appended.
	void handle_save(bool on, const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);
	/// @brief The `LOAD` arm of `View::buttonAction`.
	/// @param on  Press or release.
	/// @param ctx The world around it.
	void handle_load(bool on, const deluge::ui::shell::UiContext& ctx);
	/// @brief The four-button guard both share (`View::buttonAction`'s SAVE and LOAD arms).
	/// @param ctx The world around it.
	/// @return True when neither SYNTH/KIT/MIDI/CV is held and KEYBOARD is not held in performance
	///         view -- i.e. when the arm's body may run.
	bool load_save_allowed(const deluge::ui::shell::UiContext& ctx) const;
	/// @brief The `SYNC_SCALING` arm of `View::buttonAction`.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param result Where a refusal popup is appended.
	void handle_sync_scaling(bool on, const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);
	/// @brief The `SELECT_ENC`+shift arm of `View::buttonAction`: open the settings menu.
	/// @param ctx    The world around it.
	/// @param result Where the refusal popup or the push is appended.
	void handle_settings_menu(const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);

	/// @brief Bump @p which's counter -- see `unported_opens()`.
	/// @param which The site that was reached.
	void count_unported(UnportedOpen which);

	/// @brief Is the SYNC_SCALING LED in `View::setTimeBaseScaleLedState`'s blink arm
	///        (`gui/views/view.cpp`) right now?
	///
	/// Asked by `leds()` and by `next_timer()`, which has to request a wake for it -- see the note
	/// there.
	/// @return True when the LED is declared `Blink` rather than solid.
	bool blinking_sync_scaling() const;

	/// @name Reads and actions through the installed ports, each answering an idle device with none
	/// @{
	[[nodiscard]] bool playing() const;                                           ///< the transport's, false with none
	[[nodiscard]] deluge::ui::scenes::bind::TransportRecording recording() const; ///< the transport's, OFF with none
	[[nodiscard]] deluge::ui::scenes::bind::ForegroundRecording
	foreground() const;                                                        ///< the audio facts', NONE with none
	[[nodiscard]] deluge::ui::scenes::bind::TransportActions* actions() const; ///< the transport's actions, or null
	/// @}

	deluge::ui::shell::SceneStack& stack_;
	deluge::ui::shell::InputLowering& lowering_;
	LearnMode& learn_;

	const deluge::ui::scenes::bind::Ports* ports_ = nullptr; ///< the installed ports, or null for none
	const deluge::project::Song* model_ = nullptr;           ///< the song's record, or null for none
	ViewState view_{};

	/// `UI_MODE_HOLDING_SAVE_BUTTON` / `UI_MODE_HOLDING_LOAD_BUTTON` (`View::buttonAction` in `gui/views/view.cpp`).
	bool holding_save_ = false;
	bool holding_load_ = false;
	/// `View::timeSaveButtonPressed` -- **one** member, shared by both arms exactly as legacy shares
	/// it: `View::buttonAction`'s LOAD arm writes the same variable on a press that its SAVE arm
	/// does. They can never both be held, because each arm's press branch requires `UI_MODE_NONE`.
	uint32_t save_pressed_ms_ = 0;
	/// The lowering's stamp of the last RECORD press the transport tail itself took (`tail_button_action`). A release
	/// belongs to the button's latest press, so one whose press a scene took finds a different stamp there and does not
	/// toggle recording.
	std::optional<uint32_t> record_press_ms_;

	/// `View::clipArmFlashOn` and its `TimerName::PLAY_ENABLE_FLASH` deadline.
	bool clip_arm_flash_on_ = false;
	std::optional<uint32_t> clip_arm_flash_deadline_ms_;

	/// `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)` (`View::buttonAction`), as a
	/// deadline rather than a blink countdown.
	std::optional<uint32_t> clip_view_alert_until_ms_;

	/// The last clock `tick()` saw, for `leds()` and `next_timer()`, neither of which is given one.
	uint32_t now_ms_ = 0;

	/// See `unported_opens()`.
	uint32_t unported_opens_[static_cast<size_t>(UnportedOpen::Count)]{};
};

// (No singleton accessor, deliberately -- the same reason `ModKnobs` has none: this service
// borrows a `SceneStack` and an `InputLowering` that only exist once a `Shell` does, so the
// application constructs it beside its `Shell` and passes it in `shell::Services::global_controls`.
// Nothing links `deluge_ui_scenes` into an application yet, so today that caller is the spec.)

} // namespace deluge::ui::scenes::services
