#pragma once

/// @brief `SlicerScene` -- the slicer: a waveform on the pads, a slice count on the OLED, and (in
///        MANUAL mode) a 64-slot slice grid under the waveform.
///
/// The port of `Slicer` (`gui/ui/slicer.{h,cpp}`), which `SampleBrowser` and the sample browser's
/// kit context menu open once a sample has been chosen. Task 9 covered the state, the two renders
/// and the three encoder arms; Task 10 added `padAction`, the rest of `buttonAction`, the audition
/// preview and the slice commit.
///
/// @par Everything this scene does to the audio, the model and the card goes through `SliceOps`
///
/// `Slicer` reaches its work through four `gui/` globals -- `sampleBrowser`, `soundEditor`,
/// `instrumentClipView` and `PadLEDs` -- every one of which `src/scenes/**` may never name
/// (`scripts/check_layer_rules.py`, `scenes-no-root-ui` and `scenes-includes-gui`). The six calls
/// that actually matter are collected into `SliceOps` below, one virtual per legacy call site, and
/// the scene keeps everything else: the state, the guards, the coalescer, the dispatch and the
/// re-entry gate. See `SliceOps` for the call-site-by-call-site mapping and for what a null `ops()`
/// means.
///
/// @par This is NOT a root scene
///
/// The plan's prose calls it one; it is not. `class Slicer final : public UI` (`gui/ui/slicer.h:33`)
/// -- a plain `UI`, opened with `openUI(&slicer)` from `gui/ui/browser/sample_browser.cpp:378` and
/// `gui/context_menu/sample_browser/kit.cpp:77`, and `grep "public RootUI"` finds exactly two
/// classes in the tree, `TimelineView` and `KeyboardScreen` (`shell/scene.h:475-478`, which already
/// records this). So `is_root()` is false, and `services::GlobalControls` correctly gates the
/// `View::buttonAction` group of legacy's global button arms off it: with the slicer up, those arms
/// are unreachable in legacy too, because `UI::buttonAction`'s base returns `NOT_DEALT_WITH`
/// without forwarding to `view` (`gui/ui/ui.h:98-100`).
///
/// It does not consume `scenes::timeline::TimelineState` either: legacy's slicer navigates a
/// *sample*, through `waveformBasicNavigator`, not a timeline.
///
/// @par The composition predicates -- five, not the four the brief named
///
///   * **`is_root()` -> false.** `class Slicer final : public UI` (`gui/ui/slicer.h:33`).
///   * **`oled_transparent(role) -> true`.** `Slicer()` sets `oledShowsUIUnderneath = true`
///     (`gui/ui/slicer.h:35`), and `renderOLED` then clears only its own window.
///   * **`is_grid_overlay() -> true`.** `renderMainPads` (`gui/ui/slicer.cpp:103-152`) returns true
///     unconditionally, so this is drawn over whatever root is beneath and is not the grid base.
///   * **`occludes_grid() -> false.`** `Slicer` overrides `renderMainPads` only. It has no
///     `renderSidebar`, and `UI::renderSidebar`'s base returns false (`gui/ui/ui.h:148-151`), so
///     `doAnyPendingGridRendering`'s top-down walk (`gui/ui/ui.cpp:305-349`) keeps going and the
///     sidebar beneath paints -- exactly `MarkerScene`'s situation. The predicate's own definition
///     is "covers the grid COMPLETELY, so scenes beneath need not render" (`shell/scene.h`), and its
///     one consumer is `Shell::tick_stack` (`shell/shell.cpp:591-605`), which stops advancing clocks
///     beneath a scene that claims it; claiming it here would stop the clock of the scene that owns
///     the sidebar.
///   * **`dims_grid() -> false`** -- the fifth, and the one that is easy to miss, because its
///     default is `is_grid_overlay()` and that is true here. Legacy never halves what is beneath the
///     slicer: `Slicer` has no `getGreyoutColsAndRows` either, so `getUIGreyoutColsAndRows`
///     (`gui/ui/ui.cpp:73-83`) walks past it to the browser below. Taking the default would halve a
///     sidebar legacy never greys.
///
/// @par The one legacy predicate that points the other way, and why it does not change the answer
///
/// `Slicer::canSeeViewUnderneath()` returns **false** (`gui/ui/slicer.h:38`) -- the only legacy
/// predicate on this class that says "do not run the layer beneath". It has two consumers in the
/// tree and neither is about grid composition:
///
///   * `UI::graphicsRoutine` (`gui/ui/ui.cpp:47-51`), which forwards the per-tick redraw to the root
///     UI only when it is true. Moot for `Slicer`, which overrides `graphicsRoutine` itself.
///   * `Session::armingChanged` (`playback/mode/session.cpp:1295`), which uses it to decide whether
///     an arming change should raise `Dirty::Oled` and flash the play LED. That is an OLED question
///     -- with the slicer's own page up, the session view's OLED content is not on screen -- and
///     false is right for it. It says nothing about which pads get painted.
///
/// Neither is `renderSidebar`, and `renderSidebar` is what the grid walk actually consults.
///
/// @par What draws what
///
/// Nothing here calls an `OledFrame` or `GridFrame` primitive (`check_layer_rules.py`,
/// `scenes-no-frame-primitives`), and nothing names `waveformRenderer` or `waveformBasicNavigator`
/// (`scenes-no-legacy-waveform`):
///
///   * `renderOLED`'s window, rule, label and centred count (`gui/ui/slicer.cpp:74-98`) ->
///     `ui::oled::count_window`.
///   * `renderMainPads`' two `waveformRenderer.renderFullScreen` calls (`:108`, `:114`) ->
///     `deluge::sample::find_peaks_per_col` + `ui::grid::waveform_full`.
///   * the MANUAL slice grid and slice marks (`:117-149`) -> `GridFrame::pad` through the region,
///     with the colours resolved here.
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
/// @par The commit does NOT need `RecorderScene`'s SD-routine guard, and here is the evidence
///
/// `RecorderScene::after_render` has to ask `AudioEngine::sdRoutineBusy()` before it finishes a
/// recording, because legacy states the hazard itself on `AudioRecorder::slowRoutine`
/// (`gui/ui/audio_recorder.cpp:168-176`): *the recorder may be suspended part-way through its own
/// `cardRoutine()`, and freeing it there leaves that `cardRoutine()` running on freed memory, then
/// frees it a second time.* Three facts, all read rather than assumed, say the slicer's commit is
/// not that shape:
///
///   1. **`cardRoutine()` exists on exactly one class in this tree.** `grep -rn cardRoutine
///      src/deluge` finds `SampleRecorder::cardRoutine` (`model/sample/sample_recorder.h:67`,
///      `sample_recorder.cpp:375`) and its one pump, `AudioEngine::doRecorderCardRoutines`
///      (`audio_engine.cpp:1682`) -- nothing else. The hazard is "free an object whose own
///      `cardRoutine()` is suspended", and `Slicer::doSlice` (`gui/ui/slicer.cpp:596-751`) frees no
///      `SampleRecorder`: its only `delugeDealloc` (`:703`) releases the `SoundDrum` it allocated
///      thirteen lines earlier at `:690`, on the `ramError2` path, and **I read the enclosing
///      braces** -- `kit->addDrum(newDrum)` is at `:711`, below both error labels, so nothing else
///      holds that pointer when it is freed.
///   2. **The commit is input-driven; the recorder's completion arm is not.** `RecorderScene` polls
///      from the shell tick with no input at all, which is why the deferral had to be its own. The
///      slicer commits only from `Slicer::buttonAction`'s SELECT_ENC arm, and legacy already refuses
///      that press from inside a card routine -- `if (inCardRoutine) return
///      ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;` (`slicer.cpp:335-337`), and the BACK arm
///      repeats it at `:372-374`. That is the **defer-and-retry** gate whose port is
///      `InputLowering::set_sd_busy` (`shell/input_lowering.h:318`), deliberately not an `Action`
///      and deliberately not a scene's business -- the same ruling `RecorderScene::handle`,
///      `MarkerScene` and `BrowserScene` all record.
///   3. **The hazard `doSlice` does have is a different one, and legacy names it itself.** The
///      comment at `slicer.cpp:353-362` says the op ends in `sampleBrowser.exitAndNeverDeleteDrum()`
///      -> `close()`, which pops UIs from inside the dispatched op, so a BACK or a second SELECT_ENC
///      arriving during that window would mutate the same UI-stack globals from two contexts (and a
///      second SELECT_ENC would double-slice). Its guard is the `currentUIMode` gate at `:363-368`,
///      not an SD test -- and *that* is the guard this port carries, as `committing()`.
///
/// So: no `sdRoutineBusy()` call here, and saying so is the finding. What is ported instead is
/// legacy's own pair -- the re-entry gate (`committing_`) and the dropped-dispatch recovery at
/// `:364-368` -- both of which are asserted in the spec, in both directions.
///
/// @par What this scene's own popups found in the shell
///
/// `on_resume()` is `Slicer::focusRegained` (`gui/ui/slicer.cpp:59-72`), the same body `on_enter`
/// runs, because `closeUI` calls `focusRegained()` on whatever it leaves on top
/// (`gui/ui/ui.cpp:194-199`). Task 10 measured that mapping firing where legacy never would: a
/// legacy popup is a display overlay, not a UI, while a shell popup is a `SceneKind::Popup` scene,
/// and `SceneStack::remove` used to notify the scene beneath whenever the scene it removed sat
/// highest. So a Y_ENC press raising a popup (`slicer.cpp:301`), or a slice-pad tap cancelling one
/// (`:503`), wiped forty hand-placed slices 800 ms later.
///
/// The body was empty for exactly one task while the shell was corrected. **It is not any more:**
/// `SceneStack` now fires `on_resume()` only when `input_top()` changes hands, and an
/// `input_transparent()` overlay never holds it, so a popup coming and going is not an edge
/// (`shell/scene_stack.h`'s contract table; design spec §15.7). Both directions are asserted in
/// `sim/scenes_spec/slicer_scene_spec.cpp`.
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
///     the same pass; the shell composes **bottom-up** (`Shell::render_grid`, `shell/shell.cpp:700-827`: base first,
///     then overlays), so reproducing the write here would leave the stray pad visible where legacy's is not. The port
///     therefore paints the slice marks inside the main-column region, which clips it. Behaviour
///     preserved by clipping, not by transcription -- the one place in this file where those differ.
///
/// @par Why the directory and namespace are `slicing/` and not `slicer/`
///
/// The plan asks for `src/scenes/slicer/slicer_scene.{h,cpp}`. The `scenes-no-root-ui` ratchet
/// (`scripts/check_layer_rules.py`'s `ROOT_UI_GLOBALS`) lists the legacy global `slicer`
/// (`gui/ui/slicer.cpp:55`) among the names `src/scenes/**` may never spell, as a whole-word
/// pattern -- and `#include "scenes/slicer/slicer_scene.h"` contains that whole word. **Measured,
/// with the path put back and the namespace left alone: two violations, in two files, one line
/// each** -- this file's own include and `registry.cpp`'s. `count_file` skips comment-only lines
/// (`check_layer_rules.py:849-853`), so the docstring mentions above never counted.
///
/// Two real include lines is not "the path is impossible"; it is "the row as written flags this
/// path, and narrowing it to skip `#include` would preserve its stated purpose exactly". That
/// narrowing is a change to a shared checker, so it went up as an open question for E4 task 14
/// rather than being made here, and the local fix stands: the directory and the namespace are
/// `slicing`, ratified by the controller, and the file names are the plan's. `SlicerScene`,
/// `kSlicerSceneKind` and `slicer_scene()` are all unaffected -- `\bslicer\b` does not match a name
/// that continues into another word character.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on
///          `Action` in `shell/scene.h`.

