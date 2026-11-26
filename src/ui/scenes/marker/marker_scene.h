#pragma once

/// @brief `MarkerScene` -- the sample marker editor: a waveform on the pads, four markers over it,
///        and the position of the selected one on the OLED.
///
/// The port of `SampleMarkerEditor` (`origin/main:src/deluge/gui/ui/sample_marker_editor.h`), which the sample menu's
/// start/end items and the audio clip's waveform item open. `SceneId::arg` is the `project::SampleMarker` to start on
/// -- `project::SampleMarker::START` or `project::SampleMarker::END`, which are the only two legacy ever opens with
/// (`origin/main:src/deluge/gui/menu_item/sample/start.h`, `origin/main:src/deluge/gui/menu_item/sample/end.h`,
/// `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.h`); the loop markers are reached from
/// *inside*, by holding a marker and tapping another column.
///
/// **What draws what.** Nothing here calls an `OledFrame` or `GridFrame` primitive
/// (`check_layer_rules.py`, `scenes-no-frame-primitives`):
///
///   * `renderMainPads`' `waveformRenderer.renderFullScreen` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`)
///     -> `services::read_waveform` then `services::paint_columns` (the read through the waveform's address, then
///     `ui::widgets::grid::waveform_full`).
///   * `renderColumn` / `renderMarkerInCol` -> `ui::widgets::grid::marker_column` /
///     `marker_tint`.
///   * `renderOLED`'s `drawScreenTitle` -> `ui::widgets::oled::title`.
///   * its lock icon + `invertArea` -> `ui::widgets::oled::title_badge`.
///   * its time and sample-count lines -> `ui::widgets::oled::sample_position`.
///
/// The read and the navigation are not widgets: the read goes through the context's waveforms port
/// (`ui/scenes/bind/ports/waveforms.h`), which answers each column as a peak, pending or past the end, and the
/// navigation is `views::waveform::WaveformView` (`ui/views/waveform/view.h`). The scene keeps no columns: a frame with
/// pending ones asks for another (`services::PeakRetry`).
///
/// @par Where the markers come from
///
/// The scene holds addresses, not engine objects: a sound's markers are its range's zone in the
/// source's record (`SoundFacts::sound_state`), an audio clip's are rows of the clip's record.
/// A move goes to a port: a sound's to the sound port as its source's ranges with the marker
/// named, an audio clip's to the clip port as a marker request. The view (scroll, zoom) and the loop lock are the
/// scene's own, kept in the view-state store (`ui/scenes/services/view_state.h`) under the range's identity or the
/// audio clip's id (`services::ViewKey`), so they stay with that range or clip as main keeps them on its sample holder.
///
/// @par The waveform is on the pads only
///
/// `SampleMarkerEditor::renderOLED` draws **no waveform at all** -- a title, an optional lock badge,
/// a duration and a sample count. So there is no `src/ui/widgets/oled/` waveform component:
/// `ui::widgets::oled::sample_position` is the OLED half, and `ui::widgets::grid::waveform_full`
/// plus `ui::widgets::grid::markers` are the pad half.
///
/// @par What is deliberately not ported
///
///   * **The collapse/explode animation.** `WaveformRenderer::renderOneColForCollapseAnimation` and
///     `...ZoomedOut` have exactly one caller each: `PadLEDs::renderAudioClipCollapseAnimation`
///     (`hid/led/pad_leds.cpp`) and `PadLEDs::renderAudioClipExplodeAnimation`, driven
///     by `sessionView`/`arrangerView` setting `collapseAnimationToWhichRow`
///     (`gui/views/session_view.cpp`, several sites). **`SampleMarkerEditor` never reaches
///     either**, so this is not this scene's to port -- not a judgement call, a call-graph fact.
///
/// @par The zoom slide and the playhead
///
/// A zoom slides from what the pads showed to the new view's bare waveform over `ui::widgets::grid::kZoomSpeed`, as
/// `WaveformBasicNavigator::zoom` hands `PadLEDs::renderZoom` the two images (`ui::widgets::grid::ZoomTransition`);
/// every input is refused while it slides, as legacy's `UI_MODE_HORIZONTAL_ZOOM` refuses it. The playhead is
/// `SampleMarkerEditor::graphicsRoutine`'s: the column the newest voice plays, on every row, painted with the flash
/// cursor Slow and flashed by the PIC with it Fast.
///
/// @par The blink, and a disagreement inside legacy
///
/// Legacy runs a four-phase blink on `TimerName::UI_SPECIFIC` at `kSampleMarkerBlinkTime`
/// and **renders it two different ways**. `SampleMarkerEditor::timerCallback` redraws only
/// the affected columns: phase 1 paints the selected column as one solid marker bar, phase 3
/// suppresses the selected marker (and, when the loop is locked, its partner), and phases 0 and 2
/// draw normally. `renderMainPads` -- which runs on a *full* redraw, not on the timer --
/// suppresses whenever `blinkPhase >= 1`, so a full redraw landing on phase 2 hides the marker
/// where the timer would have shown it.
///
/// **This scene follows `timerCallback`**, because that is the path that actually drives the blink
/// the user sees; `renderMainPads`' phase-2 arm only shows on the rare tick where something else
/// forced a whole-screen redraw. Recorded rather than reproduced: reproducing it would mean making
/// the port's render depend on *why* it was called, which the shell does not tell it.
///
/// @warning **This scene must not raise `Action::Kind::LockInput`**, and does not. See the warning on `Action` in
///          `ui/shell/scene.h`: it is one bare global bool, it *drops* events rather than deferring them, and
///          `on_exit()` cannot give it back. Legacy's own gate here is `sdRoutineLock` ->
///          `REMIND_ME_OUTSIDE_CARD_ROUTINE`
///          (`SampleMarkerEditor::padAction`, `SampleMarkerEditor::horizontalEncoderAction`), which defers and retries;
///          its port is `InputLowering::set_sd_busy`, which is not an `Action` and is not this scene's to file.

