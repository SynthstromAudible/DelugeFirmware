#pragma once

/// @file
/// @brief `GlobalControls` -- the transport-and-global half of `View`, plus the tail of
///        `Buttons::buttonAction`, as a `shell::Service`.
///
/// The port of `View::buttonAction`'s global arms (`gui/views/view.cpp:139-470`) and of the
/// button tail `Buttons::buttonAction` runs *after* the current UI has declined an event
/// (`hid/buttons.cpp:143-231`), together with the LEDs those two decide:
/// `View::setLedStates` (`view.cpp:514-516`), `View::setTimeBaseScaleLedState` (`:502-512`) and
/// `PlaybackHandler::setLedStates` (`playback/playback_handler.cpp:2499-2512`). The clip-arm
/// flash -- `View::flashPlayEnable` (`view.cpp:2766-2768`), `View::flashPlayDisable`
/// (`:2770-2783`) and `View::flashPlayRoutine` (`:2754-2764`) -- is this service's `tick()` and
/// `next_timer()`.
///
/// @par Why a service in `src/scenes/` and not in `src/shell/`
///
/// The same reason `ModKnobs` gives (`services/mod_knobs.h`): `shell::Service` is a shell
/// interface, but `src/shell/**` may not include `model/**` (`scripts/check_layer_rules.py`'s
/// `shell-includes-outside` row) and every arm here writes the model -- `PlaybackHandler`,
/// `Song::setInputTickScaleClip`, `ActionLogger`. So the shell's interface is implemented from
/// inside the scenes library, which may see both.
///
/// @par Where this sits in the chain, and why that is legacy's own shape
///
/// `Router::route`'s chain is **top scene -> `mod_knobs` -> `global_controls`**
/// (`shell/router.cpp`'s `run_chain`), and this service is deliberately last. That is
/// `Buttons::buttonAction` exactly: it calls `getCurrentUI()->buttonAction(b, on, inCardRoutine)`
/// at `hid/buttons.cpp:134`, returns early if the UI dealt with it (`:139-141`), and only then
/// runs the tail at `:143-231`. So a scene gets first refusal and this service is the last word,
/// which is what "routed last" means.
///
/// @par Two legacy fallbacks, two reachability rules, and only one of them is gated
///
/// The tail runs for **any** current UI that declined the event. `View::buttonAction` runs only for
/// a **root view**: `UI::buttonAction`'s base implementation is `return
/// ActionResult::NOT_DEALT_WITH` (`gui/ui/ui.h:98-100`) and does **not** forward to `view`, unlike
/// `UI::modEncoderAction`/`modButtonAction`/`modEncoderButtonAction` (`gui/ui/ui.cpp:35-45`), which
/// do and which is why `ModKnobs` really is a base-class fallback. `View::buttonAction` has exactly
/// two callers, `TimelineView::buttonAction`'s final `else` (`gui/views/timeline_view.cpp:119-121`)
/// and `KeyboardScreen::buttonAction` (`gui/ui/keyboard/keyboard_screen.cpp:591`) -- and
/// `grep "public RootUI"` finds exactly those two classes (`gui/views/timeline_view.h:27`,
/// `gui/ui/keyboard/keyboard_screen.h:35`). So "the current UI is the root UI" is not an
/// approximation of the rule; it **is** the rule.
///
/// Hence `handle()` gates the `View`-arm group on `stack_.input_top().is_root()`
/// (`shell/scene.h`'s `Scene::is_root`, added for this) and leaves the tail ungated. With a
/// non-root scene on top -- a menu, a browser, the marker editor -- `SAVE`, `LOAD`,
/// `SYNC_SCALING`, `TAP_TEMPO`, `BACK` and shift+`SELECT_ENC` do nothing here, exactly as they do
/// nothing in legacy: `SoundEditor::buttonAction` (`gui/ui/sound_editor.cpp:333`) ends by
/// delegating to `currentMenuItem->buttonAction` (`:595-628`), whose base
/// (`gui/menu_item/menu_item.h:78-80`) returns `NOT_DEALT_WITH`, and nothing on that path names
/// `view`.
///
/// @warning **Before the gate, the divergence was live and its only protection was an accident.**
///          `ButtonId::Back` is claimed unconditionally by the `BACK` arm, so an ungated service
///          would fire undo/redo from inside any non-root scene that does not claim `Back` itself.
///          What masked it was `ActionLogger::allowedToDoReversion` (`model/action/action_logger.cpp:854`),
///          which *itself* tests `getCurrentUI() == getRootUI()` -- an undocumented legacy-global
///          read that goes stale the moment the shell drives the device and `numUIsOpen` stops
///          tracking the scene stack. The gate is what makes the port's own answer load-bearing;
///          do not remove it on the grounds that "the action logger refuses anyway".
///
/// @note The chain ran the other way round (`mod_knobs` before the scene) from plan D until E4
///       task 3 corrected it; the defect, the evidence and the fix are recorded as amendments
///       §15.4 and §15.5 of `docs/superpowers/specs/2026-08-26-ui-shell-design.md`. Nothing here
///       needed changing for it, but the correction is *why* this service can be written as
///       legacy's tail rather than as a pre-filter.
///
/// @par The two halves of `handle()`, in legacy's order
///
/// `handle()` runs `View::buttonAction`'s arms first and the `Buttons::buttonAction` tail second,
/// because that is the order legacy runs them in and the first can suppress the second: an arm
/// that reaches `view.cpp:469`'s `return ActionResult::DEALT_WITH` makes `buttons.cpp:139-141`
/// jump straight to `dealtWith` and the tail never runs. The one place that matters is `PLAY` and
/// `RECORD`: under MIDI learn `View` claims them (`view.cpp:199`, `:215`) and the transport tail
/// is skipped; outside learn the arm's `&& currentUIMode == UI_MODE_MIDI_LEARN` fails, the chain
/// falls to `view.cpp:465-467`'s `return ActionResult::NOT_DEALT_WITH`, and the tail runs.
///
/// @par The MIDI-learn boundary (E4 task 4 / task 5)
///
/// Four arms of `View::buttonAction` are learn-mode work end to end and belong to `MidiLearn`,
/// not here: the `TAP_TEMPO` learn branch (`view.cpp:148-166`), the `LEARN` button
/// (`:185-196`), `PLAY` under learn (`:199-212`) and `RECORD` under learn (`:215-228`). The rule
/// is the E4 controller's: **the service that owns the mode owns the arm, and the other asks
/// it.** This service asks, through `LearnMode::active()` (`services/learn_mode.h`) -- one
/// virtual call, no second copy of the mode.
///
/// @warning **`MidiLearn` does not exist yet and is not in the router's chain**
///          (`shell/router.h`'s `Services::midi_learn`, "carried here for a later task and is not
///          yet called from `Router::route`"). So the four arms above are returned **unhandled**
///          from here rather than swallowed: swallowing them would look like a working port and
///          would also stop a future `MidiLearn` routed *after* this service from ever seeing
///          them. Until task 5 lands, a `LEARN` press does nothing and `Router::dropped_events()`
///          counts it. That is the gap being visible, which is the point.
///
/// @par What is deliberately NOT ported, and why -- read this before adding to it
///
/// Every entry is a call into something `src/scenes` may not name, with no seam that would let it
/// be spelled honestly. The E4 controller's ruling for the load/save pair applies to all of them:
/// **port the arm's decision and leave the call as a documented no-op with a `@warning`, not as a
/// new outbox.** A second outbox with no consumer is the exact shape migration spec §10.2 already
/// lists as a blocker -- `menu/action.h`'s `MenuAction` mailbox has five requests and no
/// dispatcher -- and adding another before the first has one would leave two seams nobody owns.
///
///   * **`openUI(&saveSongUI)`** (`view.cpp:261`) and **`openUI(&loadSongUI)`** (`:313`). The
///     load/save browser family is not ported; migration spec §10.4 puts it on phase 6, and
///     `saveSongUI`/`loadSongUI` are both on `scenes-no-root-ui`'s forbidden list. The *decision*
///     -- the four-button guard, the hold, the short-press test, the overdub refusal -- is ported
///     in full and specced; only the open is missing. See `handle_save`/`handle_load`.
///   * **`context_menu::clearSong`** (`view.cpp:290-293`), shift+LOAD. `ContextMenuScene` exists
///     (`scenes/context_menu/`), but the *clear-song* menu is not among the context menus E2 task
///     6 ported, and its `setupAndCheckAvailability()` is a `gui/` call. Decision ported, open
///     missing.
///   * **`audioRecorder.beginOutputRecording()`** (`hid/buttons.cpp:161`, `:182`),
///     **`audioRecorder.isCurrentlyResampling()`** (`:195`) and
///     **`audioRecorder.endRecordingSoon()`** (`:196`). `audioRecorder` is on
///     `scenes-no-root-ui`'s list, and `RecorderScene`'s own class doc records the split this sits
///     on the wrong side of: *"`AudioRecorder` the object is also the output recorder ... none of
///     that is a UI ... all of it stays on the legacy `audioRecorder` object"*
///     (`scenes/recorder/recorder_scene.h`). `AudioEngine::foregroundRecordingSource()` is the one
///     seam that already exists and it is read-only, so the two *tests* legacy makes on
///     `recordingSource` (`buttons.cpp:147`, `:180`) ARE ported; the three verbs are not.
///   * **`display->hasPopupOfType(PopupType::THRESHOLD_RECORDING_MODE)`** and the `cancelPopup()`
///     it guards (`hid/buttons.cpp:190-192`). `scenes-no-display` forbids `display->`, and the
///     seam that would replace it does not reach: that popup is raised by *model* code straight to
///     the legacy display (`model/song/song.cpp:5856`), never through `notify::` or
///     `shell::PopupScene`, so even an injected `PopupScene` could not see it. **This one is a
///     behaviour difference, not merely a missing call**: with that popup up, legacy's short
///     RECORD tap dismisses it and does nothing else, while this port falls through to
///     `PlaybackHandler::recordButtonPressed()` and toggles recording. Recorded in the E4 task-4
///     report's owed list.
///   * **The `SYNC_SCALING` fill-mode arm's note halves** (`view.cpp:339-353`):
///     `soundEditor.inNoteEditor()`/`inNoteRowEditor()`,
///     `instrumentClipView.adjustNoteFillWithOffset(1)` and
///     `instrumentClipView.setNoteRowFillWithOffset(1)`. All three name globals on
///     `scenes-no-root-ui`'s list, and neither the note editor nor `InstrumentClipScene` is ported
///     (E5). What survives is `currentSong->changeFillMode(on)` (`:356`), which is the arm's
///     "otherwise" and the only half that is not a view call.
///   * **`deluge_signal_write(DELUGE_SIGNAL_SYNC_LED, ...)`** (`playback_handler.cpp:2508-2509`).
///     The external-clock LED is a board *signal*, not an `IndicatorLED`, so it has no `ButtonId`
///     and therefore no `ui::LedId` (`shell/profiles/deluge.h`: "LedId values = ButtonId values").
///     `LedFrame` cannot express it. Owed to whoever widens the LED surface.
///
/// @par The pre-UI section: verified, not re-ported
///
/// `Buttons::buttonAction`'s section above the UI call (`hid/buttons.cpp:88-132`) is plan D's, and
/// this task's brief said to verify rather than re-port it. Verified, and **two of its five items
/// are ported and three are not**:
///
///   * `:88-95`'s `considerShiftReleaseForSticky = false` on any press -- ported,
///     `InputLowering::on_button` (`shell/input_lowering.cpp:277-279`). Its *cross-screen* twin on
///     the very next line is **not**.
///   * `:100-107`'s mod-button loop -- ported, as `Router`'s `is_mod_or_gold` plus `ModKnobs`
///     (E4 tasks 2 and 3).
///   * `:109-113`'s `AFFECT_ENTIRE` -> `display->cancelPopup()` -- **not ported**. It belongs to
///     the notice seam (`notify::Notice::Kind::CancelPopup`), not to this service.
///   * `:114-120`'s `BACK` -> `TimerName::BACK_MENU_EXIT` long-press, whose callback is
///     `getCurrentUI()->exitUI()` (`gui/ui_timer_manager.cpp:149-155`) -- **not ported**. It is a
///     scene-exit gesture, so it belongs to whoever owns "hold BACK to leave", not here.
///   * `:122-127`'s `CROSS_SCREEN_EDIT` arming `considerCrossScreenReleaseForCrossScreenMode` --
///     **not ported**. Cross-screen mode is a clip-view fact (E5).
///   * `:128-131`'s `selectButtonPressUsedUp = false` -- ported, and generalised: `InputLowering`
///     clears the used-up bit for **every** button on its own press
///     (`input_lowering.cpp:283`), which subsumes both this and `buttons.cpp:178`'s
///     `recordButtonPressUsedUp = false`.
///
/// And the tail's `SHIFT` arm (`hid/buttons.cpp:169-171`, `Buttons::commandToggleShift` at
/// `:237-278`) is already `InputLowering::toggle_shift` (`shell/input_lowering.cpp:304-324`),
/// called from `on_button` *before* the event is ever emitted. `handle()` therefore reports
/// `ButtonId::Shift` handled -- legacy's tail does deal with it -- and does nothing.
///
/// @par `#ifdef undoButtonX` is dead code, and the plan's table listed it as an arm
///
/// The E4 plan's task-4 table names "undo (`:425-441`), redo (`:442-448`)" as arms this service
/// must port. They are inside `#ifdef undoButtonX` (`view.cpp:423-446`), and **`undoButtonX` is
/// defined nowhere in this repository** -- the only two occurrences are that `#ifdef` and the
/// `#ifndef` on the `BACK` arm at `:407`. The `BACK` arm (`:404-421`) *is* the undo/redo, and it
/// is what this service ports. Reported as an error in the task's brief.
///
/// @par The play flash is the clip-arm flash, and it is not a Play-LED flash
///
/// The plan's table sends `View::flashPlayRoutine` here as this service's `leds`/`tick`/
/// `next_timer`, and the task brief asked for "a quiet-tick example proving the Play LED changes
/// with no input at all". **`flashPlayRoutine` does not touch the Play LED.** It toggles
/// `View::clipArmFlashOn` (`view.cpp:2755`) -- the phase armed clips blink at on the *pads*, read
/// by `SessionView::renderRow` (`session_view.cpp:3255`, `:4804`), `View::drawClipPad`-adjacent
/// code (`view.cpp:2623`) and `View::renderMacros` (`:2795`, `:2808`) -- and then asks for a
/// render. The only time-driven thing in legacy that *does* set `IndicatorLED::PLAY` is
/// `View::midiLearnFlash` (`view.cpp:806`), which is E4 task 5's. So what this service owns is the
/// clip-arm phase and its 60 ms (`kFastFlashTime`, `definitions_cxx.hpp:160`) deadline;
/// `clip_arm_flash_on()` is the fact, and the scenes that paint armed clips (E5's `SessionScene`)
/// are its consumers. Reported as an error in the task's brief.
///
/// @note `View::flashPlayRoutine` does **not** re-arm its own timer -- the re-arm lives in
///       `SessionView::flashPlayRoutine` (`gui/views/session_view.cpp:2411-2451`), which calls
///       `view.flashPlayEnable()` again only while some clip is actually armed (`:2423-2424`,
///       `:2441-2444`). That is view-side and not ported, so here `tick()` toggles the phase once
///       per `flash_play_enable()` and stops. A future `SessionScene` re-arms it exactly where
///       legacy does.
///
/// @warning **No wall clock.** `Service::tick(now_ms, dt_ms)` and `UiContext::now_ms` are the only
///          places a clock arrives (`shell-no-clock`). Legacy's `AudioEngine::audioSampleTimer`
///          press timings (`view.cpp:241`, `:298`, `hid/buttons.cpp:176`) become milliseconds
///          here; `kShortPressTime` is `kSampleRate / 2` (`definitions_cxx.hpp:1028`), which is
///          exactly the 500 ms `LoweringConfig::short_press_ms` already carries
///          (`shell/input_lowering.h:19-26`).