#include "shell/scene.h"
#include "ui/grid/frame.h"
#include "ui/oled/frame.h"

#include "definitions_cxx.hpp"
#include "model/sample/waveform_navigator.h"
#include "storage/latest_wins.h"

#include <cstdint>
#include <optional>

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

/// @brief One pad-hold/tap audition request, snapshotted at trigger time.
///
/// Legacy's `Slicer::PreviewTarget` (`gui/ui/slicer.h:93-98`), field for field, and for its reason:
/// all plain values, so nothing points into `manualSlicePoints[]` or any other live state once the
/// request has been queued.
struct PreviewRequest {
	/// Sample-frame position where the audition slice starts.
	int64_t start_point = 0;
	/// Sample-frame position where it ends; -1 leaves the current end alone (`gui/ui/slicer.h:57`).
	int64_t end_point = 0;
	/// Semitones.
	int32_t transpose = 0;
	/// Legacy's `on`, which is `padAction`'s **velocity** and not a bool (`slicer.cpp:494-499`
	/// passes the third parameter of `Slicer::padAction` straight through); zero silences.
	int32_t on = 0;
	bool operator==(const PreviewRequest&) const = default;
};

/// @brief The commit gesture, snapshotted at dispatch time.
///
/// Legacy's `Slicer::SliceCommitTarget` (`gui/ui/slicer.h:123-127`). Plain data for the reason its
/// docstring gives: the dispatched op may yield inside a load, and a concurrent encoder turn or pad
/// press during that yield must not be able to desync what gets sliced from what gets painted.
struct SliceCommit {
	/// Was the slicer in MANUAL mode at press time (`slicer.cpp:345`)?
	bool is_manual = false;
	/// `numManualSlice` (MANUAL) or `numClips` (REGION) at press time (`slicer.cpp:346`).
	int32_t slice_count = 0;
	/// The slice table as it stood at press time (`slicer.cpp:348-349`); only read when
	/// `is_manual`.
	SlicePoint points[kMaxManualSlices]{};
};

