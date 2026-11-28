#pragma once

/// @brief `SlicerScene` -- the slicer: a waveform on the pads, a slice count on the OLED, and (in
///        MANUAL mode) a 64-slot slice grid under the waveform.
///
/// The port of `Slicer` (`origin/main:src/deluge/gui/ui/slicer.h`), which the sample browser opens on a file in a
/// brand-new kit: from the kit's SAMPLES menu (SLICE) or with Shift+Select.
///
/// @par Everything this scene does to the audio, the model and the card goes through `SliceOps`
///
/// `Slicer` reaches its work through four `gui/` globals -- `sampleBrowser`, `soundEditor`,
/// `instrumentClipView` and `PadLEDs` -- every one of which `src/ui/scenes/**` may never name
/// (`scripts/check_layer_rules.py`'s `scenes-no-root-ui` and `scripts/layers.toml`'s `scenes-includes-gui`). The calls
/// that reach them are collected into `SliceOps` below, and the scene keeps everything else: the state, the guards, the
/// snapshot at Select and the commit gate. The slice is asynchronous: the host answers `SliceOps::begin_commit` later
/// with `commit_done`. See `SliceOps` for what a null `ops()` means.
///
/// @par This is not a root scene
///
/// `class Slicer final : public UI` (`origin/main:src/deluge/gui/ui/slicer.h`) is a plain `UI`, opened with
/// `openUI(&slicer)`; the only `RootUI`s are `TimelineView` and `KeyboardScreen` (`ui/shell/scene.h`). So `is_root()`
/// is false, and `services::GlobalControls` gates the `View::buttonAction` group of legacy's global button arms off it:
/// with the slicer up, those arms are unreachable in legacy too, because `UI::buttonAction`'s base returns
/// `NOT_DEALT_WITH` without forwarding to `view` (`gui/ui/ui.h`). It does not consume
/// `scenes::timeline::TimelineState` either: legacy's slicer navigates a *sample*, through `waveformBasicNavigator`,
/// not a timeline.
///
/// @par The composition predicates
///
///   * **`is_root()` -> false**, above.
///   * **`oled_transparent(role) -> true`.** `Slicer()` sets `oledShowsUIUnderneath = true`
///     (`origin/main:src/deluge/gui/ui/slicer.h`), and `renderOLED` then clears only its own window.
///   * **`is_grid_overlay() -> true`.** `renderMainPads` returns true unconditionally, so this is drawn over whatever
///     root is beneath and is not the grid base.
///   * **`occludes_grid() -> false.`** `Slicer` overrides `renderMainPads` only. It has no `renderSidebar`, and
///     `UI::renderSidebar`'s base returns false (`gui/ui/ui.h`), so legacy's top-down grid walk keeps going and
///     the sidebar beneath paints -- `MarkerScene`'s situation. The predicate means "covers the grid completely, so
///     scenes beneath need not render" (`ui/shell/scene.h`), and `Shell::tick_stack` stops advancing clocks beneath a
///     scene that claims it; claiming it here would stop the clock of the scene that owns the sidebar.
///
/// `Slicer::canSeeViewUnderneath()` returns false (`origin/main:src/deluge/gui/ui/slicer.h`), the one legacy predicate
/// that points the other way. Neither of its consumers is about grid composition: `UI::graphicsRoutine` forwards the
/// per-tick redraw to the root only when it is true, which `Slicer` overrides anyway, and `Session::armingChanged` uses
/// it to decide whether the session view's OLED is on screen.
///
/// @par What draws what
///
/// Nothing here calls an `OledFrame` or `GridFrame` primitive (`scenes-no-frame-primitives`), and nothing names
/// `waveformRenderer` or `waveformBasicNavigator` (`scenes-no-legacy-waveform`):
///
///   * `renderOLED`'s window, rule, label and centred count -> `views::oled::slicer_count`.
///   * `renderMainPads`' two `waveformRenderer.renderFullScreen` calls -> the waveforms port's read at the SLICE
///     address (`services::read_waveform`), its columns (`present_waveform`) drawn by `views::grid::waveform`.
///   * the MANUAL slice grid and slice marks -> `views::grid::slice_marks`, over what `present_marks` gathers.
///   * `graphicsRoutine`'s playhead -> `views::grid::playhead` on rows 7 to 4 at the column `SliceOps::play_position`
///     reports (`present_playhead`), asked again every `kPlayheadMs` while something plays (`next_timer`).
///
/// A frame whose read left a column pending paints no main pad and asks again (`services::PeakRetry`).
///
/// @par Its ports are installed, not given by a context
///
/// The menu does not open this scene, so it has no `MenuContext`: it reads the set the host installs with `set_ports`
/// (`gui::shell_host::install_scene_ports`), each port through `bind::port_of`. With no set installed it reads not
/// auditioning, a Slow play cursor and no waveform, wipes no logs, and its `next_timer` never reads live.
///
/// @par The commit does not need `RecorderScene`'s settled-recorder wait
///
/// `RecorderScene::after_render` waits for `SampleRecorder::settled()` before it finishes a recording, because freeing
/// a `SampleRecorder` whose last cluster write is still out frees the buffer the driver writes from. The slicer's
/// commit is not that shape:
///
///   1. Only a `SampleRecorder` lends the driver a buffer like that, and `Slicer::doSlice`
///      (`origin/main:src/deluge/gui/ui/slicer.cpp`) frees no recorder: its one `delugeDealloc` releases a `SoundDrum`
///      it allocated itself, before `kit->addDrum` publishes it.
///   2. The commit is input-driven. Legacy refuses the Select from inside a card routine
///      (`REMIND_ME_OUTSIDE_CARD_ROUTINE`), and that defer-and-retry gate is `InputLowering::set_sd_busy`, not a
///      scene's business -- as `RecorderScene::handle`, `MarkerScene` and `BrowserScene` also hold.
///   3. The hazard the slice does have is re-entry: a Back or a second Select while its load is out would close the
///      slicer under it or slice twice. Legacy guards it with the `currentUIMode` gate; this scene carries it as
///      `committing()`, held from Select until the host's `commit_done`.
///
/// @par `on_resume()` and popups
///
/// `on_resume()` is `Slicer::focusRegained` (`origin/main:src/deluge/gui/ui/slicer.cpp`), because `closeUI` calls
/// `focusRegained()` on whatever it leaves on top (`gui/ui/ui.cpp`). A legacy popup is a display overlay, not a
/// UI, while a shell popup is a `SceneKind::Popup` scene; `SceneStack` fires `on_resume()` only when `input_top()`
/// changes hands, and an `input_transparent()` overlay never holds it, so a popup the scene raises coming and going
/// does not reset its slice table (`ui/shell/scene_stack.h`'s contract table). Both directions are asserted in
/// `tests/integration/ui/scenes/slicer_scene_spec.cpp`.
///
/// @par Two legacy hazards
///
/// Legacy's `closeUI` quizzes `Slicer::renderMainPads` with the no-argument form (`gui/ui/ui.cpp`), which `UI`'s
/// header requires every override to survive (`UI::renderMainPads`); `renderMainPads` returns early on `!image`. The
/// port has no such call: `render_grid` is always handed a real frame.
///
/// **The MANUAL slice mark can land in the sidebar, and the port clips it rather than reproducing it.**
/// `x = startPos / lengthInSamples * 16` is 16 once a slice has been clamped to the end of the sample, and column 16
/// is the first sidebar column. Legacy composes the grid top-down, so the sidebar beneath overwrites that pad in the
/// same pass; the shell composes bottom-up (`Shell::render_grid`), so the write would leave a stray pad. The marks
/// are painted inside the main-column region, which clips it.
///
/// @par Why the directory and namespace are `slicing/` and not `slicer/`
///
/// `scenes-no-root-ui` (`scripts/check_layer_rules.py`'s `ROOT_UI_GLOBALS`) lists the legacy global `slicer` as a
/// whole word `src/ui/scenes/**` may never spell, and `#include "ui/scenes/slicer/slicer_scene.h"` contains it.
/// `SlicerScene`, `kSlicerSceneKind` and `slicer_scene()` are unaffected: `\bslicer\b` does not match a name that
/// continues into another word character.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on
///          `Action` in `ui/shell/scene.h`.

