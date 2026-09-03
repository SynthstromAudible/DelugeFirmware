#pragma once

/// @brief `RecorderScene` -- the audio recorder: arm an input, capture it to a file, and hand the
///        finished `Sample` to the oscillator that opened it.
///
/// The port of `AudioRecorder` (`gui/ui/audio_recorder.{h,cpp}`), which the sample-recorder page's
/// two items open (`gui/menu_item/osc/audio_recorder.h:36 beginSession`; the ported nodes are
/// `nodes::sample0_recorder` / `sample1_recorder`, `menu/nodes/sound_osc.h:303`, `:307`).
///
/// @par Legacy's menu item drives the UI, and that is the shape this replaces
///
/// `osc::AudioRecorder::beginSession` does **two** things (`osc/audio_recorder.h:50-57`):
///
/// ```
///     if (bool success = openUI(&audioRecorder); !success) { ...; return ...; }
///     audioRecorder.process();
/// ```
///
/// `openUI` runs `AudioRecorder::opened()` (`audio_recorder.cpp:63`), which allocates the
/// `SampleRecorder`; `process()` (`:193`) is then an **infinite loop inside the menu item's own
/// stack frame** that pumps the audio engine, the UI timers, the display and the button/pad scan by
/// hand, polls `recorder->status` every pass, and only returns after it has called `close()`. The
/// whole recorder UI is therefore a blocking call: no other UI runs, `SoundEditor` is suspended
/// mid-`beginSession`, and the "screen" is two `OLED::sendMainImage()` calls made from inside the
/// loop rather than from a render hook.
///
/// **The port keeps the state machine and throws the loop away.** Its five statements map like
/// this:
///
/// | `process()`'s loop body (`:194-241`) | here |
/// |---|---|
/// | `AudioEngine::routineWithClusterLoading()`, `uiTimerManager.routine()`, `deluge_display_service()`,
/// `deluge_control_flush()`, `readButtonsAndPads()`, `AudioEngine::slowRoutine()` | the shell's own tick -- none of it
/// is this scene's | | `if (recorder->status >= COMPLETE \|\| hadCardError)` -> attach the sample, `finishRecording()`,
/// `close()` | `after_render()`'s completion arm, which files `Action::Kind::Remove` | | `recordingClippedRecently` ->
/// popup | `after_render()`'s clipping arm, which files `Action::Kind::Popup` | | `!updatedRecordingStatus &&
/// numSamplesCaptured` -> redraw with "Recording" | `render_oled` reads `capturing()`; see `updatedRecordingStatus`
/// below | | the `while (true)` itself | `next_timer()` + the repaint-gate arm in `render_oled`, both at
/// `kRecorderPollMs` |
///
/// `after_render()` is the hook the poll has to live in, for `BrowserScene`'s reason: it is the only
/// one that can file an `Action`, and the shell calls it only for a scene that actually *drew* this
/// tick (`shell/shell.cpp:360-364`). So `render_oled` arms the panel's repaint gate for as long as a
/// recorder exists, and `next_timer()` asks the shell back. **A timer alone would not do it** --
/// see the `@warning` on `Scene::next_timer` -- and here the consequence is not a dead animation but
/// a recording that never notices it has finished.
///
/// `updatedRecordingStatus` (`audio_recorder.h:48`) has no member here. Legacy needs it because
/// `process()` paints imperatively and must not repaint every pass; it is set exactly when
/// `numSamplesCaptured` first goes non-zero (`:234-239`) and cleared in `opened()` (`:64`), so it is
/// a cache of `numSamplesCaptured != 0`. `render_oled` reads the recorder instead, which also drops
/// the one-tick lag a cached copy written in `after_render` would have had.
///
/// @par What this scene is NOT
///
/// `AudioRecorder` the object is also the **output recorder**: `beginOutputRecording` (`:143`) is
/// called by `playbackHandler` and `StemExport` for resampling and stem export, `slowRoutine`
/// (`:168`) reaps that recording, and `isCurrentlyResampling` (`:282`) is read from eight places.
/// **None of that is a UI**, none of it is opened from a menu item, and all of it stays on the
/// legacy `audioRecorder` object. This scene ports only the menu-driven mic/line capture:
/// `opened()`, `process()`, `renderOLED`, `buttonAction`, `endRecordingSoon`, `finishRecording` and
/// `getGreyoutColsAndRows`.
///
/// That split is also why `can()` asks `AudioEngine::isAnyInternalRecordingHappening()` rather than
/// legacy's `recordingSource > AudioInputChannel::NONE` (`:69-71`): `recordingSource` lives on
/// `audioRecorder`, which this layer may not name (`check_layer_rules.py`,
/// `scenes-no-root-ui`), and every way it is set from outside this UI goes through
/// `beginOutputRecording`, i.e. an *internal* channel (`AudioInputChannel::OUTPUT` and
/// `OFFLINE_OUTPUT` both sort at or above `AUDIO_INPUT_CHANNEL_FIRST_INTERNAL_OPTION`,
/// `definitions_cxx.hpp:865`). The other half of legacy's guard -- "the recorder UI is already
/// open" -- is `recorder_ != nullptr` here.
///
/// @par What is deliberately not ported
///
///   * **The shortcut blink** (`opened()`, `:97-98`). `setupShortcutBlink` / `blinkShortcut` belong
///     to `MenuScene`, which owns the shortcut tables and the pad flash that draws them. (The seven
///     `indicator_leds::` calls at `:100-106` **are** ported: they are `hid/`, so they are declared
///     through `leds()` instead of called.)
///   * **`PadLEDs::clearTickSquares(true)`** (`:90`). Tick squares are a `PadLEDs` facility with no
///     shell equivalent yet -- the same gap `MarkerScene` records for the playhead.
///   * **`instrumentClipView.cancelAllAuditioning()`** (`:109-111`), a root-UI call this layer may
///     not make (`scenes-no-root-ui`).
///   * **`buttonAction`'s `SYNC_SCALING` release arm** (`:259-262`), which forwards to
///     `getRootUI()` so a held fill mode can be turned off. The scene simply does not handle the
///     event, and the shell routes it to the scene beneath -- which is the same outcome without
///     naming a root UI.
///   * **`checkPermissionToBeginSession`'s FM refusal** (`osc/audio_recorder.h:65-74`). It is
///     unreachable from the menu: `isRelevant` and the ported `relevance::is_subtractive` on
///     `nodes::sample0_recorder` hide the item in FM mode in the first place, so the popup only
///     ever fired on legacy's shortcut-pad path, which `MenuScene` owns.
///
/// @warning **This scene must not raise `Action::Kind::LockInput`**, and does not. See the warning
///          on `Action` in `shell/scene.h`. Legacy's own gate here is
///          `buttonAction`'s `inCardRoutine` -> `REMIND_ME_OUTSIDE_CARD_ROUTINE`
///          (`audio_recorder.cpp:270-272`), which **defers and retries**; its port is
///          `InputLowering::set_sd_busy`, which is deliberately not an `Action` and is not this
///          scene's to file. `LockInput` would *drop* those presses instead, and could not be
///          released from `on_exit()`.

