#pragma once

/// @brief `AudioClipScene` -- the audio clip view: a coloured waveform on the pads, the clip's
///        start/end markers, the play cursor, and the output's name on the OLED.
///
/// The port of `AudioClipView` (`views/audio_clip_view.cpp`): `opened`, `focusRegained`, `renderOLED`,
/// `renderMainPads`, `timerCallback`, `renderSidebar`, `graphicsRoutine`,
/// `needsRenderingDependingOnSubMode`, `setupScroll`, `getMaxLength`, `getMaxZoom`, `buttonAction`,
/// `padAction`, `changeUnderlyingSampleLength`, `changeUnderlyingSampleStart`, `playbackEnded`,
/// `clipNeedsReRendering`, `sampleNeedsReRendering`, `selectEncoderAction`,
/// `setClipLengthEqualToSampleLength`, `adjustLoopLength`, `horizontalEncoderAction`,
/// `editClipLengthWithoutTimestretching` and `verticalEncoderAction`.
///
/// @par This IS a root scene -- the first one in the tree -- and that has consequences
///
/// `class AudioClipView final : public ClipView, public ClipMinder` (`audio_clip_view.h`) -> `ClipView : public
/// ClipNavigationTimelineView` (`gui/views/clip_view.h`) -> `ClipNavigationTimelineView : public
/// TimelineView` (`gui/views/clip_navigation_timeline_view.h`) -> `class TimelineView : public
/// RootUI` (`gui/views/timeline_view.h`). `grep "public RootUI"` finds exactly two classes in
/// the tree, `TimelineView` and `KeyboardScreen` (`gui/ui/keyboard/keyboard_screen.h`), and this
/// is the first scene ported from either. So **`is_root()` returns true**, and
/// `services::GlobalControls` gates a whole group of legacy's global button arms on exactly that
/// predicate (`ui/shell/scene.h`'s `Scene::is_root`, tested in `GlobalControls::handle`).
///
/// The group that becomes reachable with this scene on top is `View::buttonAction`'s
/// (`gui/views/view.cpp`), and it is **nine** arms: `SAVE`, `LOAD`, `SYNC_SCALING`, `TAP_TEMPO`, `BACK`
/// (undo/redo) and shift+`SELECT_ENC`, **plus** `LEARN` and -- while MIDI learn is on -- `PLAY` and
/// `RECORD`. Those last three are `View::buttonAction` arms too (`views/view.cpp`);
/// `services::GlobalControls` returns them as `ViewArm::LearnOwns`
/// (`GlobalControls::view_button_action`) so `services::MidiLearn`, routed one step further on,
/// gets them -- but that hand-off happens **inside the same `is_root()` gate** in
/// `GlobalControls::handle`, so with a non-root scene on top it does not happen at all, exactly as
/// in legacy. **That reachability is legacy's, checked by reading the chain rather than assumed:**
/// `AudioClipView::buttonAction`'s final `else` (`audio_clip_view.cpp`) calls `ClipMinder::buttonAction` and
/// then `ClipView::buttonAction` (`clip_view.cpp`), whose own `else` calls
/// `ClipNavigationTimelineView::buttonAction` -- which that class
/// **does not declare** (`grep buttonAction gui/views/clip_navigation_timeline_view.{h,cpp}` finds
/// nothing), so the name resolves to the inherited `TimelineView::buttonAction`, whose own `else`
/// (`timeline_view.cpp`) is `return view.buttonAction(b, on)`. None of the
/// five buttons above is claimed earlier on that path.
///
/// @par One arm of that group collided with a button this scene claims, and the collision is CLOSED
///
/// `AudioClipView` claims `BACK` for "clear clip" while `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`
/// (`audio_clip_view.cpp`), which in legacy runs *instead of* `View::buttonAction`'s undo/redo arm because it returns
/// before `View::buttonAction`'s `BACK` arm is reached. `GlobalControls` is routed after the top scene
/// (`ui/shell/router.h`'s `Router`), so the port gets the same precedence only by claiming `Back` in that mode -- which
/// `handle()`'s `Back` arm now does, returning `handled = true` so the event never reaches
/// `GlobalControls::view_button_action`'s undo/redo. Without that arm a `BACK` press with the horizontal encoder held
/// would undo where legacy clears.
///
/// **The mode is a latch, not a question about held buttons**, and that distinction is legacy's:
/// `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` is entered by `TimelineView::buttonAction`'s `X_ENC`
/// arm (`gui/views/timeline_view.cpp`), which `AudioClipView::buttonAction` only reaches
/// when shift was **not** held at the moment of the press (with shift down and `Y_ENC`
/// up, its `X_ENC` arm does nothing at all and never delegates). So "X_ENC is down" and "the mode is
/// set" are different facts -- pressing X_ENC with shift held enters no mode, and releasing X_ENC
/// while `Y_ENC` is held leaves the mode set -- and only the latch answers the `BACK` arm's condition. That
/// condition is `currentUIMode == UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`, an **exact**
/// comparison, so the song button being held too (`UI_MODE_HOLDING_SONG_BUTTON`, an exclusive mode
/// that ORs into the same word) takes the arm away again.
///
/// @par The UI modes this scene carries, and why it carries any at all
///
/// Legacy's `currentUIMode` is one global word with two kinds of value in it: an exclusive field in
/// the low byte (`EXCLUSIVE_UI_MODES_MASK`, `gui/ui/ui.h`) and independent flag bits above it
/// -- `isUIModeActive`, `exitUIMode` and `enterUIMode` (all in `gui/ui/ui.cpp`) are the three that tell the two apart.
/// Two of its values are set by this view's own button arms and read by
/// this view's own encoder arms, so the port cannot drop them and stay faithful:
/// `UI_MODE_HOLDING_SONG_BUTTON` (`ui.h`) and `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`
/// (`ui.h`). They are transcribed as `kUiModeHoldingSongButton` /
/// `kUiModeHoldingHorizontalEncoder` below, with `enter_ui_mode`/`exit_ui_mode`/`ui_mode_active`
/// transcribing those three functions' exclusive-vs-flag arithmetic, because `src/ui/scenes/**` may not
/// include `gui/ui/ui.h` (`scenes-includes-gui`). No other legacy UI mode is reachable from this
/// scene, so `no_ui_mode_active()` is exact for everything this port can do -- and *only* for that:
/// on the device `currentUIMode` is also written by views this scene knows nothing about, and a
/// scene stack has no shared word at all.
///
/// @par The playhead IS ported
///
/// `AudioClipView::graphicsRoutine` does not walk voices over a live `AudioEngine` to draw the
/// playhead. The voice walk is the legacy `Slicer::graphicsRoutine`'s, which reads the drum's voices;
/// `AudioClipView::graphicsRoutine`
/// (`audio_clip_view.cpp`) reads whether the clip plays (playback on, the clip active, no count-in), whether a clock
/// runs, whether the clip records linearly, and `ClipView::getTickSquare()`. The scene reads the first and the third
/// through its clip-facts port (`clip_playing`, `clip_recording_linearly`), the clocks and the arrangement's mode
/// through its transport port (`clocks_active`, `arrangement_mode`), and the last is
/// already ported and measured as `timeline::TimelineState::tick_square`. Every one a spec can drive, so the
/// playhead ships, painted through `ui::widgets::grid::tick_squares` (`ui/widgets/grid/columns.h`), and
/// `tests/integration/ui/scenes/audio_clip_scene_spec.cpp` compares it against legacy's own `graphicsRoutine`
/// composed through legacy's own `PadLEDs::prepareColour` (`hid/led/pad_leds.cpp`), which
/// is the driver-side substitution this widget is the port of.
///
/// @note **The flash-cursor setting** (`PadLEDs::flashCursor`) comes from the device port's
///       `pads/playCursor` row: Off refuses the tick-square update outright (`AudioClipView::graphicsRoutine`),
///       only Slow reaches a pixel (`PadLEDs::prepareColour`), and Fast has the PIC flash each new square
///       (`ui::widgets::grid::flash_tick_squares`); `ui/widgets/grid/columns.h`'s `tick_colour` warning records why
///       that flash is colourless for a muted row.
///
/// @par What draws what
///
/// Nothing here calls an `OledFrame` or `GridFrame` primitive (`check_layer_rules.py`,
/// `scenes-no-frame-primitives`), and nothing names `waveformRenderer` or
/// `waveformBasicNavigator` (`scenes-no-legacy-waveform`):
///
///   * `renderMainPads`' `waveformRenderer.renderFullScreen` -> the waveforms port's read at the clip's AUDIO_CLIP
///     address (`services::read_waveform`) + `ui::widgets::grid::waveform_full` over its PEAK columns. A frame whose
///     read left a column pending draws no waveform and asks again, for as long as one is pending
///     (`services::PeakRetry`).
///   * the grey region and the two marker columns -> `GridFrame::pad` through the
///     main-column region, with the colours resolved here.
///   * `renderSidebar`'s black fill -> `ui::widgets::grid::sidebar_columns`.
///   * `graphicsRoutine`'s `PadLEDs::setTickSquares` -> `ui::widgets::grid::tick_squares`.
///   * `renderOLED`'s `view.displayOutputName` -> `ui::widgets::oled::output_name`, plus the
///     clip's "Section N" line through `ui::widgets::oled::text` (see `render_oled`).
///
/// @par The sidebar widget's status-pad rules are NOT this view's
///
/// `ui/widgets/grid/sidebar.h` carries `status_pad`/`status_colour` (the port of
/// `View::getClipMuteSquareColour`) and `audition_pad`/`audition_colour` (the port of
/// `InstrumentClipView::drawAuditionSquare`). **`AudioClipView::renderSidebar` uses neither.** Read
/// end to end (`audio_clip_view.cpp`), its body is: return early if `image` is null or a
/// collapse animation is running; then, per row, `std::fill` both sidebar columns with
/// `colours::black`; then, *only while* `isUIModeActive(UI_MODE_HOLDING_SONG_BUTTON)`,
/// `view.renderMacros(kDisplayWidth, y, -1, image, occupancyMask)`; then refresh the occupancy
/// mask; and finally `view.flashPlayEnable()` if any macro reported armed. No clip-status square,
/// no audition square. So the entry point this scene uses is `ui::widgets::grid::sidebar_columns`, and the
/// two colour helpers would have been the wrong widget for it -- which is the answer to the
/// question, not an omission.
///
/// @warning **The macro arm is not ported here.** Its three reads are all outside this layer:
///          `view.clipArmFlashOn` (`View::clipArmFlashOn` -- ported, but on
///          `services::GlobalControls::clip_arm_flash_on()`, which this scene holds no handle to),
///          `matrixDriver.isPadPressed` (`hid/matrix/`, no seam) and `view.flashPlayEnable()`
///          (`views/view.cpp`, likewise on `GlobalControls`). Its gate,
///          `UI_MODE_HOLDING_SONG_BUTTON`, is set by `AudioClipView::buttonAction`'s
///          `SESSION_VIEW` arm (`audio_clip_view.cpp`), so
///          nothing in this scene reaches it. `View::renderMacros` is shared with `SessionView`,
///          so the widget it wants belongs with `SessionScene` rather than being invented here for
///          one unreachable arm.
///
/// @par The transitions, the UI modes and the two collapse gates
///
/// `renderMainPads` and `renderSidebar` both return early while `UI_MODE_INSTRUMENT_CLIP_COLLAPSING` or
/// `UI_MODE_IMPLODE_ANIMATION` is active, and `graphicsRoutine` returns early on `UI_MODE_AUDIO_CLIP_COLLAPSING` and
/// reports "no cursor" on `UI_MODE_EXPLODE_ANIMATION`/`UI_MODE_IMPLODE_ANIMATION`. **All four are `PadLEDs` transition
/// animations, and the port carries none of them** -- `ui/scenes/timeline/timeline.h` already records that split for
/// `initiateXScroll`/`initiateXZoom`, and the shell has no transition surface at all. So those gates are constant-false
/// here, and `needsRenderingDependingOnSubMode`, whose entire body is "ask for a redraw unless a horizontal scroll or
/// zoom transition is running", reduces to "ask for a redraw" -- which in an immediate-mode shell is what
/// `next_timer()` already grants every tick the playhead is live.
///
/// @par Where `TimelineState::stop_following()` is called, and where it must be called
///
/// `TimelineState::stop_following()` is public because legacy clears
/// `xScrollBeforeFollowingAutoExtendingLinearRecording` at the top of
/// `ClipNavigationTimelineView::horizontalEncoderAction` (`clip_navigation_timeline_view.cpp`),
/// **above** `TimelineView::horizontalEncoderAction`'s re-entry guards and UI-mode gates
/// (`timeline_view.cpp`, its zoom gate, its scroll gate), while the port's `zoom()`/`scroll()` clear it
/// from inside. The residual is therefore: *legacy forgets the restore point even on a turn it then
/// refuses; the port does not.*
///
/// This scene owns two of the three places the field is written, and calls both:
///
///   * `focus_regained()` -- `ClipNavigationTimelineView::focusRegained`
///     (`clip_navigation_timeline_view.cpp`), whose whole body is that same clear. It is
///     reached from `AudioClipView::focusRegained` -> `ClipView::focusRegained`, so `on_enter()`
///     and `on_resume()` both call it.
///   * the gestures' own clear, inside `TimelineState::zoom`/`scroll`.
///
/// The third is the refused-turn case, and it is performed **one function below
/// `AudioClipView::horizontalEncoderAction`** (`audio_clip_view.cpp`), which is a
/// behavioural difference, not a placement detail: legacy's clear is the
/// first statement of `ClipNavigationTimelineView::horizontalEncoderAction`
/// (`clip_navigation_timeline_view.cpp`), which is reached only through the **third** arm of
/// `ClipView::horizontalEncoderAction` (its `currentlyScrollableAndZoomable` arm) --
/// and that arm sits below three earlier decisions that legacy makes *without* clearing:
///
///   1. `AudioClipView::horizontalEncoderAction`'s own shift+`X_ENC` arm, which goes
///      to `editClipLengthWithoutTimestretching` and never reaches the navigation view;
///   2. `ClipView::horizontalEncoderAction`'s shift length edit (`clip_view.cpp`);
///   3. its `Y_ENC` horizontal-shift arm;
///   4. and, inside the third arm itself, the `currentlyScrollableAndZoomable()` guard in
///      `ClipView::horizontalEncoderAction`, which returns before the clear.
///
/// So `stop_following()` is called here as the first statement of `nav_horizontal_encoder_action`,
/// the port of that one function -- above `TimelineView::horizontalEncoderAction`'s re-entry guards
/// and UI-mode gates, which is the whole point, and below the four decisions above,
/// which is what keeps it from clearing on turns legacy leaves alone. Driving the higher placement
/// would have cleared the restore point on a shift length edit, a `Y_ENC` shift and a tempoless
/// clip; that is a behaviour change, not a placement detail.
///
/// The refused-turn case the call exists for is specced: with `UI_MODE_HOLDING_SONG_BUTTON` set,
/// `TimelineView::horizontalEncoderAction` matches neither its zoom gate (which needs the
/// horizontal-encoder flag) nor its scroll gate (which needs no mode at all), so nothing moves -- and the restore point
/// is cleared anyway, on both sides.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on
///          `Action` in `ui/shell/scene.h`.