#include "ui/scenes/bind/ports/ports.h"
#include "ui/shell/scene.h"
#include "ui/widgets/grid/frame.h"
#include "ui/widgets/oled/frame.h"

#include "ui/gfx/grid/pad_grid.h"
#include "ui/scenes/services/peak_retry.h"
#include "ui/scenes/services/waveform_paint.h"
#include "ui/views/grid/playhead.h"
#include "ui/views/grid/slicer.h"
#include "ui/views/oled/slicer_count.h"
#include "ui/views/waveform/view.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace deluge::ui::scenes::slicing {

/// @brief Legacy's `SLICER_MODE_REGION` / `SLICER_MODE_MANUAL` (`origin/main:src/deluge/gui/ui/slicer.h`), as an
///        enum rather than two `#define`s.
///
/// The values are legacy's, because `Slicer::buttonAction`'s `X_ENC` arm advances the mode with
/// `slicerMode++; slicerMode %= 2` (`origin/main:src/deluge/gui/ui/slicer.cpp`).
enum class Mode : uint8_t {
	/// Cut the sample into `num_clips()` equal regions. The OLED shows that count; the pads show
	/// the whole waveform.
	Region = 0,
	/// Place up to `kMaxManualSlices` slice points by hand. The OLED shows how many are placed; the
	/// pads show the waveform squashed into the top half and a 64-slot grid in the bottom half.
	Manual = 1,
};