/// @brief Everything `Slicer` does to the audio engine, the model and the card -- the six calls
///        `src/scenes/**` may not spell.
///
/// **This is a boundary, not a new feature.** Each virtual stands for one legacy call site, named
/// below; together they are the complete set of things `Slicer::padAction`, `Slicer::buttonAction`,
/// `Slicer::previewForTarget` and `Slicer::commitSlice` do that reach `sampleBrowser`,
/// `soundEditor`, `instrumentClipView` or `PadLEDs` -- the four `gui/` globals
/// `scripts/check_layer_rules.py`'s `scenes-no-root-ui` / `scenes-includes-gui` rows forbid. The
/// scene keeps every part of those functions that is state, arithmetic or a guard, which is the
/// part an oracle can compare against legacy; the parts below are the part it cannot.
///
/// The same split `menu::model_stack_for()` makes for the model stack and
/// `scenes::browser::Listing` makes for the card, and for the same reason: one named place that
/// knows how to reach the thing, rather than a hole in the layer rule.
///
/// @warning **A null `SlicerScene::ops()` is not an error and not a stub.** Nothing links
///          `deluge_scenes` into an application yet (spec F's `RunOptions::second_registry` is what
///          will), so today every scene in this library runs with its model side unwired. With no
///          ops the scene still does all of its own bookkeeping -- the slice table, the modes, the
///          gate, the coalescer -- and simply performs no audio or card work, which is what a spec
///          wants and what an unwired application would want too. It is emphatically *not* a
///          licence to leave a call out: a call this interface does not carry is a call the port
///          has dropped.
class SliceOps {
public:
	virtual ~SliceOps() = default;