#include "project/clip_address.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/timeline/timeline.h"
#include "ui/shell/scene.h"
#include "ui/widgets/core/metrics.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/oled/frame.h"

#include "project/device_settings.h"
#include "project/setting_group.h"
#include "ui/gfx/grid/pad_grid.h"
#include "ui/scenes/bind/device_reader.h"
#include "ui/scenes/bind/ports/ports.h"
#include "ui/scenes/bind/ports/waveforms.h"
#include "ui/scenes/services/peak_retry.h"
#include "ui/widgets/grid/columns.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::project {
struct DocClip;
class Song;
} // namespace deluge::project

namespace deluge::ui::scenes::audio_clip {

/// @brief How long one blink phase lasts: `widgets::core::kSampleMarkerBlinkTime` (`ui/widgets/core/metrics.h`), the
///        interval at which `AudioClipView::timerCallback` re-arms `TimerName::UI_SPECIFIC`.
///
/// Spelled through the constant rather than as a literal, exactly as
/// `marker::kBlinkPhaseMs` is.
inline constexpr uint32_t kMarkerBlinkMs = static_cast<uint32_t>(widgets::core::kSampleMarkerBlinkTime);

/// @name The `currentUIMode` word, transcribed
///
/// `#define`s in `gui/ui/ui.h`, which `src/ui/scenes/**` may not include (`scenes-includes-gui`), so
/// they are transcribed with their citations exactly as `timeline::divide_round_negative`
/// transcribes `util/functions.cpp`'s body. See the class docstring for why this scene carries a
/// mode word at all, and for which two values it can hold.
/// @{
/// @brief `UI_MODE_NONE` (`gui/ui/ui.h`) -- `isNoUIModeActive()` is `!currentUIMode`
///        (`gui/ui/ui.cpp`).
inline constexpr uint32_t kUiModeNone = 0;
/// @brief `EXCLUSIVE_UI_MODES_MASK` (`gui/ui/ui.h`): a value at or below it occupies the low
///        byte and displaces any other low-byte value; a value above it is an independent flag.
inline constexpr uint32_t kExclusiveUiModesMask = 255;
/// @brief `UI_MODE_HOLDING_SONG_BUTTON` (`gui/ui/ui.h`) -- an exclusive value, set by
///        `AudioClipView::buttonAction`'s `SESSION_VIEW` arm (`audio_clip_view.cpp`).
inline constexpr uint32_t kUiModeHoldingSongButton = 65;
/// @brief `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON` (`gui/ui/ui.h`) -- a flag bit, set by
///        `TimelineView::buttonAction`'s `X_ENC` arm (`gui/views/timeline_view.cpp`).
inline constexpr uint32_t kUiModeHoldingHorizontalEncoder = 1u << 31;
/// @}

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 9` (`ui/scenes/registry.h`).
inline constexpr deluge::ui::shell::SceneKind kAudioClipSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 9);

/// @brief The audio clip view. One instance for the whole application (`ui/scenes/registry.h`).
class AudioClipScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at a clip and the song's record it lives in.
	///
	/// The port of nothing inside `AudioClipView`: legacy reads `getCurrentAudioClip()` and
	/// `currentSong`, two globals this layer may not name (`check_layer_rules.py`'s
	/// `scenes-currentSong` row), so the clip's address and the song's record arrive here instead.
	/// `MarkerScene::open` and `SlicerScene::open` are the same shape and exist for the same reason.
	///
	/// Also runs `TimelineState::adopt` (`ui/scenes/timeline/timeline.h`) over the clip-facts port's
	/// `clip_navigation()`: legacy's scroll and zoom live on `Song`, and adopting them here is what
	/// makes the port render from the same view legacy does. With no clip-facts port, or no song
	/// behind it, the view stays as it was.
	///
	/// @param clip  The clip to show; nullopt leaves the scene with nothing to draw.
	/// @param model The song's record, which holds the clip's record and its track's rows; null is treated as
	///              "no clip". Borrowed, not owned; it must outlive the scene's use of it.
	void open(std::optional<deluge::project::ClipAddress> clip, const deluge::project::Song* model);

	/// @brief Install the ports the scene reads and writes through: the app installs its own at boot
	///        (`gui::shell_host::install_scene_ports`), a spec the app's or a copy with fakes in it.
	///
	/// With none, or a null port, the scene reads no clip fact, no waveform, the SLOW play cursor, not playing and
	/// no clocks, writes nothing, and `next_timer` reads "not live".
	/// @param ports The set, or null. Borrowed, not owned; it must outlive the scene's use of it.
	void set_ports(const deluge::ui::scenes::bind::Ports* ports) { ports_ = ports; }

	/// @name The two marker latches
	/// `AudioClipView::endMarkerVisible` / `startMarkerVisible` (`audio_clip_view.h`). They
	/// are read by three functions --
	/// `renderMainPads`, `getMaxLength` and
	/// `getMaxZoom` -- and `padAction` is what sets them.
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

	/// @brief `AudioClipView::getMaxLength`.
	/// @return The clip's loop length, plus one while the end marker is being adjusted.
	[[nodiscard]] uint32_t max_length() const;
	/// @brief `AudioClipView::getMaxZoom`.
	/// @return `Clip::getMaxZoom()`, doubled while the end marker is being adjusted.
	[[nodiscard]] uint32_t max_zoom() const;
	/// @brief `AudioClipView::setupScroll`'s guard.
	///
	/// Legacy's body is that guard plus `ClipView::setupScroll`, which is the `PadLEDs` scroll
	/// transition the port does not carry (`ui/scenes/timeline/timeline.h`). What survives is the
	/// question, which the horizontal-encoder arm has to ask before scrolling or zooming
	/// -- it is also `ClipView::horizontalEncoderAction`'s own `currentlyScrollableAndZoomable` gate.
	/// @return `AudioClip::currentlyScrollableAndZoomable()`, or false with no clip.
	[[nodiscard]] bool scrollable_and_zoomable() const;

	/// @brief `AudioClipView::setClipLengthEqualToSampleLength`:
	///        drop the time-stretch by making the clip exactly as long as the sample it plays.
	///
	/// `ui/scenes/menu/action.h`'s table names `audioClipView.setClipLengthEqualToSampleLength()` as
	/// what `MenuAction::SetClipLengthToSampleLength` means. The consumer of
	/// `MenuScene::take_action()` calls the **legacy** method on `audioClipView`
	/// (`gui/ui/shell_action_dispatch.cpp`). This is the port of
	/// that same method; **there is deliberately no dispatcher here** -- that would be a second seam.
	///
	/// It is public for a second reason too: legacy reaches it from two places, the menu item and
	/// `buttonAction`'s `X_ENC`+`Y_ENC` chord (`audio_clip_view.cpp`), and this scene's own
	/// `handle()` is the port of the second.
	void set_clip_length_equal_to_sample_length();

	/// @brief The port's `currentUIMode`, restricted to the two values this scene can set.
	///
	/// Exposed because it is the condition three of legacy's arms branch on and the spec has to
	/// assert it directly -- there is no rendered consequence of `UI_MODE_HOLDING_SONG_BUTTON` in
	/// this port at all (the sidebar macro arm it gates is not ported; see the `@warning` above).
	/// @return `kUiModeNone`, or an OR of `kUiModeHoldingSongButton` / `kUiModeHoldingHorizontalEncoder`.
	[[nodiscard]] uint32_t ui_mode() const { return ui_mode_; }

	/// @brief The scroll and zoom this scene renders from, and the gestures that move it.
	/// @return The state, for the encoder arms and for the specs.
	/// @{
	[[nodiscard]] timeline::TimelineState& timeline() { return timeline_; }
	[[nodiscard]] const timeline::TimelineState& timeline() const { return timeline_; }
	/// @}

	/// @brief `graphicsRoutine`'s column, as this scene last computed it.
	///
	/// `AudioClipView::lastTickSquare` (`audio_clip_view.h`), which `ui/scenes/timeline/timeline.h`'s
	/// table leaves to this scene. 255 is legacy's "no cursor" sentinel
	/// (`AudioClipView::graphicsRoutine` in `audio_clip_view.cpp`), which is also `ui::widgets::grid::kNoTickSquare`.
	/// @return The column, or 255.
	[[nodiscard]] int32_t last_tick_square() const { return last_tick_square_; }

	/// @brief The blink phase the two marker columns are drawn in.
	///
	/// `AudioClipView::blinkOn` (`audio_clip_view.h`), toggled by `timerCallback`
	/// (`AudioClipView::timerCallback`).
	/// @return True on the bright half of the blink.
	[[nodiscard]] bool blink_on() const { return blink_on_; }

	deluge::ui::shell::SceneKind kind() const override { return kAudioClipSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	/// @brief `AudioClipView::focusRegained` (`audio_clip_view.cpp`) again, as `on_enter` is:
	///        `closeUI` calls `focusRegained()` on whatever it leaves on top (`gui/ui/ui.cpp`).
	void on_resume() override;
	/// @brief `AudioClipView::clipNeedsReRendering`,
	///        `sampleNeedsReRendering` and `playbackEnded`.
	///
	/// All three are notifications in legacy too -- the model calls them, they are not asked. Two of
	/// the three have **no body left** once the redraw is the shell's: `Shell::apply_dirty`
	/// already arms the grid gate for `Dirty::ClipContent` and, through
	/// `has_untargeted_fact`, for `Dirty::SampleReady` and `Dirty::PlaybackState`, so
	/// `uiNeedsRendering(this, 0xFFFFFFFF, 0)` needs nothing here. What is genuinely the scene's is
	/// `clipNeedsReRendering`'s *other* half, the scroll-back-left -- and legacy's own
	/// comment there calls it "a bit hacky that I put this in this function".
	///
	/// @param dirty What changed.
	void on_model_changed(const deluge::base::notify::DirtySet& dirty) override;

	/// @brief **True**: this scene is a root. See the class docstring.
	bool is_root() const override { return true; }
	/// @brief False: this scene IS the grid base. `AudioClipView` is a `RootUI`, the bottom of
	///        legacy's `uiNavigationHierarchy`, and it paints every pad including the sidebar.
	bool is_grid_overlay() const override { return false; }
	/// @brief True: `renderMainPads` and `renderSidebar` both return true unconditionally
	///        (`AudioClipView::renderMainPads`, `AudioClipView::renderSidebar`), and `doAnyPendingGridRendering`'s
	///        top-down walk
	///        (`gui/ui/ui.cpp`) stops as soon as both instruction words are used up -- so
	///        nothing below this in the hierarchy renders.
	///
	/// @note Its one consumer is `Shell::tick_stack`, which stops
	///       advancing clocks *beneath* a scene that claims it. A root scene sits at the bottom of
	///       the stack, so today this changes nothing either way; it is declared from legacy's
	///       answer rather than from that observation, because the observation is about the stack
	///       and the predicate is about the scene.
	bool occludes_grid() const override { return true; }
	/// @brief False: `AudioClipView` never sets `oledShowsUIUnderneath` (it is `UI`'s default,
	///        `gui/ui/ui.h`), so `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp`) starts its
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
	///     deadline (`UI::graphicsRoutine`, `gui/ui/ui.cpp`). It is **not** called from the
	///     main loop on every iteration: its one external driver is
	///     `UITimerManager::routine`'s `TimerName::GRAPHICS_ROUTINE` arm, re-armed at 15 ms
	///     (`gui/ui_timer_manager.cpp`); `ShellHostUI` is the host that has to decide
	///     what a zero costs. Its port is therefore
	///     `0`, "repaint on the next tick" -- the case `Scene::next_timer`'s own contract
	///     spells out -- for as long as the cursor
	///     is live; the shell's own
	///     router arms repaints with exactly `request_repaint_after(0, now_ms)`
	///     (`Shell::tick`) and the grid flush diffs against `last_sent`, so a tick on
	///     which the column did not move costs a repaint and no blit.
	///   * **the marker blink.** `timerCallback` re-arms `TimerName::UI_SPECIFIC` at
	///     `kSampleMarkerBlinkTime` (`AudioClipView::timerCallback`), and only the two marker columns
	///     change with it -- so it is armed only while a marker is visible.
	///
	/// @return Milliseconds from now, or `std::nullopt`.
	std::optional<uint32_t> next_timer() const override;
	/// @brief `{.oled = false, .grid = true}`: neither the playhead nor the marker blink touches
	///        the OLED page, which draws the output's name and nothing time-dependent.
	///
	/// Arming the OLED from this timer would be a permanent unconditional blit at the playhead's
	/// own cadence -- the defect `Scene::timer_repaints` exists to prevent (`ui/shell/scene.h`).
	RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

private:
	/// @brief `AudioClipView::focusRegained`'s portable half (`audio_clip_view.cpp`).
	///
	/// `indicator_leds::setLedState(IndicatorLED::BACK, false)` is `LedFrame`'s, and this
	/// scene declares no LEDs yet; `view.focusRegained()` and
	/// `view.setActiveModControllableTimelineCounter` are `services::ModKnobs`'s and
	/// `services::MidiLearn`'s, which the scene holds no handle to; `drawCurrentClipPad` is
	/// inside `#ifdef currentClipStatusButtonX`, which is defined nowhere in this repository.
	void focus_regained();