/// @brief Legacy's `MAX_MANUAL_SLICES` (`origin/main:src/deluge/gui/ui/slicer.h`).
inline constexpr int32_t kMaxManualSlices = 64;

/// @brief One manual slice: legacy's `SliceItem` (`origin/main:src/deluge/gui/ui/slicer.h`).
struct SlicePoint {
	/// Sample-frame position where this slice starts.
	int32_t start_pos = 0;
	/// Semitones to transpose the slice by, `-24 .. 24` (`Slicer::verticalEncoderAction` in
	/// `origin/main:src/deluge/gui/ui/slicer.cpp`).
	int32_t transpose = 0;
	bool operator==(const SlicePoint&) const = default;
};

/// @brief One pad-hold/tap audition request, snapshotted at trigger time.
///
/// The arguments of main's `Slicer::preview` (`origin/main:src/deluge/gui/ui/slicer.cpp`), field for
/// field, held as plain values so nothing points into `manualSlicePoints[]` or any other live state once the
/// request has been queued.
struct PreviewRequest {
	/// Sample-frame position where the audition slice starts.
	int64_t start_point = 0;
	/// Sample-frame position where it ends; -1 leaves the current end alone (`Slicer::preview` in
	/// `origin/main:src/deluge/gui/ui/slicer.cpp`).
	int64_t end_point = 0;
	/// Semitones.
	int32_t transpose = 0;
	/// Legacy's `on`, which is `padAction`'s **velocity** and not a bool (`Slicer::padAction`
	/// passes its third parameter straight through to `Slicer::preview`); zero silences.
	int32_t on = 0;
	bool operator==(const PreviewRequest&) const = default;
};

/// @brief The commit gesture, snapshotted at Select.
///
/// Plain data: the slice runs when its load lands, and an encoder turn or pad press while it is out
/// must not change what gets sliced or painted.
struct SliceCommit {
	/// Was the slicer in MANUAL mode at press time (`Slicer::buttonAction`'s Select arm,
	/// `origin/main:src/deluge/gui/ui/slicer.cpp`)?
	bool is_manual = false;
	/// `numManualSlice` (MANUAL) or `numClips` (REGION) at press time (the same arm).
	int32_t slice_count = 0;
	/// The slice table as it stood at press time (the same arm); only read when
	/// `is_manual`.
	SlicePoint points[kMaxManualSlices]{};
};