	/// @brief `AudioEngine::stopAnyPreviewing()` -- `buttonAction`'s X_ENC arm when it lands in
	///        MANUAL (`slicer.cpp:287-288`) and `doSlice`'s first statement (`:598`).
	virtual void stop_previewing() {}

	/// @brief `getCurrentKit()->firstDrum->killAllVoices()` -- the X_ENC arm (`slicer.cpp:291`), the
	///        SELECT_ENC arm's MANUAL branch (`:350`) and the BACK arm's MANUAL branch (`:380`).
	virtual void kill_voices() {}

	/// @brief `Slicer::previewForTarget` (`slicer.cpp:446-482`): load the sliced sample if it is not
	///        already resident, point the drum's range at @p req, and sound or silence the audition
	///        note.
	///
	/// Runs on the storage owner -- `SlicerScene` dispatches it through
	/// `deluge::storage::Owner::run`, exactly as `Slicer::preview` does (`:426`), so the resident
	/// check, the load it may trigger and the audition note stay together on one op rather than
	/// being split across a yield.
	/// @param req The audition request.
	virtual void audition(const PreviewRequest& req) { (void)req; }

	/// @brief The voice walk at the top of `Slicer::padAction`'s "do slice" branch
	///        (`slicer.cpp:508-534`): where is the previewing drum's sample playing right now?
	///
	/// Unported for `graphicsRoutine`'s reason and by the same ruling -- it needs `Sound::voices()`
	/// to report a live voice, which needs a running `AudioEngine`, which no spec in this tree can
	/// produce. The *arithmetic* it feeds is ported and measured
	/// (`SlicerScene::add_slice_at`); only the source of the number is behind this call.
	/// @return The play position in sample frames, or `nullopt` for legacy's `voiceSample ==
	///         nullptr` (`:533`), which places no slice at all.
	virtual std::optional<int32_t> play_position() { return std::nullopt; }

	/// @brief `buttonAction`'s BACK arm in MANUAL mode (`slicer.cpp:381-388`): put the previewing
	///        drum's range back to the whole sample, untransposed, on the way out.
	virtual void restore_full_range() {}

	/// @brief `Slicer::commitSlice` (`slicer.cpp:575-594`) minus its gate release: `doSlice`
	///        (`:596-751`) and, for a MANUAL commit, the per-drum start/end/transpose paint loop
	///        (`:578-589`).
	///
	/// Runs on the storage owner (`Owner::run_or_inline`, `slicer.cpp:364`). The gate release at
	/// `:593` is the scene's, not this call's, because every exit path of `doSlice` -- including its
	/// internal `getOut:` load-failure branch -- has to reach it.
	/// @param commit The dispatch-time snapshot.
	virtual void commit(const SliceCommit& commit) { (void)commit; }
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

