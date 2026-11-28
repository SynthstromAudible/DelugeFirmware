#pragma once

/// @brief `RecorderScene` -- the audio recorder: arm an input, capture it to a file, and hand the
///        finished `Sample` to the oscillator that opened it.
///
/// Mirrors main's `AudioRecorder` (`origin/main:src/deluge/gui/ui/audio_recorder.h`), which the sample-recorder page's
/// two items open (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h` `beginSession`; the nodes are
/// `nodes::sample0_recorder` / `sample1_recorder`, `menu/nodes/sound_osc.h`).
///
/// @par In main the menu item drives the UI
///
/// `osc::AudioRecorder::beginSession` does **two** things
/// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`):
///
/// ```
///     if (bool success = openUI(&audioRecorder); !success) { ...; return ...; }
///     audioRecorder.process();
/// ```
///
/// `openUI` runs `AudioRecorder::opened()`, which allocates the `SampleRecorder`; `AudioRecorder::process()`
/// (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`) is then an **infinite loop inside the menu item's own stack
/// frame** that pumps the audio engine, the UI timers, the display and the button/pad scan by hand, polls
/// `recorder->status` every pass, and only returns after it has called `close()`. The whole recorder UI is therefore a
/// blocking call: no other UI runs, `SoundEditor` is suspended mid-`beginSession`, and the "screen" is two
/// `OLED::sendMainImage()` calls made from inside the loop rather than from a render hook.
///
/// **`RecorderScene` keeps the state machine and throws the loop away.** Its five statements map like
/// this:
///
/// - `process()`'s loop body, and where each part goes here:
///   - `uiTimerManager.routine()`, `deluge_display_service()`,
///     `deluge_control_flush()`, `readButtonsAndPads()`, `AudioEngine::slowRoutine()` -- the shell's own tick;
///     none of it is this scene's.
///   - `if (recorder->status >= COMPLETE || hadCardError)` -> attach the sample, `finishRecording()`,
///     `close()` -- `after_render()`'s completion arm, which files `Action::Kind::Remove`.
///   - `recordingClippedRecently` -> popup -- `after_render()`'s clipping arm, which files
///     `Action::Kind::Popup`.
///   - `!updatedRecordingStatus && numSamplesCaptured` -> redraw with "Recording" -- `render_oled`
///     reads `capturing()`; see `updatedRecordingStatus` below.
///   - the `while (true)` itself -- `next_timer()` + the repaint-gate arm in `render_oled`, both at
///     `kRecorderPollMs`.
///
/// `after_render()` is the hook the poll has to live in, for `BrowserScene`'s reason: it is the only
/// one that can file an `Action`, and the shell calls it only for a scene that actually *drew* this
/// tick (`Shell::tick`'s `after_render` loop). So `next_timer()` asks the shell back at `kRecorderPollMs`, and
/// `Shell::tick`'s step 8b turns that answer into a repaint of every surface this scene both claims
/// and is not covered on -- which is what makes the woken tick actually render, and therefore call
/// `after_render()`.
///
/// `render_oled` **also** arms the panel's gate itself, at the same interval. That arm is a
/// refinement rather than the load-bearing part; it is kept because it is
/// the tighter statement (the panel is armed from the moment the frame is drawn rather than from the
/// bottom of the tick) and because it is what a reader of this scene alone can see. A timer written
/// without the arm is a dead animation for most scenes, but here it is a recording that
/// never notices it has finished. See `Scene::next_timer`.
///
/// `AudioRecorder::updatedRecordingStatus` (`origin/main:src/deluge/gui/ui/audio_recorder.h`) has no member here.
/// Legacy needs it because `process()` paints imperatively and must not repaint every pass; it is set exactly when
/// `numSamplesCaptured` first goes non-zero and cleared in `opened()`, so it is a cache of `numSamplesCaptured != 0`.
/// `render_oled` reads the take's state (`bind::AudioFacts::input_take`) instead, which also drops the one-tick lag a
/// cached copy written in `after_render` would have had.
///
/// @par What this scene is NOT
///
/// The **output recording** -- resampling and stem export, started from `Buttons::buttonAction`,
/// `StemExport` and the views -- is the engine's foreground recording (`deluge::recording`,
/// `processing/recording/foreground_recording.h`): `beginOutputRecording` starts it,
/// `slowRoutine` reaps it, and `isCurrentlyResampling` is read from several places. **None of that
/// is a UI**, none of it is opened from a menu item, and this scene does not touch it. This scene
/// ports only the menu-driven mic/line capture: `AudioRecorder`'s `opened()`, `process()`,
/// `renderOLED`, `buttonAction`, `endRecordingSoon`, `finishRecording` and
/// `getGreyoutColsAndRows`.
///
/// @par The foreground recording is not bookkeeping, and the take port publishes it
///
/// The foreground recording's channel and recorder are read by **many subsystems** as "a foreground recording is
/// live". Most consequentially input monitoring (`AudioEngine::setMonitoringMode`,
/// `origin/main:src/deluge/processing/engines/audio_engine.cpp`) is enabled only while the channel is `STEREO` or
/// `LEFT` -- exactly the two values `AudioRecorder::opened` sets. The others are shift+RECORD (which must not start an
/// *output* recording on top of a live mic one), stem export, the RECORD LED, the arrangement's tick, the keyboard
/// screen, the instrument clip view's RECORD and audition arms and the sample browser's RECORD arm.
///
/// So the take's lifecycle is one port, `bind::TakeActions`: `begin` makes the take and publishes it in one step,
/// `finish` unpublishes and frees a settled take, and `on_exit` and `taken_down` `orphan` a take still running, which
/// the host then ends and frees once it has settled. The take's state is read on `bind::AudioFacts::input_take`, and
/// `can()` reads `bind::AudioFacts::foreground_recording` for legacy's own "already recording" refusal in
/// `AudioRecorder::opened`. An internal-recording predicate is not the test: it does not see an input recording at
/// all, so shift+RECORD could start an output recording over the top.
///
/// `armed_` is a second, scene-local test in `on_enter`: the same fact once the publication exists, kept because it is
/// the one this scene can assert about *itself*.
///
/// @par SYNC_SCALING
///
/// A SYNC_SCALING release with fill mode on goes to the root view (`recorderPassesFillRelease`), as main's
/// `AudioRecorder::buttonAction` sends it; the scene itself does not name a root UI.
///
/// @par The held audition
///
/// `AudioRecorder::opened`'s `instrumentClipView.cancelAllAuditioning()` is the root view's: the scene asks it through
/// `bind::ViewActions::end_audition` (`AuditionEnd::ClipViewWhenAuditioning`), whether the take began or not.
///
/// @par What is deliberately not ported
///
///   * **`checkPermissionToBeginSession`'s FM refusal** (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`).
///     It is unreachable from the menu: `isRelevant` and the ported `relevance::is_subtractive` on
///     `nodes::sample0_recorder` hide the item in FM mode in the first place, so the popup only ever fired on legacy's
///     shortcut-pad path, which `MenuScene` owns.
///
/// @par The completion poll runs under this scene's own popup
///
/// `PopupScene` is not `oled_transparent` (`ui/shell/notices.h`), so `Shell::render_oled`'s
/// transparency walk stops at it: while a popup is up **this scene is
/// not drawn on the OLED**. That is why the `render_oled` gate arm
/// above cannot be the poll's only lifeline -- a recording that finished while this scene's own
/// clipping popup was on screen would otherwise go unnoticed until the popup expired
/// (`kPopupTimeoutMs`, 800 ms).
///
/// Step 8b of `Shell::tick` closes that gap, through the *grid* rather than the panel. This scene and the
/// `PopupScene` above it are both grid overlays, and `Shell::render_grid` draws and `append_rendered`s every scene
/// from the grid base up on every tick the grid gate fires. Step 8b arms each surface this scene claims and is not
/// covered on -- the grid, here, and *not* the OLED panels the opaque popup covers -- so the grid gate fires at
/// `kRecorderPollMs`, this scene enters `rendered_this_tick_`, and `after_render()` runs on schedule with the popup
/// still up. Arming the OLED half would be pure cost: `flush` blits an OLED surface unconditionally once rendered, so
/// it would be a 100 Hz two-panel blit of a frame this scene is not even in. Nothing about the OLED walk has to change,
/// and neither alternative (making the scene `oled_transparent`, or moving the poll out of `after_render` where it
/// could no longer file the `Remove`) is needed.
///
/// `recorder_scene_spec` pins this -- "notices
/// completion while its own clipping popup is up, through the grid pass".
///
/// @warning **This scene must not raise `Action::Kind::LockInput`**, and does not. See the warning
///          on `Action` in `ui/shell/scene.h`. Legacy's own gate here is
///          `buttonAction`'s `inCardRoutine` -> `REMIND_ME_OUTSIDE_CARD_ROUTINE`
///          (`AudioRecorder::buttonAction`), which **defers and retries**; its port is
///          `InputLowering::set_sd_busy`, which is deliberately not an `Action` and is not this
///          scene's to file. `LockInput` would *drop* those presses instead, and could not be
///          released from `on_exit()`.