/// @brief Everything `Slicer` does to the audio engine, the model and the card -- the calls
///        `src/ui/scenes/**` may not spell.
///
/// **This is a boundary, not a new feature.** Together the virtuals are what `Slicer::padAction`,
/// `Slicer::buttonAction` and `Slicer::graphicsRoutine` do that reach `sampleBrowser`, `soundEditor`,
/// `instrumentClipView` or `PadLEDs`. On the device the host's `SliceCore` does them
/// (`gui/ui/shell_slice_ops.cpp`), as it does for the legacy `Slicer`. The scene keeps every part of
/// those functions that is state, arithmetic or a guard, which is the part an oracle can compare
/// against legacy.
///
/// The same split `scenes::browser::Listing` makes for the card, and for the same reason: one named place that
/// knows how to reach the thing, rather than a hole in the layer rule.
///
/// @warning **A null `SlicerScene::ops()` is not an error and not a stub.** The application wires the host's ops at
///          boot (`gui::ui::wireShellSliceOps`); a spec starts unwired. With no ops the scene still does all of its
///          own bookkeeping -- the slice table and the modes -- and performs no audio or card work and begins no
///          commit. It is not a licence to leave a call out: a call this interface does not carry is a call the
///          port has dropped.
class SliceOps {
public:
	virtual ~SliceOps() = default;

	/// @brief `AudioEngine::stopAnyPreviewing`: silence the browser's preview (X_ENC into MANUAL).
	virtual void stop_previewing() {}

	/// @brief Silence the kit's first drum (`firstDrum->killAllVoices`).
	virtual void kill_voices() {}

	/// @brief Press or release one audition through the first drum; a press whose sample is not loaded
	///        sounds once it is.
	/// @param req The audition, snapshotted.
	virtual void audition(const PreviewRequest& req) { (void)req; }

	/// @brief Forget a held audition, so its load sounds nothing when it lands (enter, X_ENC, Select, BACK).
	virtual void cancel_audition() {}

	/// @brief The slicer is closing: BACK, the mute column's exit, or a landed slice. The host lets the file go, so
	///        the waveforms port's SLICE address names nothing until the next open; the scene keeps its file and view,
	///        which a reopen on the same file draws again.
	virtual void closed() {}

	/// @return Where the first drum's newest voice on the sliced file is playing, in samples, if one is.
	virtual std::optional<int32_t> play_position() { return std::nullopt; }

	/// @brief Give the first drum its whole file back, untransposed (BACK out of MANUAL); a drum that holds no
	///        file is left as it is.
	virtual void restore_full_range() {}

	/// @brief Start slicing the file with @p commit. The host ends it with `SlicerScene::commit_done`.
	/// @param commit The gesture, snapshotted at Select.
	/// @return False when the host refused, and nothing is out.
	virtual bool begin_commit(const SliceCommit& commit) {
		(void)commit;
		return false;
	}
};