	/// @brief Wire the audio/model/card boundary.
	///
	/// @param ops The implementation, or null to run with the model side unwired. Borrowed, not
	///            owned; it must outlive the scene's use of it.
	void set_ops(SliceOps* ops) { ops_ = ops; }
	/// @return The wired boundary, or null. See `SliceOps`.
	SliceOps* ops() const { return ops_; }

	/// @brief `Slicer::padAction`'s "do slice" arithmetic (`gui/ui/slicer.cpp:535-551`), with the
	///        play position handed in rather than walked for.
	///
	/// Public because the walk that produces @p sample_pos lives behind `SliceOps::play_position`
	/// and is not ported (see its docstring); this half is, and is compared against legacy's own
	/// loop in the spec.
	/// @param sample_pos Where the previewing drum is playing, in sample frames.
	/// @return True if a slice was placed -- legacy's `samplePos < lengthInSamples &&
	///         numManualSlice < MAX_MANUAL_SLICES` (`:535`).
	bool add_slice_at(int32_t sample_pos);

	deluge::ui::shell::SceneKind kind() const override { return kSlicerSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	/// @brief `Slicer::focusRegained` again, as `on_enter` is -- see the definition, and "What this
	///        scene's own popups found in the shell" in the class docstring.
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
	/// @return Is a slice commit in flight? Legacy's `currentUIMode ==
	///         UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES` gate (`slicer.cpp:363`), which makes
	///         `buttonAction` a no-op until the op releases it (`:279-281`, `:593`).
	bool committing() const { return committing_; }
	/// @return Is an audition op in flight? `previewCoalescer_.in_flight()` (`slicer.h:111`).
	bool previewing() const { return preview_coalescer_.in_flight(); }
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

	/// @brief `Slicer::padAction` (`slicer.cpp:484-568`).
	/// @param x        Absolute pad column.
	/// @param y        Absolute pad row.
	/// @param velocity Legacy's third parameter, which it names `on`; zero is a release.
	/// @return Handled in MANUAL mode (`:563-565`); unhandled in REGION, where legacy delegates to
	///         `sampleBrowser.padAction` (`:567`) -- see the note at the definition.
	deluge::ui::shell::Result pad_action(int32_t x, int32_t y, int32_t velocity);

	/// @brief `Slicer::buttonAction` (`slicer.cpp:276-398`), press edges only.
	/// @param b   Which button.
	/// @param ctx The world around the press -- the held-pad set the SAVE arm asks about.
	/// @return What the shell should do.
	deluge::ui::shell::Result button_action(deluge::ui::shell::ButtonId b, const deluge::ui::shell::UiContext& ctx);

	/// @brief `buttonAction`'s SAVE arm (`slicer.cpp:306-332`), past its held-pad test.
	void delete_current_slice();

	/// @brief `Slicer::preview` (`slicer.cpp:409-433`): snapshot, coalesce latest-wins, dispatch.
	/// @param req The audition request.
	void preview(const PreviewRequest& req);
	/// @brief `Slicer::runPreviewOp` (`slicer.cpp:435-444`), as an `Owner::run` thunk.
	/// @param self The scene.
	static void run_preview_op(void* self);
	/// @brief `Slicer::runDoSliceOp` (`slicer.cpp:570-573`), as an `Owner::run_or_inline` thunk.
	/// @param self The scene.
	static void run_do_slice_op(void* self);
	/// @brief `Slicer::commitSlice` (`slicer.cpp:575-594`): the op, and the gate release.
	void commit_slice();

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

	/// The audio/model/card boundary; null until an application wires one. See `SliceOps`.
	SliceOps* ops_ = nullptr;

	/// `Slicer::previewCoalescer_` (`gui/ui/slicer.h:111`), the same type legacy uses.
	deluge::storage::LatestWins<PreviewRequest> preview_coalescer_{};

	/// `Slicer::pendingSliceTarget_` (`gui/ui/slicer.h:145`).
	SliceCommit pending_commit_{};

	/// Legacy's `currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES` gate (`slicer.cpp:363`),
	/// scoped to this scene. See "The commit does NOT need `RecorderScene`'s SD-routine guard" in
	/// the class docstring for what it is for and what it is not.
	bool committing_ = false;
};

/// @brief The application's one slicer.
///
/// A function-local static, for the reason every other scene singleton in this tree is one.
/// @return The singleton.
SlicerScene& slicer_scene();

} // namespace deluge::scenes::slicing