	/// @brief `AudioClipView::graphicsRoutine` (`audio_clip_view.cpp`) minus its paint.
	///
	/// Called from `render_grid`, before the frame's read, as the main loop runs `UI::graphicsRoutine` before
	/// `doAnyPendingUIRendering` (`origin/main:src/deluge/deluge.cpp`).
	/// @return The column for every row, or 255.
	int32_t compute_tick_square();

	/// @name What the scene reads: the clip's record, and the facts no record holds
	/// @{
	/// @return The clip's record, or null.
	[[nodiscard]] const deluge::project::DocClip* doc() const;
	/// @return The clip's loop length in ticks, from its record.
	[[nodiscard]] int32_t loop_length() const;
	/// @return What the clip's view needs of it that no record holds, or nullopt.
	[[nodiscard]] std::optional<deluge::ui::scenes::bind::AudioClipFact> fact() const;
	/// @return Whether the clip records a take linearly now.
	[[nodiscard]] bool recording_linearly() const;
	/// @return The clip's waveform address.
	[[nodiscard]] deluge::ui::scenes::bind::WaveformAddress waveform() const;
	/// @return `getSample()`'s test (`audio_clip_view.cpp`): is there a waveform -- the take while recording
	///         linearly, otherwise the clip's file?
	[[nodiscard]] bool has_sample() const;
	/// @return The audio track the clip plays on, or nullopt.
	[[nodiscard]] std::optional<deluge::project::TrackAddress> track() const;
	/// @return Row @p path of @p group on the clip's track, as text; empty where there is none.
	[[nodiscard]] std::string track_row(deluge::project::SettingGroupId group, std::string_view path) const;
	/// @return The track's audio mode, an index of `project::kAudioModeWords`.
	[[nodiscard]] int32_t audio_mode() const;
	/// @}

