#pragma once

/// @brief `SlicerScene` -- the slicer: a waveform on the pads, a slice count on the OLED, and (in
///        MANUAL mode) a 64-slot slice grid under the waveform.
///
/// The port of `Slicer` (`gui/ui/slicer.{h,cpp}`), which `SampleBrowser` and the sample browser's
/// kit context menu open once a sample has been chosen. This task covers the state, the two
/// renders and the three encoder arms; `padAction`, the rest of `buttonAction`, the audition
/// preview and the slice commit are Task 10's.
///
/// @par This is NOT a root scene
///
/// The plan's prose calls it one; it is not. `class Slicer final : public UI` (`gui/ui/slicer.h:33`)
/// -- a plain `UI`, opened with `openUI(&slicer)` from `gui/ui/browser/sample_browser.cpp:378` and
/// `gui/context_menu/sample_browser/kit.cpp:77`, and `grep "public RootUI"` finds exactly two
/// classes in the tree, `TimelineView` and `KeyboardScreen` (`shell/scene.h:385`, which already
/// records this). So `is_root()` is false, and `services::GlobalControls` correctly gates the
/// `View::buttonAction` group of legacy's global button arms off it: with the slicer up, those arms
/// are unreachable in legacy too, because `UI::buttonAction`'s base returns `NOT_DEALT_WITH`
/// without forwarding to `view` (`gui/ui/ui.h:98-100`).
///
/// It does not consume `scenes::timeline::TimelineState` either: legacy's slicer navigates a
/// *sample*, through `waveformBasicNavigator`, not a timeline.
///
/// @par The four composition predicates
///
/// | predicate | value | legacy |
/// |---|---|---|
/// | `is_root()` | false | `class Slicer final : public UI` (`slicer.h:33`) |
/// | `oled_transparent(role)` | true | `Slicer()` sets `oledShowsUIUnderneath = true` (`slicer.h:35`), and `renderOLED`
/// then clears only its own window | | `is_grid_overlay()` | true | `renderMainPads` (`slicer.cpp:103-152`) returns
/// true, so it is drawn over whatever root is beneath and is not the grid base | | `occludes_grid()` | **false** |
/// `Slicer` overrides `renderMainPads` only. It has no `renderSidebar`, and `UI::renderSidebar`'s base returns false
/// (`gui/ui/ui.h:148-151`), so the sidebar beneath keeps painting -- exactly `MarkerScene`'s situation | |
/// `dims_grid()` | false | legacy never halves what is beneath; `Slicer` has no `getGreyoutColsAndRows` either, so
/// `getUIGreyoutColsAndRows` (`gui/ui/ui.cpp:73-83`) walks past it to the browser below |
///
/// @par What draws what
///
/// Nothing here calls an `OledFrame` or `GridFrame` primitive (`check_layer_rules.py`,
/// `scenes-no-frame-primitives`), and nothing names `waveformRenderer` or `waveformBasicNavigator`
/// (`scenes-no-legacy-waveform`):
///
/// | legacy | here |
/// |---|---|
/// | `renderOLED`'s window, rule, label and centred count (`slicer.cpp:74-98`) | `ui::oled::count_window` |
/// | `renderMainPads`' `waveformRenderer.renderFullScreen` (`:108`, `:114`) | `deluge::sample::find_peaks_per_col` +
/// `ui::grid::waveform_full` | | the MANUAL slice grid and slice marks (`:117-149`) | `GridFrame::pad` through the
/// region, with the colours resolved here |
///
/// @par `graphicsRoutine` -- the playhead -- is deliberately not ported
///
/// `Slicer::graphicsRoutine` (`slicer.cpp:156-203`) walks the current kit's first drum for a
/// sounding `VoiceSample`, turns its play position into a column, and hands rows 4..7 to
/// `PadLEDs::setTickSquares`. Two halves, and only one of them is ready:
///
///   * the *paint* half now exists in the toolkit (`ui::grid::tick_squares`, `ui/grid/columns.h`),
///     which closes the gap `MarkerScene` recorded for the same routine;
///   * the *walk* does not. It needs `Sound::voices()` to report a live voice, which means a
///     running audio engine, and no spec in this tree can produce one -- `sim/scenes_spec`'s
///     fixtures build a `Song` by direct construction with no `AudioEngine::init()` behind them.
///     Every branch of the column arithmetic (`:186-192`) would therefore ship unmeasured, and the
///     only branch a spec CAN reach is "no voices, no tick square", which asserts nothing.
///
/// Ported as a consequence: **nothing, and no timer.** `graphicsRoutine` is a per-tick redraw
/// driven by audio, not a deadline (it is called from the main loop, never from
/// `uiTimerManager`), so with the playhead absent this scene has no time-driven state at all --
/// `next_timer()` stays `std::nullopt` and `timer_repaints()` is left at its default rather than
/// being overridden to claim surfaces nothing would repaint. Whoever ports the walk owns both.
///
/// @par Two legacy defects found while porting, reproduced or clipped rather than fixed
///
///   * **`Slicer::renderMainPads` does not check `image != nullptr`.** `UI`'s own header says it
///     must ("These *must* check whether image has been supplied", `gui/ui/ui.h:141-142`) and
///     `SampleMarkerEditor::renderMainPads` does (`sample_marker_editor.cpp:1362-1366`). `closeUI`
///     quizzes every UI above the one being closed with the no-argument form
///     (`gui/ui/ui.cpp:186`), and closing the slicer itself takes that path, so
///     `waveformRenderer.renderFullScreen` is reached with a null image and memsets through it.
///     Reported, not fixed -- it is a legacy edit this plan does not authorise. The port has no
///     equivalent: `render_grid` is always handed a real frame.
///   * **The MANUAL slice mark can land in the sidebar.** `image[4][x]` with
///     `x = startPos / lengthInSamples * 16` (`:123-128`) is 16 when `startPos` has been driven to
///     `lengthInSamples`, which `horizontalEncoderAction`'s clamp (`:220-221`) allows -- and column
///     16 is the first sidebar column. Legacy composes the grid **top-down**
///     (`doAnyPendingGridRendering`, `gui/ui/ui.cpp:305-349`), so the sidebar UI beneath overwrites that pad later in
///     the same pass; the shell composes **bottom-up** (`Shell::render_grid`, `shell/shell.cpp:682-798`: base first,
///     then overlays), so reproducing the write here would leave the stray pad visible where legacy's is not. The port
///     therefore paints the slice marks inside the main-column region, which clips it. Behaviour
///     preserved by clipping, not by transcription -- the one place in this file where those differ.
///
/// @par Why the directory and namespace are `slicing/` and not `slicer/`
///
/// The plan asks for `src/scenes/slicer/slicer_scene.{h,cpp}`, and that path cannot exist. The
/// `scenes-no-root-ui` ratchet (`scripts/check_layer_rules.py`'s `ROOT_UI_GLOBALS`) lists the
/// legacy global `slicer` (`gui/ui/slicer.cpp:55`) among the names `src/scenes/**` may never spell,
/// as a whole-word pattern -- and `#include "scenes/slicer/slicer_scene.h"` contains that whole
/// word, so the directory name alone is a violation on every file that includes this header,
/// whatever the namespace is called. Global Constraint 10 (the baseline may only shrink) is the
/// stronger of the two requirements, so the directory and the namespace are `slicing` and the file
/// names are the plan's. `SlicerScene`, `kSlicerSceneKind` and `slicer_scene()` are all unaffected:
/// `\bslicer\b` does not match a name that continues into another word character.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on
///          `Action` in `shell/scene.h`.