#include "definitions_cxx.hpp" // kSampleMarkerBlinkTime
#include "project/sample_marker.h"
#include "ui/scenes/bind/ports/waveforms.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/services/peak_retry.h"
#include "ui/scenes/services/view_state.h"
#include "ui/shell/scene.h"
#include "ui/views/waveform/view.h"
#include "ui/widgets/core/response.h"
#include "ui/widgets/grid/effects.h"
#include "ui/widgets/grid/markers.h"
#include "ui/widgets/grid/waveform.h"
#include "ui/widgets/grid/zoom_transition.h"

#include "gui/l10n/strings.h"
#include "project/device_settings.h"
#include "ui/gfx/grid/pad_grid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace deluge::synth {
struct SourceRange;
} // namespace deluge::synth

namespace deluge::instruments::classic {
struct SoundState;
enum class SourceId : uint8_t;
} // namespace deluge::instruments::classic

namespace deluge::ui::scenes::marker {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 4` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kMarkerSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 4);

/// @brief How long one blink phase lasts. Legacy's `kSampleMarkerBlinkTime` (`definitions_cxx.hpp`).
inline constexpr uint32_t kBlinkPhaseMs = static_cast<uint32_t>(kSampleMarkerBlinkTime);

/// @brief The four marker colours, in `project::SampleMarker` order.
///
/// `SampleMarkerEditor::renderMarkerInCol`'s switch (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`), lifted
/// out because `ui::widgets::grid::marker_column` takes colours rather than markers -- the widgets include only
/// `ui/**`, so they cannot know what a `project::SampleMarker` is.
/// @return A span of `project::kSampleMarkerCount` colours: green, cyan, magenta, red.
std::span<const RGB> marker_colours();

/// @brief The marker a reversed sample's marker plays as: its start is the sample's end, and its loop runs the other
///        way.
/// @param marker A marker of the sample as stored.
/// @return The marker in its place when the sample plays reversed.
[[nodiscard]] constexpr project::SampleMarker mirrored(project::SampleMarker marker) {
	switch (marker) {
	case project::SampleMarker::START:
		return project::SampleMarker::END;
	case project::SampleMarker::LOOP_START:
		return project::SampleMarker::LOOP_END;
	case project::SampleMarker::LOOP_END:
		return project::SampleMarker::LOOP_START;
	case project::SampleMarker::END:
		return project::SampleMarker::START;
	}
	return marker;
}

