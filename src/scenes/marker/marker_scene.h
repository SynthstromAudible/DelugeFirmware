#pragma once

/// @brief `MarkerScene` -- the sample marker editor: a waveform on the pads, four markers over it,
///        and the position of the selected one on the OLED.
///
/// The port of `SampleMarkerEditor` (`gui/ui/sample_marker_editor.{h,cpp}`), which the sample
/// menu's start/end items and the audio clip's waveform item open. `SceneId::arg` is the
/// `MarkerType` to start on -- `MarkerType::START` or `MarkerType::END`, which are the only two
/// legacy ever opens with (`gui/menu_item/sample/start.h:23`, `end.h:23`,
/// `gui/menu_item/audio_clip/sample_marker_editor.h:33`); the loop markers are reached from
/// *inside*, by holding a marker and tapping another column.
///
/// **What draws what.** Nothing here calls an `OledFrame` or `GridFrame` primitive
/// (`check_layer_rules.py`, `scenes-no-frame-primitives`):
///
/// | legacy | here |
/// |---|---|
/// | `renderMainPads`' `waveformRenderer.renderFullScreen` (`:1369`) | `deluge::sample::find_peaks_per_col` +
/// `ui::grid::waveform_full` | | `renderColumn` / `renderMarkerInCol` (`:1268`, `:1058`) | `ui::grid::marker_column` /
/// `marker_tint` | | `renderOLED`'s `drawScreenTitle` (`:1128`) | `ui::oled::title` | | its lock icon + `invertArea`
/// (`:1130-1133`) | `ui::oled::title_badge` | | its time and sample-count lines (`:1135-1222`) |
/// `ui::oled::sample_position` |
///
/// The scan and the navigation are not widgets -- they read `Sample`, `SampleHolder` and the card,
/// so they live one layer down in `model/sample/waveform_scan.h` and
/// `model/sample/waveform_navigator.h`, which this layer may include and `src/ui` may not. That
/// split is `ui/grid/waveform.h`'s own, written down before this scene existed.
///
/// @par The waveform is on the pads only
///
/// `SampleMarkerEditor::renderOLED` draws **no waveform at all** -- a title, an optional lock badge,
/// a duration and a sample count. So there is no `src/ui/oled/` waveform component in this task and
/// none was invented: `ui::oled::sample_position` is the OLED half, and `ui::grid::waveform_full`
/// (which predates this task) plus `ui::grid::markers` are the pad half.
///
/// @par What is deliberately not ported
///
///   * **The collapse/explode animation.** `WaveformRenderer::renderOneColForCollapseAnimation` and
///     `...ZoomedOut` have exactly one caller, `PadLEDs::renderExplodeAnimation`
///     (`hid/led/pad_leds.cpp:551`, `:629`), driven by `sessionView`/`arrangerView` setting
///     `collapseAnimationToWhichRow` (`gui/views/session_view.cpp:2704` and three others).
///     **`SampleMarkerEditor` never reaches it**, so it is not this scene's to port -- not a
///     judgement call, a call-graph fact.
///   * **The zoom transition animation.** `WaveformBasicNavigator::zoom` follows its arithmetic with
///     a `PadLEDs` zoom transition (`waveform_basic_navigator.cpp:44-67` after the split). The shell
///     has no transition surface, so the scene holds a plain `deluge::sample::WaveformNav` and gets
///     the arithmetic alone: a zoom lands on the new view immediately instead of sliding into it.
///   * **The playhead.** `graphicsRoutine` (`:899`) walks the sound's voices for a `VoiceSample` and
///     sets `PadLEDs::setTickSquares`. Tick squares are a `PadLEDs` facility with no shell
///     equivalent yet, and the walk needs `Sound::voices()`; both are left for whoever ports tick
///     squares.
///   * **`verticalEncoderAction`** (`:871`), which is a straight delegation to
///     `instrumentClipView` -- a root UI this layer may not name (`scenes-no-root-ui`).
///   * **`soundEditor.potentialShortcutPadAction`** (`:346`), the shortcut-pad jump out of the
///     editor. `MenuScene` owns the shortcut tables; a marker editor that jumped to another menu
///     item would have to file that navigation, and no node points at this scene yet.
///
/// @par The blink, and a disagreement inside legacy
///
/// Legacy runs a four-phase blink on `TimerName::UI_SPECIFIC` at `kSampleMarkerBlinkTime`
/// (`:1402`, `:866`) and **renders it two different ways**. `timerCallback` (`:771`) redraws only
/// the affected columns: phase 1 paints the selected column as one solid marker bar, phase 3
/// suppresses the selected marker (and, when the loop is locked, its partner), and phases 0 and 2
/// draw normally. `renderMainPads` (`:1362`) -- which runs on a *full* redraw, not on the timer --
/// suppresses whenever `blinkPhase >= 1`, so a full redraw landing on phase 2 hides the marker
/// where the timer would have shown it.
///
/// **This scene follows `timerCallback`**, because that is the path that actually drives the blink
/// the user sees; `renderMainPads`' phase-2 arm only shows on the rare tick where something else
/// forced a whole-screen redraw. Recorded rather than reproduced: reproducing it would mean making
/// the port's render depend on *why* it was called, which the shell does not tell it.
///
/// @warning **This scene must not raise `Action::Kind::LockInput`**, and does not. See the warning
///          on `Action` in `shell/scene.h`: it is one bare global bool, it *drops* events rather
///          than deferring them, and `on_exit()` cannot give it back. Legacy's own gate here is
///          `isSDRoutineActive()` -> `REMIND_ME_OUTSIDE_CARD_ROUTINE` (`:341`, `:731`), which
///          defers and retries; its port is `InputLowering::set_sd_busy`, which is not an `Action`
///          and is not this scene's to file.