#include "scenes/services/learn_mode.h"

#include "ui/core/led_frame.h"

#include "shell/scene.h"
#include "shell/scene_stack.h"
#include "shell/service.h"

#include <cstdint>
#include <optional>

class Song;

namespace deluge::ui::shell {
class InputLowering;
}

namespace deluge::scenes::services {

/// @brief Transport, tempo, sync scaling, undo/redo, load/save holds -- and the LEDs they decide.
class GlobalControls final : public deluge::ui::shell::Service {
public:
	/// @brief The one `currentUIMode` value this service reads.
	///
	/// Legacy has a single `uint32_t currentUIMode` (`gui/ui/ui.h:30`), and this service asks it
	/// in seven places. `src/scenes` may not include `gui/ui/ui.h`, so the value is reconstructed:
	/// `ViewState::ui_mode` carries what the root scene knows, and `effective_ui_mode()` folds in
	/// the two modes this service sets for itself (`HoldingSave`, `HoldingLoad`) and the one
	/// `MidiLearn` owns.
	///
	/// `Other` is every value not listed -- `UI_MODE_STUTTERING`, `UI_MODE_HOLDING_ARRANGEMENT_ROW`
	/// and the rest. It exists so a scene can say "some mode is active" without this enum having
	/// to grow a name for it: every test here is against a *specific* mode, so `Other` correctly
	/// fails all of them.
	enum class UiMode : uint8_t {
		None,          ///< `UI_MODE_NONE` (`gui/ui/ui.h:34`, which is 0).
		MidiLearn,     ///< `UI_MODE_MIDI_LEARN`. Never set through `ViewState`; see `LearnMode`.
		RecordCountIn, ///< `UI_MODE_RECORD_COUNT_IN` -- read by the `TAP_TEMPO` arm (`view.cpp:167`).
		Auditioning,   ///< `UI_MODE_AUDITIONING` -- read by the `LOAD` release arm (`view.cpp:326`).
		HoldingSave,   ///< `UI_MODE_HOLDING_SAVE_BUTTON` (`view.cpp:240`). Owned here.
		HoldingLoad,   ///< `UI_MODE_HOLDING_LOAD_BUTTON` (`view.cpp:297`). Owned here.
		Other,         ///< Any other mode: active, and none of the above.
	};

