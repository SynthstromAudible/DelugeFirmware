#pragma once

/// @brief `AudioClipScene` -- the audio clip view: a coloured waveform on the pads, the clip's
///        start/end markers, the play cursor, and the output's name on the OLED.
///
/// The port of `AudioClipView` (`gui/views/audio_clip_view.{h,cpp}`). Task 11 covers `opened`
/// (`:68-76`), `focusRegained` (`:78-89`), `renderOLED` (`:91-93`), `renderMainPads` (`:95-221`),
/// `timerCallback` (`:223-229`), `renderSidebar` (`:231-260`), `graphicsRoutine` (`:262-310`),
/// `needsRenderingDependingOnSubMode` (`:312-320`), `setupScroll` (`:850-855`), `getMaxLength`
/// (`:857-862`) and `getMaxZoom` (`:864-870`); Task 12 adds `buttonAction` (`:324-470`), `padAction`
/// (`:472-589`), `changeUnderlyingSampleLength` (`:592-646`), `changeUnderlyingSampleStart`
/// (`:649-701`), `playbackEnded` (`:703-705`), `clipNeedsReRendering` (`:707-718`),
/// `sampleNeedsReRendering` (`:720-724`), `selectEncoderAction` (`:726-738`),
/// `setClipLengthEqualToSampleLength` (`:740-751`), `adjustLoopLength` (`:753-785`),
/// `horizontalEncoderAction` (`:787-796`), `editClipLengthWithoutTimestretching` (`:798-835`) and
/// `verticalEncoderAction` (`:837-848`).
///
/// @par This IS a root scene -- the first one in the tree -- and that has consequences
///
/// `class AudioClipView final : public ClipView` (`audio_clip_view.h:27`) -> `ClipView : public
/// ClipNavigationTimelineView` (`gui/views/clip_view.h:26`) -> `ClipNavigationTimelineView : public
/// TimelineView` (`gui/views/clip_navigation_timeline_view.h:23`) -> `class TimelineView : public
/// RootUI` (`gui/views/timeline_view.h:27`). `grep "public RootUI"` finds exactly two classes in
/// the tree, `TimelineView` and `KeyboardScreen` (`gui/ui/keyboard/keyboard_screen.h:35`), and this
/// is the first scene ported from either. So **`is_root()` returns true**, and
/// `services::GlobalControls` gates a whole group of legacy's global button arms on exactly that
/// predicate (`shell/scene.h`'s `Scene::is_root`, `services/global_controls.cpp:580`).
///
/// The group that becomes reachable with this scene on top is `View::buttonAction`'s
/// (`gui/views/view.cpp:140-487`), and it is **nine** arms, not the six an earlier cut of this
/// comment listed (fix round 1, Minor 1): `SAVE`, `LOAD`, `SYNC_SCALING`, `TAP_TEMPO`, `BACK`
/// (undo/redo) and shift+`SELECT_ENC`, **plus** `LEARN` and -- while MIDI learn is on -- `PLAY` and
/// `RECORD`. Those last three are `View::buttonAction` arms too (`view.cpp:186-197`, `:200-213`,
/// `:215-228`); `services::GlobalControls` returns them as `ViewArm::LearnOwns`
/// (`services/global_controls.cpp:370-381`) so `services::MidiLearn`, routed one step further on,
/// gets them -- but that hand-off happens **inside the same `is_root()` gate** at
/// `global_controls.cpp:580`, so with a non-root scene on top it does not happen at all, exactly as
/// in legacy. **That reachability is legacy's, checked by reading the chain rather than assumed:**
/// `AudioClipView::buttonAction`'s final `else` (`audio_clip_view.cpp:466-485`) calls `ClipMinder::buttonAction` and
/// then `ClipView::buttonAction` (`clip_view.cpp:47-70`), whose own `else` (`:65-67`) calls
/// `ClipNavigationTimelineView::buttonAction` -- which that class
/// **does not declare** (`grep buttonAction gui/views/clip_navigation_timeline_view.{h,cpp}` finds
/// nothing), so the name resolves to the inherited `TimelineView::buttonAction`, whose own `else`
/// (`timeline_view.cpp:119-121`) is `return view.buttonAction(b, on, inCardRoutine)`. None of the
/// five buttons above is claimed earlier on that path.
///
/// @par One arm of that group collided with a button this scene claims, and the collision is CLOSED
///
/// `AudioClipView` claims `BACK` for "clear clip" while
/// `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` (`audio_clip_view.cpp:435-465`),
/// which in legacy runs *instead of* `View::buttonAction`'s undo/redo arm because it returns before
/// the `else` at `:448`. `GlobalControls` is routed after the top scene (`shell/router.h:55-65`),
/// so the port gets the same precedence only by claiming `Back` in that mode -- which
/// `handle()`'s `Back` arm now does, returning `handled = true` so the event never reaches
/// `GlobalControls::view_button_action`'s undo/redo. Task 11 shipped this scene with `is_root()`
/// true and no `Back` arm, so for one task a `BACK` press with the horizontal encoder held undid
/// where legacy cleared; that is what this arm closes.
///
/// **The mode is a latch, not a question about held buttons**, and that distinction is legacy's:
/// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` is entered by `TimelineView::buttonAction`'s `X_ENC`
/// arm (`gui/views/timeline_view.cpp:73-90`), which `AudioClipView::buttonAction` only reaches
/// when shift was **not** held at the moment of the press (`:383-397`: with shift down and `Y_ENC`
/// up, the `X_ENC` arm does nothing at all and never delegates). So "X_ENC is down" and "the mode is
/// set" are different facts -- pressing X_ENC with shift held enters no mode, and releasing X_ENC
/// while `Y_ENC` is held leaves the mode set -- and only the latch answers `:417`'s condition. That
/// condition is `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, an **exact**
/// comparison, so the song button being held too (`UI_MODE_HOLDING_SONG_BUTTON`, an exclusive mode
/// that ORs into the same word) takes the arm away again.
///
/// @par The UI modes this scene carries, and why it carries any at all
///
/// Legacy's `currentUIMode` is one global word with two kinds of value in it: an exclusive field in
/// the low byte (`EXCLUSIVE_UI_MODES_MASK`, `gui/ui/ui.h:81`) and independent flag bits above it
/// -- `isUIModeActive` (`gui/ui/ui.cpp:400-408`), `exitUIMode` (`:443-452`) and `enterUIMode`
/// (`:454-461`) are the three that tell the two apart.
/// Two of its values are set by this view's own button arms and read by
/// this view's own encoder arms, so the port cannot drop them and stay faithful:
/// `UI_MODE_HOLDING_SONG_BUTTON` (`ui.h:79`) and `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`
/// (`ui.h:89`). They are transcribed as `kUiModeHoldingSongButton` /
/// `kUiModeHoldingHorizontalEncoder` below, with `enter_ui_mode`/`exit_ui_mode`/`ui_mode_active`
/// transcribing those three functions' exclusive-vs-flag arithmetic, because `src/scenes/**` may not
/// include `gui/ui/ui.h` (`scenes-includes-gui`). No other legacy UI mode is reachable from this
/// scene, so `no_ui_mode_active()` is exact for everything this port can do -- and *only* for that:
/// on the device `currentUIMode` is also written by views this scene knows nothing about, and a
/// scene stack has no shared word at all.
///
/// @par The playhead IS ported, and the plan's brief was wrong about why it could not be
///
/// Three tasks in a row (E4 tasks 9 and 10) recorded a playhead they could not measure, and this
/// task's brief carried that forward as *"`AudioClipView::graphicsRoutine` walks voices over a live
/// `AudioEngine` to draw the playhead"*. **It does not.** The voice walk is `Slicer::graphicsRoutine`'s
/// (`gui/ui/slicer.cpp:156-203`, which reads `drum->voices()`); `AudioClipView::graphicsRoutine`
/// (`audio_clip_view.cpp:263-311`) reads `playbackHandler.playbackState`,
/// `currentSong->isClipActive`, `playbackHandler.ticksLeftInCountIn`,
/// `playbackHandler.isEitherClockActive()`, `Clip::getCurrentlyRecordingLinearly()` and
/// `ClipView::getTickSquare()` -- every one of which a spec can drive, and the last of which is
/// already ported and measured as `timeline::TimelineState::tick_square` (E4 task 8). So the
/// playhead ships, painted through `ui::grid::tick_squares` (`ui/grid/columns.h`), and
/// `sim/scenes_spec/audio_clip_scene_spec.cpp` compares it against legacy's own `graphicsRoutine`
/// composed through legacy's own `PadLEDs::prepareColour` (`hid/led/pad_leds.cpp:283-298`), which
/// is the driver-side substitution this widget is the port of.
///
/// @warning **`PadLEDs::flashCursor` has no seam, so this scene carries its own copy.** Legacy
///          gates the whole tick-square update on `flashCursor != FLASH_CURSOR_OFF`
///          (`audio_clip_view.cpp:297`) and `prepareColour` substitutes a pixel only in
///          `FLASH_CURSOR_SLOW` (`pad_leds.cpp:284`). That setting lives on `PadLEDs` (`hid/`,
///          which `src/scenes/**` may never include) and is loaded by
///          `FlashStorage::readSettings` (`storage/flash_storage.cpp:440`) straight into it, so
///          there is nothing in `model/` or `storage/` to read it from. `set_flash_cursor()` below
///          is the port's copy; **nothing sets it at boot**, exactly as
///          `menu::set_shortcuts_version()` and `ShellConfig::screensaver_idle_ms` are unset (see
///          the boot-wiring note in `scenes/registry.h`). Its default is
///          `FlashCursor::Slow`, which is the factory default (`flash_storage.cpp:287`) and the
///          only mode that reaches a pixel at all -- `ui/grid/columns.h`'s own `tick_colour`
///          warning already records why the `FLASH_CURSOR_FAST` branch cannot.
///
/// @par What draws what
///
/// Nothing here calls an `OledFrame` or `GridFrame` primitive (`check_layer_rules.py`,
/// `scenes-no-frame-primitives`), and nothing names `waveformRenderer` or
/// `waveformBasicNavigator` (`scenes-no-legacy-waveform`):
///
///   * `renderMainPads`' `waveformRenderer.renderFullScreen` (`:147-148`) ->
///     `deluge::sample::find_peaks_per_col` + `ui::grid::waveform_full`.
///   * the grey region and the two marker columns (`:157-218`) -> `GridFrame::pad` through the
///     main-column region, with the colours resolved here.
///   * `renderSidebar`'s black fill (`:244-245`) -> `ui::grid::sidebar_columns`.
///   * `graphicsRoutine`'s `PadLEDs::setTickSquares` (`:305`) -> `ui::grid::tick_squares`.
///   * `renderOLED`'s `view.displayOutputName` (`:92`) -> `ui::oled::output_name`, plus the
///     clip's "Section N" line through `ui::oled::text` (see `render_oled`).
///
/// @par The sidebar widget's status-pad rules are NOT this view's, and the brief asked to check
///
/// `ui/grid/sidebar.h` carries `status_pad`/`status_colour` (the port of
/// `View::getClipMuteSquareColour`) and `audition_pad`/`audition_colour` (the port of
/// `InstrumentClipView::drawAuditionSquare`). **`AudioClipView::renderSidebar` uses neither.** Read
/// end to end (`audio_clip_view.cpp:232-261`), its body is: return early if `image` is null or a
/// collapse animation is running; then, per row, `std::fill` both sidebar columns with
/// `colours::black`; then, *only while* `isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)`,
/// `view.renderMacros(kDisplayWidth, y, -1, image, occupancyMask)`; then refresh the occupancy
/// mask; and finally `view.flashPlayEnable()` if any macro reported armed. No clip-status square,
/// no audition square. So the entry point this scene uses is `ui::grid::sidebar_columns`, and the
/// two colour helpers would have been the wrong widget for it -- which is the answer to the
/// question, not an omission.
///
/// @warning **The macro arm is not ported here.** Its three reads are all outside this layer:
///          `view.clipArmFlashOn` (`gui/views/view.h:125` -- ported, but on
///          `services::GlobalControls::clip_arm_flash_on()`, which this scene holds no handle to),
///          `matrixDriver.isPadPressed` (`hid/matrix/`, no seam) and `view.flashPlayEnable()`
///          (`view.cpp:2783-2785`, likewise on `GlobalControls`). Its gate,
///          `UI_MODE_HOLDING_SONG_BUTTON`, is set by `AudioClipView::buttonAction`'s
///          `SESSION_VIEW` arm (`audio_clip_view.cpp:331-358`) -- **Task 12's** citation -- so
///          nothing in Task 11 can even reach it. `View::renderMacros` (`view.cpp:2802-2854`) is
///          shared with `SessionView`, so the widget it wants belongs with E5's `SessionScene`
///          rather than being invented here for one unreachable arm.
///
/// @par The transitions, the UI modes and the two collapse gates
///
/// `renderMainPads` and `renderSidebar` both return early while
/// `UI_MODE_INSTRUMENT_CLIP_COLLAPSING` or `UI_MODE_IMPLODE_ANIMATION` is active (`:101-103`,
/// `:237-239`), and `graphicsRoutine` returns early on `UI_MODE_AUDIO_CLIP_COLLAPSING` (`:263-265`)
/// and reports "no cursor" on `UI_MODE_EXPLODE_ANIMATION`/`UI_MODE_IMPLODE_ANIMATION` (`:270`).
/// **All four are `PadLEDs` transition animations, and the port carries none of them** --
/// `scenes/timeline/timeline.h` already records that split for `initiateXScroll`/`initiateXZoom`,
/// and the shell has no transition surface at all. So those gates are constant-false here, and
/// `needsRenderingDependingOnSubMode` (`:312-320`), whose entire body is "ask for a redraw unless a
/// horizontal scroll or zoom transition is running", reduces to "ask for a redraw" -- which in an
/// immediate-mode shell is what `next_timer()` already grants every tick the playhead is live.
///
/// @par Where `TimelineState::stop_following()` is called, and where it must be called
///
/// `TimelineState::stop_following()` is public because legacy clears
/// `xScrollBeforeFollowingAutoExtendingLinearRecording` at the top of
/// `ClipNavigationTimelineView::horizontalEncoderAction` (`clip_navigation_timeline_view.cpp:32`),
/// **above** `TimelineView::horizontalEncoderAction`'s SD-routine, rendering-lock and UI-mode gates
/// (`timeline_view.cpp:137-151`, `:161`, `:203`), while the port's `zoom()`/`scroll()` clear it
/// from inside. The residual is therefore: *legacy forgets the restore point even on a turn it then
/// refuses; the port does not.*
///
/// This scene owns two of the three places the field is written, and calls both:
///
///   * `focus_regained()` -- `ClipNavigationTimelineView::focusRegained`
///     (`clip_navigation_timeline_view.cpp:26-28`), whose whole body is that same clear. It is
///     reached from `AudioClipView::focusRegained` -> `ClipView::focusRegained`, so `on_enter()`
///     and `on_resume()` both call it.
///   * the gestures' own clear, inside `TimelineState::zoom`/`scroll`.
///
/// The third is the refused-turn case, and Task 12 performs it -- but **one function lower than
/// Task 11's ruling said, and the difference is a behavioural one.** Task 11 (and the Task 12
/// brief after it) ruled the call belongs at "the first statement of this scene's
/// horizontal-encoder arm, above every gate", naming `AudioClipView::horizontalEncoderAction`
/// (`audio_clip_view.cpp:805-814`). Read against the citation the ruling itself gives
/// (`clip_navigation_timeline_view.cpp:32`), that is one level too high: legacy's clear is the
/// first statement of `ClipNavigationTimelineView::horizontalEncoderAction`
/// (`clip_navigation_timeline_view.cpp:30-34`), which is reached only through the **third** arm of
/// `ClipView::horizontalEncoderAction` (`gui/views/clip_view.cpp:253-262`) -- and that arm sits
/// below three earlier decisions that legacy makes *without* clearing:
///
///   1. `AudioClipView::horizontalEncoderAction`'s own shift+`X_ENC` arm (`:789-791`), which goes
///      to `editClipLengthWithoutTimestretching` and never reaches the navigation view;
///   2. `ClipView::horizontalEncoderAction`'s shift length edit (`clip_view.cpp:149-181`);
///   3. its `Y_ENC` horizontal-shift arm (`:184-250`);
///   4. and, inside the third arm itself, the `currentlyScrollableAndZoomable()` guard at
///      `clip_view.cpp:256-258`, which returns before the clear.
///
/// So `stop_following()` is called here as the first statement of `nav_horizontal_encoder_action`,
/// the port of that one function -- above `TimelineView::horizontalEncoderAction`'s SD-routine,
/// rendering-lock and UI-mode gates, which is the whole point, and below the four decisions above,
/// which is what keeps it from clearing on turns legacy leaves alone. Driving the higher placement
/// would have cleared the restore point on a shift length edit, a `Y_ENC` shift and a tempoless
/// clip; that is a behaviour change, not a placement detail.
///
/// The refused-turn case the call exists for is specced: with `UI_MODE_HOLDING_SONG_BUTTON` set,
/// `TimelineView::horizontalEncoderAction` matches neither its zoom gate (`timeline_view.cpp:161`,
/// which needs the horizontal-encoder flag) nor its scroll gate (`:203`, which needs no mode at
/// all), so nothing moves -- and the restore point is cleared anyway, on both sides.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on
///          `Action` in `shell/scene.h`.