/// @brief The file the slicer opens on: which waveform it is, and its path, which a reopen compares.
struct SliceFile {
	uint64_t identity{0}; ///< the waveform's identity, as `bind::WaveformInfo::identity` gives it
	std::string path;     ///< the file's path
	bool operator==(const SliceFile&) const = default;
};

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 8` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kSlicerSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 8);

/// @brief The slicer. One instance for the whole application (`registry.h`).
class SlicerScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at the file the browser previewed.
	///
	/// The entry point for whoever opens the slicer, split from `on_enter` for the reason `MarkerScene::open` is.
	/// Legacy has no equivalent line inside `Slicer` at all: it reads the **`waveformBasicNavigator` global**, which
	/// the sample browser's preview path set up and scanned (`SampleBrowser::previewIfPossible`,
	/// `origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`). The scene draws whatever the waveforms port's SLICE
	/// address answers, which is the file the host opened it on. With @p shown the scene takes the preview's view, and
	/// the port answers the columns the preview read, since it keeps them by file. Without it the scene opens zoomed
	/// out over the length SLICE answers; called again on the file it already shows, it keeps its view, as nothing
	/// resets legacy's global between two slicer opens.
	/// @param file  The file to slice; none leaves the scene with nothing to draw.
	/// @param shown The view the browser's preview drew @p file at, or none.
	void open(std::optional<SliceFile> file, std::optional<views::waveform::ShownWaveform> shown = std::nullopt);

	/// @brief Switch between REGION and MANUAL.
	///
	/// The state half of `Slicer::buttonAction`'s `X_ENC` arm (`origin/main:src/deluge/gui/ui/slicer.cpp`), for specs
	/// that set a mode without the button; the button's arm also silences the preview and the first drum
	/// (`button_action`).
	/// @param mode The mode to switch to.
	void set_mode(Mode mode);

	/// @brief Wire the audio/model/card boundary.
	///
	/// @param ops The implementation, or null to run with the model side unwired. Borrowed, not
	///            owned; it must outlive the scene's use of it.
	void set_ops(SliceOps* ops) { ops_ = ops; }

	/// @brief Install the ports the scene reads: the app installs its own at boot
	///        (`gui::shell_host::install_scene_ports`), a spec the app's or a copy with fakes in it.
	///
	/// With none, or a null port, the scene reads nothing auditioned, the SLOW play cursor and no waveform, and
	/// wipes no undo log.
	/// @param ports The set, or null. Borrowed, not owned; it must outlive the scene's use of it.
	void set_ports(const bind::Ports* ports) { ports_ = ports; }

	/// @brief The host's answer to `SliceOps::begin_commit`, once the slice has ended.
	///
	/// A landed slice removes the slicer on the next `after_render` (the host closes the menu and the browser beneath,
	/// as main's `exitAndNeverDeleteDrum` closes all three); a failed one leaves it open and taking input again.
	/// Ignored when no commit is out.
	/// @param success Whether the drums were made.
	void commit_done(bool success);
	/// @return The wired boundary, or null. See `SliceOps`.
	SliceOps* ops() const { return ops_; }

	/// @brief `Slicer::padAction`'s "do slice" arithmetic, with the
	///        play position handed in rather than walked for.
	///
	/// Public because the walk that produces @p sample_pos lives behind `SliceOps::play_position`
	/// and is not ported (see its docstring); this half is, and is compared against legacy's own
	/// loop in the spec.
	/// @param sample_pos Where the previewing drum is playing, in sample frames.
	/// @return True if a slice was placed -- legacy's `samplePos < lengthInSamples &&
	///         numManualSlice < MAX_MANUAL_SLICES`.
	bool add_slice_at(int32_t sample_pos);

	deluge::ui::shell::SceneKind kind() const override { return kSlicerSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	/// @name What the surfaces show (`slicing/present.cpp`)
	/// @{
	/// @brief Where the screen's count is formatted: legacy's own 12-byte `intToString` buffer.
	struct SlicerCountText {
		char digits[12];
	};
	/// @brief The screen: the heading and the slice count of the current mode.
	/// @param text Where the count is formatted; the result points into it.
	[[nodiscard]] deluge::ui::views::oled::SlicerCount present_count(SlicerCountText& text) const;
	/// @brief The waveform's columns from @p wf over the 16 main columns.
	[[nodiscard]] std::array<deluge::ui::widgets::grid::PeakCol, views::waveform::kColumns>
	present_waveform(const services::WaveformFrame& wf) const;
	/// @brief The placed slices' columns, with the mode.
	[[nodiscard]] deluge::ui::views::grid::SliceMarks present_marks() const;
	/// @brief The playhead on rows 7 to 4, as the device's flash cursor shows it.
	/// @param pos Where the newest voice plays (`SliceOps::play_position`, read once by the frame); none when nothing
	///            plays.
	[[nodiscard]] deluge::ui::views::grid::Playhead present_playhead(std::optional<int32_t> pos) const;
	/// @}
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	/// @brief `Slicer::focusRegained` again, as `on_enter` is -- see "`on_resume()` and popups" in the file
	///        comment.
	void on_resume() override;
	deluge::ui::shell::Result after_render() override;
	/// @brief Now while a landed commit waits for `after_render`; `kPlayheadMs` while something plays, for the
	///        playhead (`Slicer::graphicsRoutine` runs every pass of the main loop); nothing otherwise.
	std::optional<uint32_t> next_timer() const override;
	/// @brief The grid only: the playhead is all the timer moves.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

	/// @brief False: a plain `UI`, not a `RootUI`. See the table in the class docstring.
	bool is_root() const override { return false; }
	/// @brief True: the waveform covers the main pads, and the sidebar beneath keeps showing.
	bool is_grid_overlay() const override { return true; }
	/// @brief `Main`: the waveform is drawn on the main pads, and the sidebar is the view
	///        beneath's (`Slicer` overrides `renderMainPads` but not `renderSidebar`).
	deluge::ui::shell::PadRegions pad_regions() const override { return deluge::ui::shell::PadRegions::Main; }
	/// @brief False: `Slicer` overrides `renderMainPads` but not `renderSidebar`, so the sidebar
	///        underneath still needs to paint (and, through `Shell::tick_stack`, still needs its
	///        clock advanced).
	bool occludes_grid() const override { return false; }
	/// @brief REGION mode leaves the audition column to the view beneath, as the sample browser it hands its pads to
	///        does (`SampleBrowser::padAction`); MANUAL deals with every pad (`Slicer::padAction`).
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return mode_ == Mode::Region ? deluge::ui::shell::GridPadClaim::AllButAudition
		                             : deluge::ui::shell::GridPadClaim::All;
	}
	/// @brief True: `Slicer()` sets `oledShowsUIUnderneath = true` (`origin/main:src/deluge/gui/ui/slicer.h`), so
	///        `doAnyPendingOLEDRendering` (`gui/ui/ui.cpp`) walks past it to the UI below
	///        and this page composites over what that drew.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return true; }

	/// @name Observable state
	/// Legacy's public members (`origin/main:src/deluge/gui/ui/slicer.h`), for specs.
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
	int64_t x_scroll() const { return view_.scroll(); }
	/// @return Samples per waveform column.
	int64_t x_zoom() const { return view_.zoom(); }
	/// @return Is a slice commit out? Legacy's `currentUIMode == UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES` gate,
	///         which refuses every button and pad until the host ends the commit (`commit_done`).
	bool committing() const { return committing_; }
	/// @return Whether `open` handed the scene a file to slice.
	bool has_sample() const { return file_.has_value(); }
	/// @return The file the scene was last opened on, or none.
	[[nodiscard]] const std::optional<SliceFile>& file() const { return file_; }
	/// @}

	/// @brief How often the playhead is redrawn while something plays.
	static constexpr uint32_t kPlayheadMs = 15;