	/// @brief The root-UI facts this service cannot read for itself, injected exactly as
	///        `ModKnobs::ViewState` injects its four.
	///
	/// Every field is a legacy read `src/scenes` has no way to make. **Nothing sets this yet** --
	/// the root scenes that would (`SessionScene`, `ArrangerScene`, `PerformanceScene`) are
	/// E5/E6's -- so today the defaults hold and the spec is the only caller. The defaults are the
	/// state of a plain root view with no mode active, which is what every scene ported so far is.
	struct ViewState {
		/// `currentUIMode`, as the root scene knows it. `MidiLearn` is deliberately not settable
		/// here: `effective_ui_mode()` takes that from `LearnMode::active()` instead, so there is
		/// one owner of the mode and not two.
		UiMode ui_mode = UiMode::None;
		/// `getCurrentUI()->toClipMinder() != nullptr` (`view.cpp:504`, `:373`). Decides whether
		/// the SYNC_SCALING LED blinks and whether sync scaling may be started at all.
		bool current_ui_is_clip_minder = false;
		/// `getRootUI() == &performanceView` (`view.cpp:236`, `:281`) -- the fifth term of the
		/// SAVE/LOAD guard, which lets KEYBOARD+SAVE through only in performance view.
		bool root_is_performance = false;
		/// `loadSongUI.isLoadingSong()` (`hid/buttons.cpp:218`). Suppresses the tempo popup while
		/// a song is loading.
		bool loading_song = false;
		/// `currentUI == &sessionView || currentUI == &arrangerView` (`hid/buttons.cpp:216`).
		/// Legacy's comment there: only show the tempo pop-up if we are not in Song / Arranger
		/// view.
		bool current_ui_is_song_or_arranger = false;
	};