#include "scenes/menu/context.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/grid/effects.h"
#include "ui/grid/markers.h"
#include "ui/grid/waveform.h"

#include "definitions_cxx.hpp"
#include "model/sample/waveform_navigator.h"

#include <cstdint>
#include <optional>
#include <span>

class MultisampleRange;
class SampleHolder;

namespace deluge::scenes::marker {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 4` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kMarkerSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 4);

/// @brief How long one blink phase lasts. Legacy's `kSampleMarkerBlinkTime` (`definitions_cxx.hpp:161`).
inline constexpr uint32_t kBlinkPhaseMs = static_cast<uint32_t>(kSampleMarkerBlinkTime);

/// @brief The four marker colours, in `MarkerType` order.
///
/// `SampleMarkerEditor::renderMarkerInCol`'s switch (`sample_marker_editor.cpp:1065-1083`), lifted
/// out because `ui::grid::marker_column` takes colours rather than marker types -- it may not
/// include `definitions_cxx.hpp` and so cannot know what a `MarkerType` is.
/// @return A span of `kNumMarkerTypes` colours: green, cyan, magenta, red.
std::span<const RGB> marker_colours();

/// @brief The sample marker editor. One instance for the whole application (`registry.h`).
class MarkerScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// The entry point for whoever opens the editor, exactly as `RenameScene::open` and
	/// `BrowserScene::open` are: `SceneId` carries one `uint16_t` and a context is not one, so the
	/// context arrives here and `SceneId::arg` (the `MarkerType`) arrives at `on_enter`.
	/// @param ctx What the markers are read from and written to.
	void open(const menu::MenuContext& ctx);

	/// @brief May the editor open on @p ctx at all?
	///
	/// The survivable spelling of `SampleMarkerEditor::opened`'s two refusals: no `Sample` on the
	/// holder (`:121-124`, which pops up `STRING_FOR_NO_SAMPLE` and returns false) and -- an
	/// addition, because this layer resolves the holder itself -- no holder to look at. Legacy gets
	/// the second for free from `LoopPoint::checkPermissionToBeginSession`
	/// (`gui/menu_item/sample/loop_point.cpp:47-49`), which refuses before opening.
	/// @param ctx The context to test.
	/// @return True if `on_enter` will succeed.
	static bool can(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kMarkerSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief True: the waveform covers the main pads, and the sidebar beneath keeps showing.
	///
	/// `SampleMarkerEditor::renderMainPads` fills every main column; `renderSidebar` (`:891`)
	/// returns false unless the root UI is the keyboard screen, in which case it delegates -- a root
	/// UI read this layer may not make, so the sidebar is simply left to whatever is beneath.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the sidebar underneath still needs to paint.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows`, it never halves every pad.
	bool dims_grid() const override { return false; }
	/// @brief `SampleMarkerEditor::getGreyoutColsAndRows` (`:106-109`): `*cols = 0b10`, rows 0.
	deluge::ui::grid::GreyoutMask greyout_mask() const override { return {0b10, 0}; }