	/// @name The installed ports, each null with no set installed or none in it
	/// @{
	/// @return The clip facts, or null.
	[[nodiscard]] const deluge::ui::scenes::bind::ClipFacts* clips() const;
	/// @return Where the clip edits go, or null.
	[[nodiscard]] deluge::ui::scenes::bind::ClipWrites* clip_writes() const;
	/// @return Where the setting writes go, or null.
	[[nodiscard]] deluge::ui::scenes::bind::WriteSink* writes() const;
	/// @return The device's settings, or null.
	[[nodiscard]] const deluge::ui::scenes::bind::DeviceSettings* device() const;
	/// @return The transport's state, or null.
	[[nodiscard]] const deluge::ui::scenes::bind::Transport* transport() const;
	/// @return The transport's actions, or null.
	[[nodiscard]] deluge::ui::scenes::bind::TransportActions* transport_actions() const;
	/// @return The waveforms, or null.
	[[nodiscard]] const deluge::ui::scenes::bind::Waveforms* waveforms() const;
	/// @}

	/// @name The UI-mode word's three operations, transcribed from `gui/ui/ui.cpp`
	/// See the class docstring for what this scene keeps a mode word for.
	/// @{
	/// @brief `enterUIMode` (`gui/ui/ui.cpp`).
	/// @param mode One of the two `kUiMode*` values.
	void enter_ui_mode(uint32_t mode);
	/// @brief `exitUIMode` (`gui/ui/ui.cpp`).
	/// @param mode One of the two `kUiMode*` values.
	void exit_ui_mode(uint32_t mode);
	/// @brief `isUIModeActive` (`gui/ui/ui.cpp`).
	/// @param mode One of the two `kUiMode*` values.
	/// @return Whether it is active.
	[[nodiscard]] bool ui_mode_active(uint32_t mode) const;
	/// @brief `isNoUIModeActive` (`gui/ui/ui.cpp`).
	/// @return Whether the word is clear.
	[[nodiscard]] bool no_ui_mode_active() const { return ui_mode_ == kUiModeNone; }
	/// @}