	/// @param stack    The scene stack this service pushes the settings menu onto
	///                 (`view.cpp:461-462`).
	/// @param lowering The input lowering it reads and writes the used-up bit and the press clock
	///                 on.
	/// @param learn    MIDI learn, asked before the `PLAY`/`RECORD`/`TAP_TEMPO` arms. Defaults to
	///                 `no_learn_mode()` until E4 task 5 exists.
	///
	/// @note `lowering` is a **non-const** reference and `UiContext::lowering` is a `const`
	///       pointer (`shell/scene.h`), so the context cannot serve: `buttons.cpp:165` and `:183`
	///       *write* the used-up bit. `ModKnobs` takes the same argument for the same kind of
	///       reason.
	GlobalControls(deluge::ui::shell::SceneStack& stack, deluge::ui::shell::InputLowering& lowering,
	               LearnMode& learn = no_learn_mode())
	    : stack_(stack), lowering_(lowering), learn_(learn) {}

	/// @name The model this service edits
	/// @{

	/// @brief Which song the arms resolve against -- legacy's `currentSong`, read at
	///        `view.cpp:256`, `:340`, `:356`, `:372`, `:388`, `:393`, `:398`, `:504`, `:510`.
	///
	/// An explicit setter because `scenes-currentSong` forbids this layer the global, exactly as
	/// `ModKnobs::set_song` does.
	/// @warning A stale song makes every sync-scaling decision and the SYNC_SCALING LED read the
	///          wrong one; update it whenever the song changes.
	/// @param song The current song, or null before one exists.
	void set_song(Song* song) { song_ = song; }
	/// @return What was last passed to `set_song`.
	Song* song() const { return song_; }