#include "ui/scenes/bind/ports/audio_facts.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/bind/ports/take_actions.h"
#include "ui/scenes/menu/context.h"
#include "ui/shell/scene.h"
#include "ui/views/grid/blink.h"
#include "ui/views/oled/recorder.h"
#include "ui/widgets/grid/effects.h"

#include <cstdint>
#include <optional>

namespace deluge::ui::scenes::recorder {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 5` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kRecorderSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 5);

/// @brief How often the scene asks to be woken while a recorder exists.
///
/// **This is what replaces `process()`'s `while (true)`, and it is not decoration.** The recorder's
/// completion is a fact only a poll can discover -- nothing publishes it, and no input arrives while
/// the user is holding a microphone -- and `after_render()` is the only hook that may file the
/// `Remove` that closes the scene. The shell calls `after_render()` only for a scene that rendered
/// this tick, so `next_timer()` asks for the tick at this interval and `Shell::tick`'s step 8b turns
/// that answer into the repaint that makes the woken tick draw (`render_oled` arms the panel itself
/// as well, more tightly -- see the class note). Before the fold, `next_timer()` alone was a scene
/// that armed a recording it could never finish.
///
/// 10 ms is one tick of the shipped UI loop, i.e. "next tick", spelled as a duration because that is
/// what both APIs take. The same number and the same argument as `browser::kListingPollMs`.
inline constexpr uint32_t kRecorderPollMs = 10;

/// @brief The row of the recorder's shortcut pad, on its source's column: `AudioRecorder::opened`'s
///        `setupShortcutBlink(currentSourceIndex, 4, 0)`, the cell `kSoundCells` (`menu/shortcuts.cpp`) gives
///        `sample0_recorder` and `sample1_recorder`.
inline constexpr uint8_t kRecordShortcutY = 4;

/// @brief The audio recorder. One instance for the whole application (`registry.h`).
class RecorderScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// The entry point for whoever opens the recorder, exactly as `RenameScene::open`,
	/// `BrowserScene::open` and `MarkerScene::open` are: a `SceneId` carries one `uint16_t` and a
	/// context is not one.
	/// @param ctx The sound, source and range the finished sample is attached to.
	void open(const menu::MenuContext& ctx);

	/// @brief May the recorder open on @p ctx at all?
	///
	/// The survivable spelling of `AudioRecorder::opened`'s two refusals plus
	/// the context reads `process()`'s completion arm makes unchecked: a sound, a
	/// source and a range to attach the finished sample to. Legacy gets the last of those from
	/// `soundEditor.checkPermissionToBeginSessionForRangeSpecificParam`
	/// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`), which refuses before opening.
	/// @param ctx The context to test.
	/// @return True if `on_enter` will get as far as trying to allocate a recorder.
	static bool can(const menu::MenuContext& ctx);
	/// @brief Whether the last `on_enter` refused: the take could not begin (`can`, the drum's name, the recorder's
	///        allocation). The menu reads it as it resumes from that refused push.
	[[nodiscard]] bool last_enter_refused() const { return last_enter_refused_; }

	deluge::ui::shell::SceneKind kind() const override { return kRecorderSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief Every UI is being taken down with this scene up: end a running take as `on_exit` does, but post no
	///        `ClearWorking`. The host disarms the shell's working animation itself (`Notices::disarm_working`), and
	///        whatever animation the legacy display shows is the taker-down's own.
	void taken_down();

	/// @brief `AudioRecorder::getGreyoutColsAndRows`: every column of the grid, sidebar included, greyed.
	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override { return {0xFFFFFFFFu, 0}; }
	/// @brief True: `AudioRecorder` overrides neither `renderMainPads` nor `renderSidebar`, so the view beneath keeps
	///        every pad and `Shell::tick` greys it with `greyout_mask`. The scene's only grid output is its blink.
	bool is_grid_overlay() const override { return true; }
	/// @brief `AudioRecorder::opened`'s `setupShortcutBlink(currentSourceIndex, 4, 0)` and `blinkShortcut()`: the
	///        source's recorder shortcut pad flashes on every cycle of the sound editor's blink counter while a take
	///        is armed.
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;

	/// @brief What the recorder screen shows: the status word, or `AudioRecorder::getThresholdMeterState`'s meter
	///        (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`), for a panel @p width wide. The word points at
	///        literals.
	[[nodiscard]] deluge::ui::views::oled::RecorderStatus present_status(int32_t width) const;
	/// @brief The shortcut pad's blink at @p now_ms: the plan, the counter's step on the anchor, and whether that step
	///        has not played yet. Reads the anchor `render_grid` set.
	[[nodiscard]] deluge::ui::views::grid::BlinkPads present_blink(uint32_t now_ms) const;

	/// @name Observable state
	/// For specs, and for whoever wires the menu items up.
	/// @{
	/// @return Is a take held -- i.e. is the scene between `on_enter` and the sample landing?
	bool recording() const { return armed_; }
	/// @return Has the take started? Legacy's `updatedRecordingStatus`, read rather than cached: reads
	///         `InputTake::capturing`, past the threshold's margin, since a threshold take holds its pre-roll before
	///         the input passes the threshold.
	bool capturing() const;
	/// @return Is this a threshold take -- the song's threshold mode was on as the take was armed? Legacy's
	///         `isThresholdRecording`: the recorder keeps the threshold it was given after the input has passed it.
	bool threshold_take() const;
	/// @return Has a stop been asked for -- `TakeActions::end_soon` having run?
	bool stopping() const { return stopping_; }
	/// @}

private:
	/// @brief `AudioRecorder::setupRecordingToFile`, through `TakeActions::begin`; sets `armed_`.
	/// @return False when no take could begin (legacy's `INSUFFICIENT_RAM` arm, or no take or audio port).
	bool setup_recording();
	/// @brief The held take's state.
	/// @return None with no take held or no audio port.
	[[nodiscard]] std::optional<bind::InputTake> take() const;
	/// @brief The take port.
	/// @return The context's `take_actions`, or null.
	[[nodiscard]] bind::TakeActions* takes() const;
	/// @brief `AudioRecorder::endRecordingSoon`, through `TakeActions::end_soon`.
	void end_recording_soon();
	/// @brief `AudioRecorder::finishRecording`, through `TakeActions::finish`.
	void finish_recording();
	/// @brief End a running take with its screen gone: its file stays on the card, attached to nothing
	///        (`TakeActions::orphan`). Forgets the target and the stop either way.
	void orphan_take();
	/// @brief `opened()`'s kit arm: name the drum being recorded into "REC".
	/// @return False on the error legacy shows and refuses on.
	bool name_drum();
	/// @brief `process()`'s success arm: hand the finished sample to the source.
	void attach_sample();
	/// @brief `opened()`'s `target_`: what the take is for, each part by its identity.
	bind::TakeTarget take_target() const;

	menu::MenuContext ctx_{};

	/// Whether a take is held: `TakeActions::begin` succeeded and neither `finish` nor `orphan` has run since -- see
	/// the class docstring.
	bool armed_ = false;

	/// Legacy has no member for this: `endRecordingSoon` is idempotent by testing
	/// `recorder->status == CAPTURING_DATA`. Kept here only so `stopping()` can be
	/// observed and so the "Working" notice is raised exactly once.
	bool stopping_ = false;
	/// Whether the next `leds()` restarts both shared blinks: the recording began (`blinkLed(BACK)`,
	/// `blinkLed(RECORD, 255, 1)`).
	bool blink_restart_ = false;
	/// Whether the last `on_enter` refused; see `last_enter_refused`.
	bool last_enter_refused_ = false;
	/// What the take is for: it attaches there or nowhere (`SoundFacts::take_range`).
	bind::TakeTarget target_{};
	/// When the shortcut blink's counter started: the take's first grid pass.
	std::optional<uint32_t> blink_anchor_ms_{};
	/// The counter step last fired, so two repaints inside one step flash once.
	uint32_t blink_fired_step_ = UINT32_MAX;
};

/// @brief The application's one audio recorder.
/// @return The singleton.
RecorderScene& recorder_scene();

} // namespace deluge::ui::scenes::recorder