	/// @name The input arms
	/// One private function per legacy entry point, so each one's citation sits on the body that
	/// ports it rather than on a `case` label.
	/// @{
	/// @brief `AudioClipView::buttonAction` (`audio_clip_view.cpp`).
	/// @param event The button event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result button_action(const deluge::ui::shell::UiEvent& event,
	                                        const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::padAction`.
	/// @param event The pad event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result pad_action(const deluge::ui::shell::UiEvent& event,
	                                     const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::horizontalEncoderAction` (`audio_clip_view.cpp`).
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	/// @brief Route @p event to the arm that takes it; `handle` is this, then `publish_navigation`.
	/// @param event The event. @param ctx Its context.
	/// @return What the arm returned.
	deluge::ui::shell::Result dispatch(const deluge::ui::shell::UiEvent& event,
	                                   const deluge::ui::shell::UiContext& ctx);
	/// @brief Write the view's scroll and zoom as the song's clip navigation when either moved since the last write.
	void publish_navigation();
	deluge::ui::shell::Result horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `ClipView::horizontalEncoderAction` (`gui/views/clip_view.cpp`), the three arms
	///        `AudioClipView`'s `else` delegates to.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result clip_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `ClipNavigationTimelineView::horizontalEncoderAction`
	///        (`gui/views/clip_navigation_timeline_view.cpp`) plus the zoom/scroll gates of
	///        `TimelineView::horizontalEncoderAction` (`gui/views/timeline_view.cpp`).
	///
	/// **This is where `TimelineState::stop_following()` is called, as the first statement**, which
	/// is exactly where legacy's `xScrollBeforeFollowingAutoExtendingLinearRecording = -1` sits
	/// (`clip_navigation_timeline_view.cpp`) -- above every gate below it, and below every
	/// decision above it. See the class docstring's `@par` for why the clear is not the first
	/// statement of `AudioClipView::horizontalEncoderAction`, one function higher.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn -- `now_ms` for the zoom delay, `shift` for the
	///               scroll arm's own gate (`TimelineView::horizontalEncoderAction`).
	/// @return What the shell should do.
	deluge::ui::shell::Result nav_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::verticalEncoderAction`.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result vertical_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief `AudioClipView::selectEncoderAction`.
	/// @param offset Signed detents.
	/// @param shift  Is shift held?
	/// @return True if anything changed, so the caller can ask for a repaint.
	bool select_encoder_action(int32_t offset, bool shift);
	/// @brief `buttonAction`'s `deactivateMarkerIfNecessary` tail (`audio_clip_view.cpp`).
	/// @return True if the end marker was up and has been put down.
	bool deactivate_marker_if_necessary();
	/// @brief `buttonAction`'s `BACK` arm's body (`audio_clip_view.cpp`).
	/// @return The l10n string id of the popup legacy shows, or 0 with no clip to clear.
	uint16_t clear_clip();
	/// @}

	/// @name The length gestures
	/// @{
	/// @brief `AudioClipView::editClipLengthWithoutTimestretching`.
	/// @param offset Signed detents.
	/// @return What the shell should do.
	deluge::ui::shell::Result edit_clip_length_without_timestretching(int32_t offset);
	/// @brief `ClipView::changeClipLength`: the new length from the view's
	///        squares, its request, and the scroll and zoom after.
	/// @param offset     Signed detents.
	/// @param old_length The clip's length before the edit.
	/// @param kind       STRETCH for the shift turn, NO_STRETCH for the shift turn with the encoder's button held.
	/// @return The new length.
	uint32_t change_clip_length(int32_t offset, uint32_t old_length, deluge::ui::scenes::bind::ResizeKind kind);
	/// @brief `AudioClipView::adjustLoopLength`'s view half (`audio_clip_view.cpp`), after a length request.
	/// @param old_length The length before the request.
	/// @param new_length The length after it.
	void after_loop_length_change(int32_t old_length, int32_t new_length);
	/// @brief Tell the clip port where a redo of the last length change scrolls back to: the view as it is now.
	void note_undo_scroll();
	/// @brief `TimelineView::displayNumberOfBarsAndBeats`' popup half.
	///
	/// The arithmetic is `TimelineState::bars_and_beats`; this is the `sprintf` and the popup.
	/// @param number         The tick position or length to describe.
	/// @param count_from_one True to number bars/beats from one.
	void show_bars_and_beats(uint32_t number, bool count_from_one);
	/// @}

	/// The clip this scene shows and the song's record it lives in (see `open`), and the ports `set_ports` installed.
	/// @{
	std::optional<deluge::project::ClipAddress> clip_;
	const deluge::project::Song* model_ = nullptr;
	const deluge::ui::scenes::bind::Ports* ports_ = nullptr;
	/// @}

	/// The redraws of a frame whose read left columns pending, with no give-up: `AudioClipView::renderMainPads`
	/// (`origin/main:src/deluge/gui/views/audio_clip_view.cpp`) asks to render again after every scan that could not
	/// load its clusters. The scene keeps no columns: the waveforms port does.
	deluge::ui::scenes::services::PeakRetry retry_{deluge::ui::scenes::services::PeakRetry::Patience::UNTIL_ANSWERED};

	/// The scroll and zoom, with `AudioClipView::supportsTriplets() == false`
	/// (`audio_clip_view.h`) baked into the state at construction.
	timeline::TimelineState timeline_{.supports_triplets = false};
	/// The scroll and zoom last written as the song's clip navigation, or adopted from it.
	struct Published {
		int32_t x_scroll = 0;
		uint32_t x_zoom = 0;
	} published_;

	/// `AudioClipView::endMarkerVisible` / `startMarkerVisible` (`audio_clip_view.h`).
	/// @{
	bool end_marker_visible_ = false;
	bool start_marker_visible_ = false;
	/// @}
	/// `AudioClipView::blinkOn` (`audio_clip_view.h`).
	bool blink_on_ = false;
	/// Milliseconds accumulated towards the next `blink_on_` toggle -- the port of
	/// `TimerName::UI_SPECIFIC`'s remaining time, which this layer has no clock to read.
	uint32_t blink_accum_ms_ = 0;

	/// `AudioClipView::lastTickSquare` / `mustRedrawTickSquares` (`audio_clip_view.h`), which
	/// `ui/scenes/timeline/timeline.h`'s table assigns to this scene by name.
	/// @{
	int32_t last_tick_square_ = 0;
	bool must_redraw_tick_squares_ = false;
	/// `graphicsRoutine` handed `PadLEDs::setTickSquares` a new square this render, which the fast cursor flashes.
	bool tick_squares_set_ = false;
	/// Each row's square at the last `setTickSquares`: legacy's `slowFlashSquares`.
	std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> flashed_ticks_ = filled_no_tick();
	static constexpr std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> filled_no_tick() {
		std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> a{};
		a.fill(deluge::ui::widgets::grid::kNoTickSquare);
		return a;
	}
	/// @}

	/// `PadLEDs::flashCursor`. See the class docstring's warning.
	/// @brief The flash-cursor setting: the device's `pads/playCursor` row; the factory's Slow, the row's default, with
	///        no device port.
	project::FlashCursor flash_cursor() const {
		return bind::read_device<project::FlashCursor>(device(), project::device_path::kPlayCursor);
	}

	/// Legacy's `currentUIMode` (`gui/ui/ui.cpp`), restricted to the two values this scene sets.
	/// See the class docstring's `@par` on the UI modes.
	uint32_t ui_mode_ = kUiModeNone;

	/// `AudioClipView::timeSongButtonPressed` (`audio_clip_view.h`), in the shell's `now_ms`
	/// rather than in `AudioEngine::audioSampleTimer` -- this layer has no clock of its own
	/// (`shell-no-clock`), and `UiContext::now_ms` is the one the shell hands in.
	uint32_t song_button_pressed_ms_ = 0;
};

/// @brief The application's one audio clip view.
///
/// A function-local static, for the reason every other scene singleton in `src/ui/scenes` is one.
/// @return The singleton.
AudioClipScene& audio_clip_scene();

} // namespace deluge::ui::scenes::audio_clip