#include "scenes/timeline/timeline.h"
#include "shell/scene.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include "definitions_cxx.hpp"

#include <cstdint>
#include <optional>

/// The legacy undo-log entry (`model/action/action.h`), not `deluge::ui::shell::Action` -- the two
/// names collide, so every mention of this one in this file and its `.cpp` is spelled `::Action`.
class Action;
class AudioClip;
class Sample;
class Song;

namespace deluge::scenes::audio_clip {

/// @brief How long one blink phase lasts. Legacy's `kSampleMarkerBlinkTime`
///        (`definitions_cxx.hpp:161`), the interval `AudioClipView::timerCallback` re-arms
///        `TimerName::UI_SPECIFIC` at (`audio_clip_view.cpp:228`).
///
/// Spelled through the constant rather than as a literal, exactly as
/// `marker::kBlinkPhaseMs` is (`scenes/marker/marker_scene.h:103-104`).
inline constexpr uint32_t kMarkerBlinkMs = static_cast<uint32_t>(kSampleMarkerBlinkTime);

/// @name The `currentUIMode` word, transcribed
///
/// `#define`s in `gui/ui/ui.h`, which `src/scenes/**` may not include (`scenes-includes-gui`), so
/// they are transcribed with their citations exactly as `timeline::divide_round_negative`
/// transcribes `util/functions.cpp`'s body. See the class docstring for why this scene carries a
/// mode word at all, and for which two values it can hold.
/// @{
/// @brief `UI_MODE_NONE` (`gui/ui/ui.h:34`) -- `isNoUIModeActive()` is `!currentUIMode`
///        (`gui/ui/ui.cpp:438-440`).
inline constexpr uint32_t kUiModeNone = 0;
/// @brief `EXCLUSIVE_UI_MODES_MASK` (`gui/ui/ui.h:81`): a value at or below it occupies the low
///        byte and displaces any other low-byte value; a value above it is an independent flag.
inline constexpr uint32_t kExclusiveUiModesMask = 255;
/// @brief `UI_MODE_HOLDING_SONG_BUTTON` (`gui/ui/ui.h:79`) -- an exclusive value, set by
///        `AudioClipView::buttonAction`'s `SESSION_VIEW` arm (`audio_clip_view.cpp:331-359`).
inline constexpr uint32_t kUiModeHoldingSongButton = 65;
/// @brief `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` (`gui/ui/ui.h:89`) -- a flag bit, set by
///        `TimelineView::buttonAction`'s `X_ENC` arm (`gui/views/timeline_view.cpp:73-90`).
inline constexpr uint32_t kUiModeHoldingHorizontalEncoder = 1u << 31;
/// @}

/// @brief `PadLEDs::flashCursor`'s three values (`definitions.h`'s `FLASH_CURSOR_*`), as an enum.
///
/// See the `@warning` in the class docstring: this layer cannot read the real setting, so the scene
/// carries a copy and nothing sets it at boot.
enum class FlashCursor : uint8_t {
	/// `FLASH_CURSOR_FAST` (0) -- legacy routes it to `PadLEDs::flashMainPad`, which changes no
	/// pixel in the rendered image (`ui/grid/columns.h`'s `tick_colour` warning). Draws no cursor
	/// here.
	Fast = 0,
	/// `FLASH_CURSOR_OFF` (1) -- `graphicsRoutine`'s own gate refuses the update outright
	/// (`audio_clip_view.cpp:297`).
	Off = 1,
	/// `FLASH_CURSOR_SLOW` (2) -- the factory default (`storage/flash_storage.cpp:287`) and the
	/// only mode `PadLEDs::prepareColour` substitutes a pixel in (`pad_leds.cpp:284`).
	Slow = 2,
};

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 9` (`scenes/registry.h`).
inline constexpr deluge::ui::shell::SceneKind kAudioClipSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 9);

/// @brief The audio clip view. One instance for the whole application (`scenes/registry.h`).
class AudioClipScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at a clip and the song it lives in.
	///
	/// The port of nothing inside `AudioClipView`: legacy reads `getCurrentAudioClip()` and
	/// `currentSong`, two globals this layer may not name (`check_layer_rules.py`'s
	/// `scenes-currentSong` row), so the two pointers arrive here instead. `MarkerScene::open` and
	/// `SlicerScene::open` are the same shape and exist for the same reason.
	///
	/// Also runs `TimelineState::adopt` (`scenes/timeline/timeline.h`), which is the only place
	/// this layer spells `NAVIGATION_CLIP`: legacy's scroll and zoom live on `Song`, and adopting
	/// them here is what makes the port render from the same view legacy does.
	///
	/// @param clip The clip to show; null leaves the scene with nothing to draw.
	/// @param song The song it belongs to; null is treated as "no clip".
	void open(::AudioClip* clip, ::Song* song);

	/// @brief `PadLEDs::flashCursor`, which this layer cannot read. See the class docstring.
	/// @param c The cursor mode.
	void set_flash_cursor(FlashCursor c) { flash_cursor_ = c; }

	/// @name The two marker latches
	/// `AudioClipView::endMarkerVisible` / `startMarkerVisible` (`audio_clip_view.h:71-72`). They
	/// are Task 11's because three of this task's functions branch on them --
	/// `renderMainPads` (`:140-145`, `:164-171`, `:197-206`), `getMaxLength` (`:858-860`) and
	/// `getMaxZoom` (`:866-868`) -- and Task 12's `padAction` is what sets them.
	/// @{
	/// @param on True while the user is adjusting the clip's end.
	void set_end_marker_visible(bool on);
	/// @param on True while the user is adjusting the clip's start.
	void set_start_marker_visible(bool on);
	/// @return Is the end marker being adjusted?
	[[nodiscard]] bool end_marker_visible() const { return end_marker_visible_; }
	/// @return Is the start marker being adjusted?
	[[nodiscard]] bool start_marker_visible() const { return start_marker_visible_; }
	/// @}

	/// @brief `AudioClipView::getMaxLength` (`audio_clip_view.cpp:875-880`).
	/// @return The clip's loop length, plus one while the end marker is being adjusted.
	[[nodiscard]] uint32_t max_length() const;
	/// @brief `AudioClipView::getMaxZoom` (`audio_clip_view.cpp:882-888`).
	/// @return `Clip::getMaxZoom()`, doubled while the end marker is being adjusted.
	[[nodiscard]] uint32_t max_zoom() const;
	/// @brief `AudioClipView::setupScroll`'s guard (`audio_clip_view.cpp:868-873`).
	///
	/// Legacy's body is that guard plus `ClipView::setupScroll`, which is the `PadLEDs` scroll
	/// transition the port does not carry (`scenes/timeline/timeline.h`). What survives is the
	/// question, which the horizontal-encoder arm (Task 12) has to ask before scrolling or zooming
	/// -- it is also `ClipView::horizontalEncoderAction`'s own gate at `clip_view.cpp:256-258`.
	/// @return `AudioClip::currentlyScrollableAndZoomable()`, or false with no clip.
	[[nodiscard]] bool scrollable_and_zoomable() const;

	/// @brief `AudioClipView::setClipLengthEqualToSampleLength` (`audio_clip_view.cpp:758-769`):
	///        drop the time-stretch by making the clip exactly as long as the sample it plays.
	///
	/// **The first of the five parked `MenuAction`s to get a real method behind it.**
	/// `scenes/menu/action.h`'s table names `audioClipView.setClipLengthEqualToSampleLength()` as
	/// what `MenuAction::SetClipLengthToSampleLength` means, and says plainly that nothing consumes
	/// `MenuScene::take_action()` because the consumer is the wiring plan's. This is the callee that
	/// dispatcher will have; **there is deliberately no dispatcher here** -- half of one would be a
	/// second seam nobody owns.
	///
	/// It is public for a second reason too: legacy reaches it from two places, the menu item and
	/// `buttonAction`'s `X_ENC`+`Y_ENC` chord (`audio_clip_view.cpp:384-391`), and this scene's own
	/// `handle()` is the port of the second.
	void set_clip_length_equal_to_sample_length();

	/// @brief `AudioClipView::adjustLoopLength` (`audio_clip_view.cpp:771-803`): set the clip's
	///        length, keeping the sample's own pitch and speed.
	///
	/// Public because `set_clip_length_equal_to_sample_length` is its only caller in legacy and the
	/// spec drives both -- the sample-length arm through the chord, and this one with a length the
	/// example chose, which is what makes the "shorter" direction reachable at all on a fixture whose
	/// sample is longer than its clip.
	/// @param new_length The clip's new loop length, in ticks.
	void adjust_loop_length(int32_t new_length);

	/// @brief The port's `currentUIMode`, restricted to the two values this scene can set.
	///
	/// Exposed because it is the condition three of legacy's arms branch on and the spec has to
	/// assert it directly -- there is no rendered consequence of `UI_MODE_HOLDING_SONG_BUTTON` in
	/// this port at all (the sidebar macro arm it gates is not ported; see the `@warning` above).
	/// @return `kUiModeNone`, or an OR of `kUiModeHoldingSongButton` / `kUiModeHoldingHorizontalEncoder`.
	[[nodiscard]] uint32_t ui_mode() const { return ui_mode_; }

	/// @brief The scroll and zoom this scene renders from, and the gestures that move it.
	/// @return The state, for Task 12's encoder arms and for the specs.
	/// @{
	[[nodiscard]] timeline::TimelineState& timeline() { return timeline_; }
	[[nodiscard]] const timeline::TimelineState& timeline() const { return timeline_; }
	/// @}

	/// @brief `graphicsRoutine`'s column, as this scene last computed it.
	///
	/// `AudioClipView::lastTickSquare` (`audio_clip_view.h:68`), which task 8 explicitly left to
	/// this scene (`scenes/timeline/timeline.h`'s table). 255 is legacy's "no cursor" sentinel
	/// (`audio_clip_view.cpp:273`), which is also `ui::grid::kNoTickSquare`.
	/// @return The column, or 255.
	[[nodiscard]] int32_t last_tick_square() const { return last_tick_square_; }

	/// @brief The blink phase the two marker columns are drawn in.
	///
	/// `AudioClipView::blinkOn` (`audio_clip_view.h:73`), toggled by `timerCallback`
	/// (`audio_clip_view.cpp:225`).
	/// @return True on the bright half of the blink.
	[[nodiscard]] bool blink_on() const { return blink_on_; }

	deluge::ui::shell::SceneKind kind() const override { return kAudioClipSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	/// @brief `AudioClipView::focusRegained` (`audio_clip_view.cpp:79-90`) again, as `on_enter` is:
	///        `closeUI` calls `focusRegained()` on whatever it leaves on top (`gui/ui/ui.cpp:194-199`).
	void on_resume() override;
	/// @brief `AudioClipView::clipNeedsReRendering` (`audio_clip_view.cpp:725-736`),
	///        `sampleNeedsReRendering` (`:720-724`) and `playbackEnded` (`:703-705`).
	///
	/// All three are notifications in legacy too -- the model calls them, they are not asked. Two of
	/// the three have **no body left** once the redraw is the shell's: `Shell::apply_dirty`
	/// (`shell/shell.cpp:502-527`) already arms the grid gate for `Dirty::ClipContent` and, through
	/// `has_untargeted_fact`, for `Dirty::SampleReady` and `Dirty::PlaybackState`, so
	/// `uiNeedsRendering(this, 0xFFFFFFFF, 0)` needs nothing here. What is genuinely the scene's is
	/// `clipNeedsReRendering`'s *other* half, the scroll-back-left at `:713-715` -- and legacy's own
	/// comment there calls it "a bit hacky that I put this in this function".
	///
	/// @param dirty What changed.
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief **True** -- the first scene in this tree for which it is. See the class docstring.
	bool is_root() const override { return true; }
	/// @brief False: this scene IS the grid base. `AudioClipView` is a `RootUI`, the bottom of
	///        legacy's `uiNavigationHierarchy`, and it paints every pad including the sidebar.
	bool is_grid_overlay() const override { return false; }
	/// @brief True: `renderMainPads` and `renderSidebar` both return true unconditionally
	///        (`audio_clip_view.cpp:221`, `:260`), and `doAnyPendingGridRendering`'s top-down walk
	///        (`gui/ui/ui.cpp:321-348`) stops as soon as both instruction words are used up -- so
	///        nothing below this in the hierarchy renders.
	///
	/// @note Its one consumer is `Shell::tick_stack` (`shell/shell.cpp:573-587`), which stops
	///       advancing clocks *beneath* a scene that claims it. A root scene sits at the bottom of
	///       the stack, so today this changes nothing either way; it is declared from legacy's
	///       answer rather than from that observation, because the observation is about the stack
	///       and the predicate is about the scene.
	bool occludes_grid() const override { return true; }
	/// @brief False (the default made explicit): `is_grid_overlay()` is false, so there is nothing
	///        beneath to dim, and legacy has no `getGreyoutColsAndRows` on this class either --
	///        `getUIGreyoutColsAndRows` (`gui/ui/ui.cpp:73-83`) finds none anywhere on the chain.
	bool dims_grid() const override { return false; }
	/// @brief False: `AudioClipView` never sets `oledShowsUIUnderneath` (it is `UI`'s default,
	///        `gui/ui/ui.h`), so `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp:351-370`) starts its
	///        walk at this view and nothing below it draws.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	/// @brief False: a root view takes input. `AudioClipView` overrides `buttonAction`, `padAction`
	///        and all three encoder hooks.
	bool input_transparent() const override { return false; }

	/// @brief When the grid next needs repainting.
	///
	/// Two independent deadlines, and the nearer wins:
	///
	///   * **the playhead.** `AudioClipView::graphicsRoutine` is a per-frame redraw rather than a
	///     deadline (`UI::graphicsRoutine`, `gui/ui/ui.cpp:47-51`). It is **not** "called from the
	///     main loop on every iteration", which this bullet used to say: its one external driver is
	///     `UITimerManager::routine`'s `TimerName::GRAPHICS_ROUTINE` arm, re-armed at 15 ms
	///     (`gui/ui_timer_manager.cpp:213-218`) -- corrected F1 task 7, whose `ShellHostUI` is the
	///     first host that had to decide what a zero costs. Its port is therefore
	///     `0`, "repaint on the next tick" -- the first zero any scene in this tree returns, and
	///     the case `Scene::next_timer`'s own contract now spells out -- for as long as the cursor
	///     is live; the shell's own
	///     router arms repaints with exactly `request_repaint_after(0, now_ms)`
	///     (`shell/shell.cpp:379-393`) and the grid flush diffs against `last_sent`, so a tick on
	///     which the column did not move costs a repaint and no blit.
	///   * **the marker blink.** `timerCallback` re-arms `TimerName::UI_SPECIFIC` at
	///     `kSampleMarkerBlinkTime` (`audio_clip_view.cpp:228`), and only the two marker columns
	///     change with it -- so it is armed only while a marker is visible.
	///
	/// @return Milliseconds from now, or `std::nullopt`.
	std::optional<uint32_t> next_timer() const override;
	/// @brief `{.oled = false, .grid = true}`: neither the playhead nor the marker blink touches
	///        the OLED page, which draws the output's name and nothing time-dependent.
	///
	/// Arming the OLED from this timer would be a permanent unconditional blit at the playhead's
	/// own cadence -- the defect `Scene::timer_repaints` was added for (`shell/scene.h`).
	RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

private:
	/// @brief `AudioClipView::focusRegained`'s portable half (`audio_clip_view.cpp:79-90`).
	///
	/// `indicator_leds::setLedState(IndicatorLED::BACK, false)` (`:82`) is `LedFrame`'s, and this
	/// scene declares no LEDs yet; `view.focusRegained()` (`:83`) and
	/// `view.setActiveModControllableTimelineCounter` (`:84`) are `services::ModKnobs`'s and
	/// `services::MidiLearn`'s, which the scene holds no handle to; `drawCurrentClipPad` (`:87`) is
	/// inside `#ifdef currentClipStatusButtonX`, which is defined nowhere in this repository.
	void focus_regained();