	/// @brief Tell this service what the root view is -- see `ViewState`.
	/// @param state The view's answers.
	void set_view_state(const ViewState& state) { view_ = state; }
	/// @return What was last passed to `set_view_state`.
	const ViewState& view_state() const { return view_; }

	/// @brief The single `currentUIMode` value, assembled from its three owners.
	///
	/// `MidiLearn` outranks everything (it is the mode legacy's own arms test first, at
	/// `view.cpp:148`), then this service's own two holding modes, then whatever the root scene
	/// said.
	/// @return The mode as legacy's `currentUIMode` would read.
	UiMode effective_ui_mode() const;

	/// @}

	/// @name The clip-arm flash -- `View::flashPlay*`
	/// @{

	/// @brief Arm the flash -- `View::flashPlayEnable` (`view.cpp:2766-2768`).
	///
	/// Legacy sets `TimerName::PLAY_ENABLE_FLASH` to `kFastFlashTime`; here that is a deadline
	/// `tick()` fires and `next_timer()` reports.
	/// @param now_ms The tick's wall clock to measure the deadline from.
	void flash_play_enable(uint32_t now_ms);
	/// @brief Disarm it and clear the phase -- `View::flashPlayDisable` (`view.cpp:2770-2783`).
	///
	/// Legacy's tail (`:2774-2782`) is a render request against `sessionView`/`performanceView`,
	/// which is view-side and not ported; a scene reading `clip_arm_flash_on()` sees the change on
	/// its next tick.
	void flash_play_disable();
	/// @brief `View::clipArmFlashOn` (`gui/views/view.h:125`) -- the phase armed clips blink at.
	///
	/// The consumers are the scenes that paint armed clips, which are E5's; nothing in this
	/// repository reads it yet.
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
	/// `PlaybackHandler::setLedStates` (`playback/playback_handler.cpp:2499-2512`) and
	/// `View::setTimeBaseScaleLedState` (`view.cpp:502-512`), which is all `View::setLedStates`
	/// (`:514-516`) is. Legacy calls these from wherever something changed; here they are
	/// recomputed from the model every tick, which is what an `LedFrame` is.
	/// @param frame The tick's declarations.
	void leds(deluge::ui::LedFrame& frame) override;