/// @brief The sample marker editor. One instance for the whole application (`registry.h`).
class MarkerScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at what @p ctx resolved.
	///
	/// The entry point for whoever opens the editor, exactly as `RenameScene::open` and
	/// `BrowserScene::open` are: `SceneId` carries one `uint16_t` and a context is not one, so the
	/// context arrives here and `SceneId::arg` (the `project::SampleMarker`) arrives at `on_enter`.
	/// @param ctx What the markers are read from and written to.
	void open(const menu::MenuContext& ctx);

	/// @brief May the editor open on @p ctx at all?
	///
	/// The survivable spelling of `SampleMarkerEditor::opened`'s two refusals: no file loaded where
	/// the context points (which pops up `STRING_FOR_NO_SAMPLE` and returns false) and --
	/// an addition, because this layer resolves the address itself -- no range or clip to point at.
	/// Legacy gets the second for free from `LoopPoint::checkPermissionToBeginSession`
	/// (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`), which refuses before opening.
	/// @param ctx The context to test.
	/// @return True if `on_enter` will succeed.
	static bool can(const menu::MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override { return kMarkerSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_exit() override;
	std::optional<uint32_t> next_timer() const override;

	/// @brief True: the waveform covers the main pads, and the sidebar beneath keeps showing.
	///
	/// `SampleMarkerEditor::renderMainPads` fills every main column; `renderSidebar`
	/// returns false unless the root UI is the keyboard screen, in which case it delegates -- a root
	/// UI read this layer may not make, so the sidebar is left to the host (`pad_regions()`).
	bool is_grid_overlay() const override { return true; }
	/// @brief `Main`: the waveform is drawn on the main pads. The sidebar is the view beneath's;
	///        over the keyboard screen, where legacy draws `instrumentClipView`'s sidebar instead,
	///        the host draws it (`renderClipViewSidebarUnderMarker`, `gui/ui/shell_pad_forward.h`).
	deluge::ui::shell::PadRegions pad_regions() const override { return deluge::ui::shell::PadRegions::Main; }
	/// @brief False: the sidebar underneath still needs to paint.
	bool occludes_grid() const override { return false; }
	/// @brief Every pad but the audition column, which `SampleMarkerEditor::padAction` hands to the instrument clip
	///        view on an instrument clip (the host does, `GridPadClaim::AllButClipAudition`).
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return deluge::ui::shell::GridPadClaim::AllButClipAudition;
	}
	/// @brief `SampleMarkerEditor::getGreyoutColsAndRows`: `*cols = 0b10`, rows 0.
	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override { return {0b10, 0}; }
	/// @brief `SampleMarkerEditor::opened`: over the keyboard screen the greyout is shown at once
	///        (`PadLEDs::skipGreyoutFade`), elsewhere it fades in.
	bool greyout_changes_instantly() const override { return ctx_.root_is_keyboard; }

	/// @name Observable state
	/// For specs, and for whoever wires the menu items up.
	/// @{
	/// @return Which marker the encoder and an untargeted pad press move.
	project::SampleMarker marker_type() const { return marker_type_; }
	/// @return The left edge of the view, in samples.
	int64_t x_scroll() const { return view_.scroll(); }
	/// @return Samples per column.
	int64_t x_zoom() const { return view_.zoom(); }
	/// @return The blink's phase, 0..3 (`SampleMarkerEditor::blinkPhase`
	/// (`origin/main:src/deluge/gui/ui/sample_marker_editor.h`)).
	int8_t blink_phase() const { return blink_phase_; }
	/// @return Is a marker pad being held (legacy's `UI_MODE_HOLDING_SAMPLE_MARKER`)?
	bool holding_marker() const { return holding_; }
	/// @brief Whether X_ENC is held: legacy's `UI_MODE_HOLDING_HORIZONTAL_ENCODER_BUTTON`.
	bool holding_xenc() const { return xenc_held_; }
	/// @brief Whether a zoom is sliding: legacy's `UI_MODE_HORIZONTAL_ZOOM`, from the turn until the frame that finds
	/// the slide over.
	bool sliding() const { return zoom_pending_.has_value() || zoom_.active; }
	/// @brief The last zoom slide begun, running or not.
	const deluge::ui::widgets::grid::ZoomTransition& zoom_slide() const { return zoom_; }
	/// @brief The zone's markers in zone order, each with its frame and the column it lands on now, as
	///        `SampleMarkerEditor::getColsOnScreen` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`) maps
	///        them: a start marker by its first frame, an end marker by its last.
	/// @return The pins; an unset loop marker has no column.
	[[nodiscard]] std::array<views::waveform::MarkerPin, project::kSampleMarkerCount> pins() const;
	/// @}

private:
	/// @brief `SampleMarkerEditor::writeValue`.
	/// @param value The new position, in samples, at least 0. A record that stores less than 64 bits takes it
	///              clamped to what it stores.
	/// @param which Which marker to write, or `NOT_AVAILABLE` for the selected one.
	void write_value(int64_t value, std::optional<project::SampleMarker> which = std::nullopt);
	/// @brief `SampleMarkerEditor::selectEncoderAction`.
	/// @param offset Signed detents.
	void select_encoder(int32_t offset);
	/// @brief `SampleMarkerEditor::padAction`'s main-pad arms.
	/// @param x  Pad column.
	/// @param y  Pad row.
	/// @param on True on press.
	void pad_action(int32_t x, int32_t y, bool on);
	/// @brief `SampleMarkerEditor::horizontalEncoderAction`.
	/// @param offset  Signed detents.
	/// @param zooming Is the horizontal encoder's button held?
	/// @param shift   Is shift held?
	/// @return Actions to file (the loop-length popups).
	deluge::ui::shell::Result horizontal_encoder(int32_t offset, bool zooming, bool shift);
	/// @brief Whether a note is auditioned: legacy's `UI_MODE_AUDITIONING`, from the host.
	bool auditioning() const { return ctx_.ports.ui != nullptr && ctx_.ports.ui->auditioning(); }
	/// @brief Whether any UI mode is up: legacy's `currentUIMode != 0` (a held marker, X_ENC held, a zoom sliding,
	/// auditioning).
	bool mode_up() const { return holding_ || xenc_held_ || sliding() || auditioning(); }
	/// @brief Legacy's `HOLDING_SAMPLE_MARKER | AUDITIONING`, under which a main-pad press and the held pad's release
	/// are refused.
	bool holding_and_auditioning() const { return holding_ && auditioning() && !audition_under_hold_; }
	/// @brief Drop every UI mode and the slide, as legacy's `currentUIMode` is dropped on the way in and out.
	void forget_modes();

	/// @brief `SampleMarkerEditor::shouldAllowExtraScrollRight`.
	bool allow_extra_scroll_right() const;
	/// @brief `SampleMarkerEditor::recordScrollAndZoom` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`).
	void record_scroll_and_zoom();
	/// @brief `SampleMarkerEditor::reverseRemap`.
	/// @param type The marker to remap.
	/// @return Its mirror when the sample is reversed, else @p type unchanged.
	project::SampleMarker reverse_remap(project::SampleMarker type) const;
	/// @brief `isLoopLocked()`.
	bool loop_locked() const;
	/// @brief `SampleMarkerEditor::loopUnlock` / `loopLock` (`origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`):
	///        set the lock and, when @p announce, queue their popup ("FREE" / "LOCK") in `pending_popup_` for `handle`
	///        to file -- the only place that can raise an `Action`.
	/// @param locked   The new state.
	/// @param announce False where the lock is written without `loopLock`/`loopUnlock` (the SHIFT halve and double).
	void set_loop_locked(bool locked, bool announce = true);

	/// @brief The four marker positions, in samples; the loop markers 0 where unset.
	struct Zone {
		uint64_t start{0};
		uint64_t end{0};
		uint32_t loop_start{0};
		uint32_t loop_end{0};
	};

	/// @brief The waveform the markers sit on: the audio clip's, or the selected range's
	///        (`getCurrentSampleHolder` in `origin/main:src/deluge/gui/ui/sample_marker_editor.cpp`).
	/// @param ctx The context to resolve.
	/// @return The address, or nullopt when the context names neither.
	static std::optional<bind::WaveformAddress> address_of(const menu::MenuContext& ctx);
	/// @brief The edited sound's state, or null on an audio clip, where the facts hold no sound, or where the edited
	///        source is not one of the sound's two.
	const deluge::instruments::classic::SoundState* sound_state() const;
	/// @brief The edited source's slot; meaningful where sound_state() is not null.
	deluge::instruments::classic::SourceId source_id() const;
	/// @brief The edited range as the record holds it, or none.
	std::optional<synth::SourceRange> edited_range() const;
	/// @brief An integer row of the audio clip's record, or 0 where it has none.
	/// @param path The row's path.
	int64_t audio_clip_row(std::string_view path) const;
	/// @brief The markers' positions, as the record holds them.
	Zone zone() const;
	/// @brief Is the sample played reversed?
	bool reversed() const;
	/// @brief The key the waveform's view is remembered under: the audio clip's id, or the range's identity; none
	///        where the host names no range there.
	std::optional<services::ViewKey> view_key_of() const;
	/// @brief The view the store remembers for the waveform's range or clip, or an empty one.
	services::SampleView view() const;
	/// @brief Follow the waveform @p info describes: the view takes a growing take's length, and another file reopens
	///        it at its window, clamped into the new file's zooms and scrolls as an open clamps a remembered view.
	void follow_waveform(const bind::WaveformInfo& info);

	menu::MenuContext ctx_{};
	/// The popup `set_loop_locked` queued during the pad press being handled, filed when `handle` returns.
	std::optional<l10n::String> pending_popup_{};
	bind::WaveformAddress where_{};
	/// Whose view the editor shows and records, resolved as it opens.
	std::optional<services::ViewKey> view_key_{};
	/// The view over the waveform: which frames the main columns show.
	views::waveform::WaveformView view_{};
	/// Which waveform `view_` is over (`bind::WaveformInfo::identity`).
	uint64_t identity_{0};
	/// The redraws of a frame whose read left columns pending.
	services::PeakRetry retry_{};

	project::SampleMarker marker_type_ = project::SampleMarker::START;
	int8_t blink_phase_ = 0;
	uint32_t blink_accum_ms_ = 0;

	/// Legacy's `UI_MODE_HOLDING_SAMPLE_MARKER` plus `pressX`/`pressY`, as members
	/// rather than a global UI mode.
	/// @{
	bool holding_ = false;
	/// X_ENC held, entered only with no UI mode or auditioning alone (`buttonAction`'s X_ENC arm).
	bool xenc_held_ = false;
	/// @brief The waveforms the editor reads: its context's port.
	/// @return The port; null where the context has none, which `can` refuses.
	[[nodiscard]] const bind::Waveforms* waveforms() const { return ctx_.ports.waveforms; }
	/// @brief What the waveform the markers sit on is.
	/// @return The info; none where the context has no waveforms port or no file is loaded there.
	[[nodiscard]] std::optional<bind::WaveformInfo> waveform_info() const;
	/// @brief Where a voice plays in the waveform the markers sit on.
	/// @return The frame; none where the context has no waveforms port or nothing plays there.
	[[nodiscard]] std::optional<int64_t> play_position() const;
	/// @brief How the playhead is shown: the device's `pads/playCursor` row through the context's device port; Slow,
	///        the row's default, with no device port.
	[[nodiscard]] deluge::project::FlashCursor play_cursor() const;
	/// A voice played the waveform at the last grid render, so the playhead is asked for again.
	bool playing_ = false;
	/// Each row's tick square at the last render: legacy's `slowFlashSquares`, which the fast cursor flashes against.
	std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> last_ticks_{};
	/// A note was already auditioning when the held marker went down: legacy's hold assigns `currentUIMode`, so that
	/// audition's bit is gone and only one begun under the hold makes `HOLDING_SAMPLE_MARKER | AUDITIONING`.
	bool audition_under_hold_ = false;
	/// How often a moving playhead is redrawn: the slicer's period.
	static constexpr uint32_t kPlayheadMs = 15;
	/// A zoom the next grid render starts sliding: the levels, pin square and step `horizontal_encoder` found.
	std::optional<deluge::ui::gfx::grid::ZoomParams> zoom_pending_;
	/// The running slide.
	deluge::ui::widgets::grid::ZoomTransition zoom_;
	/// The main pads as the last settled frame painted them, the playhead aside: a slide's `from`.
	std::optional<deluge::ui::gfx::grid::PadGrid> shown_;
	int8_t press_x_ = -1;
	int8_t press_y_ = -1;
	/// @}

	/// True while the last `render_grid` saw a marker inside the display, which is legacy's
	/// condition for re-arming the blink timer.
	///
	/// @note **Two different widths decide "on screen" in this scene, and they can disagree.**
	///       This one is `render_grid`'s `main.bounds().w` -- the frame it was actually given. The
	///       *scroll* and *hit-test* arms (`select_encoder`'s `old_col >= 0 && old_col <
	///       kDisplayWidth`, `pad_action`'s column tests, and `pins()`' unset markers)
	///       use `kDisplayWidth`, because they are transcriptions of legacy, which has
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

} // namespace deluge::ui::scenes::marker