	/// @brief `AudioClipView::graphicsRoutine` (`audio_clip_view.cpp:263-311`) minus its paint.
	///
	/// Called from `render_grid` rather than from `tick`, because its middle arm writes
	/// `renderData.xScroll = -1` (`:281`) to force the waveform cache to be recomputed, and that
	/// write has to land before the scan the same frame reads it.
	/// @return The column for every row, or 255.
	int32_t compute_tick_square();

	/// @return `getSample()` (`audio_clip_view.cpp:61-67`): the recorder's sample while recording
	///         linearly, otherwise the clip's own audio file.
	[[nodiscard]] ::Sample* current_sample() const;

	/// @name The UI-mode word's three operations, transcribed from `gui/ui/ui.cpp`
	/// See the class docstring for what this scene keeps a mode word for.
	/// @{
	/// @brief `enterUIMode` (`gui/ui/ui.cpp:454-461`).
	/// @param mode One of the two `kUiMode*` values.
	void enter_ui_mode(uint32_t mode);
	/// @brief `exitUIMode` (`gui/ui/ui.cpp:443-452`).
	/// @param mode One of the two `kUiMode*` values.
	void exit_ui_mode(uint32_t mode);
	/// @brief `isUIModeActive` (`gui/ui/ui.cpp:400-408`).
	/// @param mode One of the two `kUiMode*` values.
	/// @return Whether it is active.
	[[nodiscard]] bool ui_mode_active(uint32_t mode) const;
	/// @brief `isNoUIModeActive` (`gui/ui/ui.cpp:438-440`).
	/// @return Whether the word is clear.
	[[nodiscard]] bool no_ui_mode_active() const { return ui_mode_ == kUiModeNone; }
	/// @}