private:
	/// @brief `Slicer::horizontalEncoderAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`).
	/// @param offset Signed detents.
	/// @param shift  Is shift held? Legacy's `Buttons::isShiftButtonPressed()`, which
	///               makes one detent 10 samples instead of 100.
	/// @return Always handled, in both modes, as legacy always returns `DEALT_WITH`.
	deluge::ui::shell::Result horizontal_encoder(int32_t offset, bool shift);
	/// @brief `Slicer::verticalEncoderAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`).
	/// @param offset Signed detents.
	/// @return Always handled, in both modes.
	deluge::ui::shell::Result vertical_encoder(int32_t offset);
	/// @brief `Slicer::selectEncoderAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`).
	/// @param offset Signed detents.
	/// @return Handled, with the repaint legacy files as `uiNeedsRendering` + `renderUIsForOled`.
	deluge::ui::shell::Result select_encoder(int32_t offset);

	/// @brief `Slicer::padAction`.
	/// @param x        Absolute pad column.
	/// @param y        Absolute pad row.
	/// @param velocity Legacy's third parameter, which it names `on`; zero is a release.
	/// @return Handled in MANUAL mode; unhandled in REGION, where legacy delegates to
	///         `sampleBrowser.padAction` -- see the note at the definition.
	deluge::ui::shell::Result pad_action(int32_t x, int32_t y, int32_t velocity);

	/// @brief `Slicer::buttonAction` (`origin/main:src/deluge/gui/ui/slicer.cpp`), press edges only.
	/// @param b   Which button.
	/// @param ctx The world around the press -- the held-pad set the SAVE arm asks about.
	/// @return What the shell should do.
	deluge::ui::shell::Result button_action(deluge::ui::shell::ButtonId b, const deluge::ui::shell::UiContext& ctx);

	/// @brief `buttonAction`'s SAVE arm (`origin/main:src/deluge/gui/ui/slicer.cpp`), past its held-pad test.
	void delete_current_slice();

	/// @brief `Slicer::focusRegained`'s reset: one slice, slot 0, REGION, the table cleared.
	void reset_state();

	/// @brief `Slicer::renderMainPads`: the waveform, and in MANUAL the slice marks and slot grid.
	/// @param f The grid frame.
	void render_pads(deluge::ui::widgets::grid::GridFrame& f);

	/// @brief `Slicer::preview`: hand the snapshotted audition to the host, which holds its load.
	/// @param req The audition request.
	void preview(const PreviewRequest& req);

	/// The file the scene slices, and `view_` was opened on: a reopen keeps the view only for the same file. None
	/// before an open.
	std::optional<SliceFile> file_{};
	/// The scroll and zoom, over the file's length, which is fixed while the file is loaded. Main shares one navigator
	/// (`waveformBasicNavigator`) between the browser, the marker editor and the slicer; each scene here owns its
	/// view, and the waveforms port keeps the columns by file.
	views::waveform::WaveformView view_{};
	/// The redraws of a frame whose read left columns pending.
	services::PeakRetry retry_{};

	/// `int16_t`, as legacy declares it (`Slicer::numClips` in `origin/main:src/deluge/gui/ui/slicer.h`) -- the wrap in
	/// `selectEncoderAction` is written against exactly 257 and 1, so the width is part of the behaviour.
	int16_t num_clips_ = 16;
	int32_t num_manual_slices_ = 1;
	int32_t current_slice_ = 0;
	Mode mode_ = Mode::Region;
	SlicePoint points_[kMaxManualSlices]{};

	/// The audio/model/card boundary; null until an application wires one. See `SliceOps`.
	SliceOps* ops_ = nullptr;

	/// The ports `set_ports` installed, or null.
	const bind::Ports* ports_ = nullptr;
	/// @return The waveforms port the scene draws through, or null with none.
	[[nodiscard]] const bind::Waveforms* waveforms() const;
	/// @return What the waveforms port answers at the SLICE address, or none with no port.
	[[nodiscard]] std::optional<bind::WaveformInfo> slice_info() const;

	/// The gesture the commit out was begun with, snapshotted at Select.
	SliceCommit pending_commit_{};

	/// Legacy's `currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES` gate, scoped to this scene: set while the
	/// host's slice is out, until `commit_done`.
	bool committing_ = false;
	/// A landed commit, waiting for `after_render` to remove the slicer.
	bool landed_ = false;
	/// A voice played the sample at the last grid render, so the frame that clears its playhead is asked for.
	bool playing_ = false;
	/// Each row's tick square at the last render: legacy's `slowFlashSquares`, which the fast cursor flashes against.
	std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> last_ticks_{};
	/// The screen's count for the pass that draws it.
	SlicerCountText count_text_{};
};

/// @brief The application's one slicer.
///
/// A function-local static, for the reason every other scene singleton in `src/ui/scenes` is one.
/// @return The singleton.
SlicerScene& slicer_scene();

} // namespace deluge::ui::scenes::slicing