	void tick(uint32_t now_ms, uint32_t dt_ms) override;
	std::optional<uint32_t> next_timer() const override;
	/// @}

	/// @name Observable state, for specs and for the root scenes that will read it
	/// @{
	/// @brief Is the SAVE button being held down for the save gesture? `UI_MODE_HOLDING_SAVE_BUTTON`
	///        (`view.cpp:240`, cleared at `:253`).
	bool holding_save() const { return holding_save_; }
	/// @brief Is the LOAD button being held down? `UI_MODE_HOLDING_LOAD_BUTTON` (`view.cpp:297`,
	///        cleared at `:310`).
	bool holding_load() const { return holding_load_; }
	/// @brief The five call sites this port reaches but cannot make -- see the class doc's
	///        "deliberately NOT ported".
	///
	/// One enumerator per *site*, not one per kind of thing: `beginOutputRecording` is reached from
	/// two different arms with two different conditions, and a single total cannot tell them apart.
	/// Fix round 1, minor 5: a spec asserting `unported_opens() == 1` after a SAVE gesture could be
	/// satisfied by a PLAY+RECORD combo instead, which would make the counter's only claim -- "the
	/// arm's decision reached the point legacy opens a UI at" -- unfalsifiable.
	///
	/// @note The doc for each enumerator is on its own line above it, not trailing: a trailing
	///       `///<` shares a line with code, and `scripts/check_layer_rules.py`'s `is_comment_line`
	///       skips only a line that is *nothing but* comment -- so the forbidden global names these
	///       docs necessarily quote would have counted as `scenes-no-root-ui` violations.
	enum class UnportedOpen : uint8_t {
		/// The song saver's open (`view.cpp:261`).
		SaveSong,
		/// The song loader's open (`view.cpp:313`).
		LoadSong,
		/// The clear-song context menu's open (`view.cpp:292`).
		ClearSong,
		/// Output recording begun from the PLAY arm (`hid/buttons.cpp:161`).
		OutputRecordFromPlay,
		/// Output recording begun from shift+RECORD (`hid/buttons.cpp:182`).
		OutputRecordFromShift,
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