	/// @name The input arms
	/// One private function per legacy entry point, so each one's citation sits on the body that
	/// ports it rather than on a `case` label.
	/// @{
	/// @brief `AudioClipView::buttonAction` (`audio_clip_view.cpp:325-488`).
	/// @param event The button event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result button_action(const deluge::ui::shell::UiEvent& event,
	                                        const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::padAction` (`audio_clip_view.cpp:490-607`).
	/// @param event The pad event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result pad_action(const deluge::ui::shell::UiEvent& event,
	                                     const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::horizontalEncoderAction` (`audio_clip_view.cpp:805-814`).
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `ClipView::horizontalEncoderAction` (`gui/views/clip_view.cpp:146-263`), the three arms
	///        `AudioClipView`'s `else` delegates to.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result clip_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `ClipNavigationTimelineView::horizontalEncoderAction`
	///        (`gui/views/clip_navigation_timeline_view.cpp:30-34`) plus the zoom/scroll gates of
	///        `TimelineView::horizontalEncoderAction` (`gui/views/timeline_view.cpp:135-228`).
	///
	/// **This is where `TimelineState::stop_following()` is called, as the first statement**, which
	/// is exactly where legacy's `xScrollBeforeFollowingAutoExtendingLinearRecording = -1` sits
	/// (`clip_navigation_timeline_view.cpp:32`) -- above every gate below it, and below every
	/// decision above it. See the class docstring's `@par` for why the brief's "first statement of
	/// the horizontal-encoder arm" was one function too high.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn -- `now_ms` for the zoom delay, `shift` for the
	///               scroll arm's own gate (`timeline_view.cpp:206`).
	/// @return What the shell should do.
	deluge::ui::shell::Result nav_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::verticalEncoderAction` (`audio_clip_view.cpp:855-866`).
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result vertical_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::selectEncoderAction` (`audio_clip_view.cpp:744-756`).
	/// @param offset Signed detents.
	/// @param shift  Is shift held?
	/// @return True if anything changed, so the caller can ask for a repaint.
	bool select_encoder_action(int32_t offset, bool shift);
	/// @brief `buttonAction`'s `deactivateMarkerIfNecessary` tail (`audio_clip_view.cpp:473-482`).
	/// @return True if the end marker was up and has been put down.
	bool deactivate_marker_if_necessary();
	/// @brief `buttonAction`'s `BACK` arm's body (`audio_clip_view.cpp:437-464`).
	/// @return The l10n string id of the popup legacy shows, or 0 with no clip to clear.
	uint16_t clear_clip();
	/// @}