#include "shell/scene.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include "definitions_cxx.hpp"
#include "model/sample/waveform_navigator.h"

#include <cstdint>

class Sample;

namespace deluge::scenes::slicing {

/// @brief Legacy's `SLICER_MODE_REGION` / `SLICER_MODE_MANUAL` (`gui/ui/slicer.h:24-25`), as an
///        enum rather than two `#define`s.
///
/// The values are legacy's, because `Slicer::buttonAction`'s `X_ENC` arm advances the mode with
/// `slicerMode++; slicerMode %= 2` (`slicer.cpp:285-286`).
enum class Mode : uint8_t {
	/// Cut the sample into `num_clips()` equal regions. The OLED shows that count; the pads show
	/// the whole waveform.
	Region = 0,
	/// Place up to `kMaxManualSlices` slice points by hand. The OLED shows how many are placed; the
	/// pads show the waveform squashed into the top half and a 64-slot grid in the bottom half.
	Manual = 1,
};

/// @brief Legacy's `MAX_MANUAL_SLICES` (`gui/ui/slicer.h:26`).
inline constexpr int32_t kMaxManualSlices = 64;

/// @brief One manual slice: legacy's `SliceItem` (`gui/ui/slicer.h:28-31`).
struct SlicePoint {
	/// Sample-frame position where this slice starts.
	int32_t start_pos = 0;
	/// Semitones to transpose the slice by, `-24 .. 24` (`slicer.cpp:237-240`).
	int32_t transpose = 0;
	bool operator==(const SlicePoint&) const = default;
};

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 8` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kSlicerSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 8);

/// @brief The slicer. One instance for the whole application (`registry.h`).
class SlicerScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at the sample the browser previewed.
	///
	/// The entry point for whoever opens the slicer, split from `on_enter` for the reason
	/// `MarkerScene::open` is. Legacy has no equivalent line inside `Slicer` at all: it reads the
	/// **`waveformBasicNavigator` global**, which the sample browser's preview path filled in with
	/// `waveformBasicNavigator.sample = sample; waveformBasicNavigator.opened();`
	/// (`SampleBrowser::renderPreviewForTarget`, `gui/ui/browser/sample_browser.cpp:617-619`)
	/// before the legacy object is opened. Those two lines are
	/// this call, onto a navigator this scene owns rather than a shared global.
	/// @param sample The sample to slice; null leaves the scene with nothing to draw.
	void open(Sample* sample);

	/// @brief Switch between REGION and MANUAL.
	///
	/// The **state half** of `Slicer::buttonAction`'s `X_ENC` arm (`slicer.cpp:283-294`). The arm
	/// itself -- the button routing, `AudioEngine::stopAnyPreviewing()` and
	/// `getCurrentKit()->firstDrum->killAllVoices()` -- belongs with the rest of `buttonAction` and
	/// the preview machinery it tears down, which is Task 10's; this is the piece the render and
	/// the encoder arms branch on, so it lands with them.
	/// @param mode The mode to switch to.
	void set_mode(Mode mode);

	deluge::ui::shell::SceneKind kind() const override { return kSlicerSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;

	/// @brief False: a plain `UI`, not a `RootUI`. See the table in the class docstring.
	bool is_root() const override { return false; }
	/// @brief True: the waveform covers the main pads, and the sidebar beneath keeps showing.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: `Slicer` overrides `renderMainPads` but not `renderSidebar`, so the sidebar
	///        underneath still needs to paint (and, through `Shell::tick_stack`, still needs its
	///        clock advanced).
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy never halves the layer beneath for the slicer -- it has no
	///        `getGreyoutColsAndRows` of its own either. Same split `MarkerScene` makes.
	bool dims_grid() const override { return false; }
	/// @brief True: `Slicer()` sets `oledShowsUIUnderneath = true` (`gui/ui/slicer.h:35`), so
	///        `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp:351-371`) walks past it to the UI below
	///        and this page composites over what that drew.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return true; }

	/// @name Observable state
	/// Legacy's four public members (`gui/ui/slicer.h:62-65, 69`), for specs and for Task 10.
	/// @{
	/// @return REGION or MANUAL (`slicerMode`).
	Mode mode() const { return mode_; }
	/// @return How many equal regions REGION mode would cut (`numClips`).
	int32_t num_clips() const { return num_clips_; }
	/// @return How many manual slices are placed (`numManualSlice`).
	int32_t num_manual_slices() const { return num_manual_slices_; }
	/// @return Which manual slice the encoders move (`currentSlice`).
	int32_t current_slice() const { return current_slice_; }
	/// @param i Slice index, `0 .. kMaxManualSlices - 1`.
	/// @return That slice's start position and transpose (`manualSlicePoints[i]`).
	const SlicePoint& slice(int32_t i) const { return points_[i]; }
	/// @return The left edge of the waveform view, in samples.
	int64_t x_scroll() const { return nav_.xScroll; }
	/// @return Samples per waveform column.
	int64_t x_zoom() const { return nav_.xZoom; }
	/// @}

private:
	/// @brief `Slicer::horizontalEncoderAction` (`slicer.cpp:205-231`).
	/// @param offset Signed detents.
	/// @param shift  Is shift held? Legacy's `Buttons::isShiftButtonPressed()` (`:209`), which
	///               makes one detent 10 samples instead of 100.
	/// @return Always handled, in both modes, as legacy always returns `DEALT_WITH` (`:230`).
	deluge::ui::shell::Result horizontal_encoder(int32_t offset, bool shift);
	/// @brief `Slicer::verticalEncoderAction` (`slicer.cpp:233-246`).
	/// @param offset Signed detents.
	/// @return Always handled, in both modes (`:245`).
	deluge::ui::shell::Result vertical_encoder(int32_t offset);
	/// @brief `Slicer::selectEncoderAction` (`slicer.cpp:248-274`).
	/// @param offset Signed detents.
	/// @return Handled, with the repaint legacy files as `uiNeedsRendering` + `renderUIsForOled`.
	deluge::ui::shell::Result select_encoder(int32_t offset);

	/// The scroll/zoom and per-column peak cache. Legacy shares one global
	/// (`waveformBasicNavigator`) between the browser, the marker editor and the slicer; every
	/// port owns its own, as `MarkerScene` already does.
	deluge::sample::WaveformNav nav_{};

	/// `int16_t`, as legacy declares it (`gui/ui/slicer.h:69`) -- the wrap in `selectEncoderAction`
	/// is written against exactly 257 and 1, so the width is part of the behaviour.
	int16_t num_clips_ = 16;
	int32_t num_manual_slices_ = 1;
	int32_t current_slice_ = 0;
	Mode mode_ = Mode::Region;
	SlicePoint points_[kMaxManualSlices]{};
};

/// @brief The application's one slicer.
///
/// A function-local static, for the reason every other scene singleton in this tree is one.
/// @return The singleton.
SlicerScene& slicer_scene();

} // namespace deluge::scenes::slicing