	/// @brief `kShortPressTime` (`definitions_cxx.hpp:1028`, `kSampleRate / 2`) in milliseconds.
	///
	/// The same 500 ms `LoweringConfig::short_press_ms` already carries; named here because the
	/// SAVE and LOAD arms measure against their own press clock (`view.cpp:241`, `:298`) rather
	/// than through `InputLowering::was_short_press`, which only knows the button's own press.
	static constexpr uint32_t kShortPressMs = 500;
	/// @brief `kFastFlashTime` (`definitions_cxx.hpp:160`) -- the clip-arm flash's half-period.
	static constexpr uint32_t kClipArmFlashMs = 60;
	/// @brief How long `indicator_leds::indicateAlertOnLed` blinks for.
	///
	/// `indicateAlertOnLed` is `blinkLed(led, 3, 1)` (`hid/led/indicator_leds.cpp:160-162`), which
	/// sets `blinksLeft = numBlinks * 2` == 6 (`:75`) and then calls `updateBlinkingLedStates`
	/// **once, at arm time** (`:79`), which decrements it to 5 (`:123`). The fold-back at `:126-129`
	/// therefore happens after **five** further `kFastFlashTime` timeouts (`:106-112`), not six:
	/// 300 ms, not 360. Pinned against legacy's own countdown in `global_controls_spec.cpp`
	/// (fix round 1, minor 3 -- it was 360 and nothing checked it).
	static constexpr uint32_t kAlertMs = (3 * 2 - 1) * kClipArmFlashMs;

private:
	/// @brief What `View::buttonAction` did with a button, in the three shapes that matter here.
	///
	/// Legacy has only two answers -- `DEALT_WITH` and `NOT_DEALT_WITH` (`view.cpp:466`, `:469`) --
	/// and the tail runs only for the second (`hid/buttons.cpp:139-141`). The port needs a third,
	/// because four of the arms are `MidiLearn`'s: legacy *deals with* them, so the tail must be
	/// skipped, but this service does none of the work, so the event must still be reported
	/// unhandled to the router. Collapsing the two would either run the transport tail under MIDI
	/// learn (wrong) or claim an event nothing acted on (invisible).
	enum class ViewArm : uint8_t {
		NotAnArm,  ///< `view.cpp:465-467` -- run the tail.
		Handled,   ///< `view.cpp:469` -- skip the tail, report handled.
		LearnOwns, ///< `view.cpp:469` for an arm that is E4 task 5's -- skip the tail, report unhandled.
	};