	/// @name The two edits, and the length machinery they share
	/// @{
	/// @brief `AudioClipView::changeUnderlyingSampleLength` (`audio_clip_view.cpp:610-664`).
	/// @param sample            The clip's sample -- `getSample()`, never null at the call sites.
	/// @param new_length        The clip's new loop length, in ticks.
	/// @param old_length        Its length before the edit.
	/// @param old_length_samples `sampleHolder.getDurationInSamples(true)` before the edit.
	void change_underlying_sample_length(const ::Sample& sample, int32_t new_length, int32_t old_length,
	                                     uint64_t old_length_samples);
	/// @brief `AudioClipView::changeUnderlyingSampleStart` (`audio_clip_view.cpp:667-719`).
	/// @param sample             The clip's sample.
	/// @param new_start_ticks    Where the clip should now start, in ticks.
	/// @param old_length         Its length before the edit.
	/// @param old_length_samples `sampleHolder.getDurationInSamples(true)` before the edit.
	void change_underlying_sample_start(const ::Sample& sample, int32_t new_start_ticks, int32_t old_length,
	                                    uint64_t old_length_samples);
	/// @brief `AudioClipView::editClipLengthWithoutTimestretching` (`audio_clip_view.cpp:816-853`).
	/// @param offset Signed detents.
	/// @return What the shell should do.
	deluge::ui::shell::Result edit_clip_length_without_timestretching(int32_t offset);
	/// @brief `ClipView::changeClipLength` (`gui/views/clip_view.cpp:264-313`).
	/// @param offset      Signed detents.
	/// @param old_length  The clip's length before the edit.
	/// @param[out] action The action the edit was logged under, or null.
	/// @return The new length.
	uint32_t change_clip_length(int32_t offset, uint32_t old_length, ::Action*& action);
	/// @brief `ClipView::lengthenClip` (`gui/views/clip_view.cpp:75-121`).
	/// @param new_length The new length; the caller has already checked it against `kMaxSequenceLength`.
	/// @return The action it was logged under, or null.
	::Action* lengthen_clip(int32_t new_length);
	/// @brief `ClipView::shortenClip` (`gui/views/clip_view.cpp:124-144`).
	/// @param new_length The new length; the caller has already checked it is positive.
	/// @return The action it was logged under, or null.
	::Action* shorten_clip(int32_t new_length);
	/// @brief `TimelineView::displayNumberOfBarsAndBeats`' popup half (`timeline_view.cpp:241-265`).
	///
	/// The arithmetic is `TimelineState::bars_and_beats`; this is the `sprintf` and the popup.
	/// @param number         The tick position or length to describe.
	/// @param count_from_one True to number bars/beats from one.
	void show_bars_and_beats(uint32_t number, bool count_from_one);
	/// @}