	/// @name Observable state
	/// For specs, and for whoever wires the menu items up.
	/// @{
	/// @return Which marker the encoder and an untargeted pad press move.
	MarkerType marker_type() const { return marker_type_; }
	/// @return The left edge of the view, in samples.
	int64_t x_scroll() const { return nav_.xScroll; }
	/// @return Samples per column.
	int64_t x_zoom() const { return nav_.xZoom; }
	/// @return The blink's phase, 0..3 (`SampleMarkerEditor::blinkPhase`).
	int8_t blink_phase() const { return blink_phase_; }
	/// @return Is a marker pad being held (legacy's `UI_MODE_HOLDING_SAMPLE_MARKER`)?
	bool holding_marker() const { return holding_; }
	/// @}

private:
	/// @brief `SampleMarkerEditor::getColsOnScreen` (`:236`).
	/// @param[out] cols `kNumMarkerTypes` entries, filled in.
	void cols_on_screen(MarkerColumn* cols) const;
	/// @brief `SampleMarkerEditor::writeValue` (`:148`).
	/// @param value The new position, in samples.
	/// @param which Which marker to write, or `NOT_AVAILABLE` for the selected one.
	void write_value(uint32_t value, MarkerType which = MarkerType::NOT_AVAILABLE);
	/// @brief `SampleMarkerEditor::selectEncoderAction` (`:268`).
	/// @param offset Signed detents.
	void select_encoder(int32_t offset);
	/// @brief `SampleMarkerEditor::padAction`'s main-pad arms (`:367-641`).
	/// @param x  Pad column.
	/// @param y  Pad row.
	/// @param on True on press.
	void pad_action(int32_t x, int32_t y, bool on);
	/// @brief `SampleMarkerEditor::horizontalEncoderAction` (`:687`).
	/// @param offset  Signed detents.
	/// @param zooming Is the horizontal encoder's button held?
	/// @param shift   Is shift held?
	/// @return Actions to file (the loop-length popups).
	deluge::ui::shell::Result horizontal_encoder(int32_t offset, bool zooming, bool shift);
	/// @brief `SampleMarkerEditor::shouldAllowExtraScrollRight` (`:1044`).
	bool allow_extra_scroll_right() const;
	/// @brief `SampleMarkerEditor::recordScrollAndZoom` (`:140`).
	void record_scroll_and_zoom();
	/// @brief `SampleMarkerEditor::reverseRemap` (`:83`).
	/// @param type The marker to remap.
	/// @return Its mirror when the sample is reversed, else @p type unchanged.
	MarkerType reverse_remap(MarkerType type) const;
	/// @brief `isLoopLocked()` (`:79`).
	bool loop_locked() const;
	/// @brief `SampleMarkerEditor::loopUnlock` / `loopLock` (`:1225`, `:1231`), minus their popups
	///        -- those are filed by the caller, which is the only place that can raise an `Action`.
	/// @param locked The new state.
	void set_loop_locked(bool locked);

	/// @brief The holder the markers live on: the audio clip's, or the multisample range's
	///        (`getCurrentSampleHolder`, `:62`).
	/// @return The holder, or null when the context resolves to neither.
	SampleHolder* holder() const;
	/// @brief `getCurrentMultisampleRange` (`:58`), or null on an audio clip.
	MultisampleRange* multi_range() const;

	menu::MenuContext ctx_{};
	deluge::sample::WaveformNav nav_{};

	MarkerType marker_type_ = MarkerType::START;
	int8_t blink_phase_ = 0;
	uint32_t blink_accum_ms_ = 0;

	/// Legacy's `UI_MODE_HOLDING_SAMPLE_MARKER` plus `pressX`/`pressY` (`:538-540`), as members
	/// rather than a global UI mode.
	/// @{
	bool holding_ = false;
	int8_t press_x_ = -1;
	int8_t press_y_ = -1;
	/// @}

	/// True while the last `render_grid` saw a marker inside the display, which is legacy's
	/// condition for re-arming the blink timer (`:1400-1403`).
	///
	/// @note **Two different widths decide "on screen" in this scene, and they can disagree.**
	///       This one is `render_grid`'s `main.bounds().w` -- the frame it was actually given. The
	///       *scroll* and *hit-test* arms (`select_encoder`'s `old_col >= 0 && old_col <
	///       kDisplayWidth`, `pad_action`'s column tests, and `cols_on_screen`'s `kInvalidColumn`
	///       arithmetic) use `kDisplayWidth`, because they are transcriptions of legacy, which has
	///       only that constant. On every shipped board the two are both 16 and the question does
	///       not arise; on a wider grid the blink would follow the frame while scrolling followed
	///       the constant. Deliberately not unified here: making the scroll arms read a width the
	///       render discovered would mean the port stopped matching legacy's arithmetic, which is
	///       what every comparison in `marker_scene_spec` is written against.
	bool marker_on_screen_ = false;
};

/// @brief The application's one sample marker editor.
/// @return The singleton.
MarkerScene& marker_scene();

} // namespace deluge::scenes::marker