#include "scenes/menu/context.h"
#include "shell/scene.h"
#include "ui/grid/effects.h"

#include <cstdint>
#include <optional>

class SampleRecorder;

namespace deluge::scenes::recorder {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 5` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kRecorderSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 5);

/// @brief How often the scene asks to be woken while a recorder exists.
///
/// **This is what replaces `process()`'s `while (true)`, and it is not decoration.** The recorder's
/// completion is a fact only a poll can discover -- nothing publishes it, and no input arrives while
/// the user is holding a microphone -- and `after_render()` is the only hook that may file the
/// `Remove` that closes the scene. The shell calls `after_render()` only for a scene that rendered
/// this tick, so `render_oled` arms the panel's repaint gate at this interval *and* `next_timer()`
/// asks for the tick. Either one alone is a scene that arms a recording it can never finish.
///
/// 10 ms is one tick of the shipped UI loop, i.e. "next tick", spelled as a duration because that is
/// what both APIs take. The same number and the same argument as `browser::kListingPollMs`.
inline constexpr uint32_t kRecorderPollMs = 10;

/// @brief The row `render_oled` draws its status word on, in frame-local coordinates.
///
/// `AudioRecorder::renderOLED` (`audio_recorder.cpp:116-119`) is one call:
/// `drawStringCentred(text, 19, kTextBigSpacingX, kTextBigSizeY)`. That 19 is an *absolute* canvas
/// row and the frame's origin already accounts for the bezel, so the frame-local row is
/// `19 - OLED_MAIN_TOPMOST_PIXEL` = 14 on the shipped panel -- exactly the subtraction
/// `MarkerScene::body_y` makes in the other direction (its legacy expression spells the bezel out,
/// this one does not).
///
/// @note On a panel with no bezel the word therefore sits 14 rows from the top of the frame rather
///       than at absolute row 19; legacy's bare literal has no opinion about such a panel at all.
inline constexpr int32_t kStatusY = 14;

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
	/// The survivable spelling of `AudioRecorder::opened`'s two refusals (`:69-71`, `:81-85`) plus
	/// the context reads `process()`'s completion arm makes unchecked (`:213-217`): a sound, a
	/// source and a range to attach the finished sample to. Legacy gets the last of those from
	/// `soundEditor.checkPermissionToBeginSessionForRangeSpecificParam`
	/// (`osc/audio_recorder.h:73`), which refuses before opening.
	/// @param ctx The context to test.
	/// @return True if `on_enter` will get as far as trying to allocate a recorder.
	static bool can(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kRecorderSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief `AudioRecorder::getGreyoutColsAndRows` (`:58-61`): `*cols = 0xFFFFFFFF`, rows
	///        untouched, returns true -- every column of the grid, sidebar included, greyed.
	///
	/// The scene itself paints no pads at all (legacy overrides neither `renderMainPads` nor
	/// `renderSidebar`), so it is not a grid overlay and occludes nothing: what is beneath keeps
	/// drawing, greyed. `Shell::tick` ORs this mask over every scene on the stack
	/// (`shell/shell.cpp:311-317`), which is where the greying happens.
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return {0xFFFFFFFFu, 0}; }

	/// @name Observable state
	/// For specs, and for whoever wires the menu items up.
	/// @{
	/// @return Is a recorder armed -- i.e. is the scene between `on_enter` and the sample landing?
	bool recording() const { return recorder_ != nullptr; }
	/// @return Has the recorder captured its first sample? Legacy's `updatedRecordingStatus`
	///         (`:234`), read rather than cached.
	bool capturing() const;
	/// @return Has a stop been asked for -- legacy's `endRecordingSoon` having run (`:159-166`)?
	bool stopping() const { return stopping_; }
	/// @return The armed recorder, or null. A spec drives the state machine through it, exactly as
	///         the audio engine does on the device.
	SampleRecorder* recorder() const { return recorder_; }
	/// @}

private:
	/// @brief `AudioRecorder::setupRecordingToFile` (`:121-141`), minus the `recordingSource`
	///        member -- `recorder_ != nullptr` is the same fact.
	/// @return False when no recorder could be allocated (legacy's `INSUFFICIENT_RAM` arm).
	bool setup_recording();
	/// @brief `AudioRecorder::endRecordingSoon` (`:159-166`).
	void end_recording_soon();
	/// @brief `AudioRecorder::finishRecording` (`:245-253`).
	void finish_recording();
	/// @brief `opened()`'s kit arm (`:74-88`): name the drum being recorded into "REC".
	/// @return False on the error legacy shows and refuses on.
	bool name_drum();
	/// @brief `process()`'s success arm (`:212-218`): hand the finished sample to the source.
	void attach_sample();

	menu::MenuContext ctx_{};

	/// The armed recorder. Legacy's `AudioRecorder::recorder` (`audio_recorder.h:42`), except that
	/// this one is not shared with the output-recording path -- see the class docstring.
	SampleRecorder* recorder_ = nullptr;

	/// Legacy has no member for this: `endRecordingSoon` is idempotent by testing
	/// `recorder->status == CAPTURING_DATA` (`:162`). Kept here only so `stopping()` can be
	/// observed and so the "Working" notice is raised exactly once.
	bool stopping_ = false;
};

/// @brief The application's one audio recorder.
/// @return The singleton.
RecorderScene& recorder_scene();

} // namespace deluge::scenes::recorder