	/// The clip this scene shows, and the song it lives in. See `open`.
	/// @{
	::AudioClip* clip_ = nullptr;
	::Song* song_ = nullptr;
	/// @}

	/// The scroll and zoom, with `AudioClipView::supportsTriplets() == false`
	/// (`audio_clip_view.h:38`) baked into the state at construction.
	timeline::TimelineState timeline_{.supports_triplets = false};

	/// `AudioClipView::endMarkerVisible` / `startMarkerVisible` (`audio_clip_view.h:71-72`).
	/// @{
	bool end_marker_visible_ = false;
	bool start_marker_visible_ = false;
	/// @}
	/// `AudioClipView::blinkOn` (`audio_clip_view.h:73`).
	bool blink_on_ = false;
	/// Milliseconds accumulated towards the next `blink_on_` toggle -- the port of
	/// `TimerName::UI_SPECIFIC`'s remaining time, which this layer has no clock to read.
	uint32_t blink_accum_ms_ = 0;

	/// `AudioClipView::lastTickSquare` / `mustRedrawTickSquares` (`audio_clip_view.h:68-69`), which
	/// `scenes/timeline/timeline.h`'s table assigns to this scene by name.
	/// @{
	int32_t last_tick_square_ = 0;
	bool must_redraw_tick_squares_ = false;
	/// @}

	/// `PadLEDs::flashCursor`. See the class docstring's warning.
	FlashCursor flash_cursor_ = FlashCursor::Slow;

	/// Legacy's `currentUIMode` (`gui/ui/ui.cpp:398`), restricted to the two values this scene sets.
	/// See the class docstring's `@par` on the UI modes.
	uint32_t ui_mode_ = kUiModeNone;

	/// `AudioClipView::timeSongButtonPressed` (`audio_clip_view.h:66`), in the shell's `now_ms`
	/// rather than in `AudioEngine::audioSampleTimer` -- this layer has no clock of its own
	/// (`shell-no-clock`), and `UiContext::now_ms` is the one the shell hands in.
	uint32_t song_button_pressed_ms_ = 0;
};

/// @brief The application's one audio clip view.
///
/// A function-local static, for the reason every other scene singleton in this tree is one.
/// @return The singleton.
AudioClipScene& audio_clip_scene();

} // namespace deluge::scenes::audio_clip