	/// @brief `View::buttonAction`'s arms (`gui/views/view.cpp:139-470`).
	/// @param button The button.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param r      Where an arm's popup or push is appended.
	/// @return Which of the three shapes above the arm was.
	ViewArm view_button_action(deluge::ui::shell::ButtonId button, bool on, const deluge::ui::shell::UiContext& ctx,
	                           deluge::ui::shell::Result& r);
	/// @brief `Buttons::buttonAction`'s tail (`hid/buttons.cpp:143-231`).
	/// @param button The button.
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @return `handled` for every button the tail names.
	deluge::ui::shell::Result tail_button_action(deluge::ui::shell::ButtonId button, bool on,
	                                             const deluge::ui::shell::UiContext& ctx);

	/// @brief The `SAVE` arm (`view.cpp:231-273`).
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param result Where the pending-overdubs refusal popup (`:257-258`) is appended.
	void handle_save(bool on, const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);
	/// @brief The `LOAD` arm (`view.cpp:276-331`).
	/// @param on  Press or release.
	/// @param ctx The world around it.
	void handle_load(bool on, const deluge::ui::shell::UiContext& ctx);
	/// @brief The four-button guard both share (`view.cpp:233-236`, `:278-281`).
	/// @param ctx The world around it.
	/// @return True when neither SYNTH/KIT/MIDI/CV is held and KEYBOARD is not held in performance
	///         view -- i.e. when the arm's body may run.
	bool load_save_allowed(const deluge::ui::shell::UiContext& ctx) const;
	/// @brief The `SYNC_SCALING` arm (`view.cpp:334-401`).
	/// @param on     Press or release.
	/// @param ctx    The world around it.
	/// @param result Where a refusal popup is appended.
	void handle_sync_scaling(bool on, const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);
	/// @brief The `SELECT_ENC`+shift arm (`view.cpp:449-464`): open the settings menu.
	/// @param ctx    The world around it.
	/// @param result Where the refusal popup or the push is appended.
	void handle_settings_menu(const deluge::ui::shell::UiContext& ctx, deluge::ui::shell::Result& result);

	/// @brief Bump @p which's counter -- see `unported_opens()`.
	/// @param which The site that was reached.
	void count_unported(UnportedOpen which);

	/// @brief Is the SYNC_SCALING LED in `View::setTimeBaseScaleLedState`'s blink arm
	///        (`view.cpp:504-506`) right now?
	///
	/// Asked by `leds()` and by `next_timer()`, which has to request a wake for it -- see the note
	/// there.
	/// @return True when the LED is declared `Blink` rather than solid.
	bool blinking_sync_scaling() const;

	deluge::ui::shell::SceneStack& stack_;
	deluge::ui::shell::InputLowering& lowering_;
	LearnMode& learn_;

	Song* song_ = nullptr;
	ViewState view_{};

	/// `UI_MODE_HOLDING_SAVE_BUTTON` / `UI_MODE_HOLDING_LOAD_BUTTON` (`view.cpp:240`, `:297`).
	bool holding_save_ = false;
	bool holding_load_ = false;
	/// `View::timeSaveButtonPressed` (`gui/views/view.h:128`) -- **one** member, shared by both
	/// arms exactly as legacy shares it: the LOAD press writes the same variable (`view.cpp:298`)
	/// the SAVE press does (`:241`). They can never both be held, because each arm's press branch
	/// requires `UI_MODE_NONE`.
	uint32_t save_pressed_ms_ = 0;

	/// `View::clipArmFlashOn` and its `TimerName::PLAY_ENABLE_FLASH` deadline.
	bool clip_arm_flash_on_ = false;
	std::optional<uint32_t> clip_arm_flash_deadline_ms_;

	/// `indicator_leds::indicateAlertOnLed(IndicatorLED::CLIP_VIEW)` (`view.cpp:374`), as a
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
// Nothing links `deluge_scenes` into an application yet, so today that caller is the spec.)

} // namespace deluge::scenes::services
