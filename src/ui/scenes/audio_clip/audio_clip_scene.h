#pragma once

/// @file
/// @brief `AudioClipScene`, the audio clip view: the clip's waveform on the pads in its colour, the start and end
///        markers, the play cursor, the session macros while SESSION is held, and the output's name and the clip's
///        section on the OLED.
///
/// A root scene (`is_root()`), so the global buttons `services::GlobalControls` gates on a root reach it: SAVE, LOAD,
/// SYNC_SCALING, TAP_TEMPO, BACK's undo and redo, shift and SELECT, and LEARN with PLAY and RECORD while learning.
///
/// **Gestures.** The horizontal encoder scrolls; with X held it zooms (showing the zoom as a note length until X goes
/// up); with SHIFT it changes the clip's length, stretching the sample; with SHIFT and X, without stretching; with Y it
/// shifts the clip's contents. X and Y together fit the clip to its sample. BACK with X held clears the clip. SHIFT and
/// the vertical encoder change its colour. The select encoder scrolls the track's audio mode, or with SHIFT moves the
/// clip to another audio output. A pad on the end column raises the end marker (the start column, the start marker,
/// behind the trim-from-start feature); with a marker up, a pad moves it. Every main pad is first offered to the
/// menu's shortcuts. SESSION's short press leaves for the song; held, it shows the macros, and a macro pad's release
/// runs one. CLIP opens the automation view; SELECT opens the clip's menu.
///
/// **The UI mode word.** The scene keeps a two-value copy of the firmware's mode word (holding SESSION, holding X),
/// with the word's exclusive-value and flag arithmetic, because its arms branch on it exactly: X pressed with SHIFT
/// held enters no mode, and BACK clears only while X is the whole of the word.
///
/// **Drawing.** The waveform comes from the waveforms port at the clip's AUDIO_CLIP address, and a frame whose read
/// left a column pending asks again until it is answered. The play cursor is frame-driven while it can move. Text too
/// wide for the panel scrolls (`scroll_text`). A scroll slides the pads a column a step and a zoom slides them about
/// the pin square (the shared `scroll_transition` and `zoom_transition`); the mode word holds a scroll or a zoom while
/// it runs, so input waits for it.
///
/// @warning This scene must not raise `Action::Kind::LockInput`, and does not. See the warning on `Action` in
///          `ui/shell/scene.h`.

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
#include "ui/widgets/core/state.h"
#include "ui/widgets/grid/columns.h"
#include "ui/widgets/grid/scroll_transition.h"
#include "ui/widgets/grid/zoom_transition.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::project {
struct DocClip;
class Song;
} // namespace deluge::project

namespace deluge::ui::scenes::services {
class GlobalControls;
} // namespace deluge::ui::scenes::services

namespace deluge::ui::scenes::audio_clip {

/// @brief How long one phase of the marker blink lasts.
inline constexpr uint32_t kMarkerBlinkMs = static_cast<uint32_t>(widgets::core::kSampleMarkerBlinkTime);

/// @name The UI mode word's values, as the firmware's
/// @{
/// @brief No mode.
inline constexpr uint32_t kUiModeNone = 0;
/// @brief A value at or below this occupies the low byte and displaces any other low-byte value; one above it is an
///        independent flag.
inline constexpr uint32_t kExclusiveUiModesMask = 255;
/// @brief SESSION held: an exclusive value.
inline constexpr uint32_t kUiModeHoldingSongButton = 65;
/// @brief X held: a flag bit.
inline constexpr uint32_t kUiModeHoldingHorizontalEncoder = 1u << 31;
/// @brief A zoom sliding: an exclusive value; input waits for it.
inline constexpr uint32_t kUiModeHorizontalZoom = 2;
/// @brief A scroll sliding: a flag bit; input waits for it.
inline constexpr uint32_t kUiModeHorizontalScroll = 1u << 29;
/// @}

/// @brief The `SceneKind` this scene is registered under (`ui/scenes/registry.h`).
inline constexpr deluge::ui::shell::SceneKind kAudioClipSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 9);

/// @brief The audio clip view. One instance for the whole application (`ui/scenes/registry.h`).
class AudioClipScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at a clip and the song's record it lives in, and take up the song's clip navigation
	///        (`ClipFacts::clip_navigation`) as its scroll and zoom; with none, the view stays as it was.
	///
	/// @param clip  The clip to show; nullopt leaves the scene with nothing to draw.
	/// @param model The song's record, which holds the clip's record and its track's rows; null is treated as
	///              "no clip". Borrowed, not owned; it must outlive the scene's use of it.
	void open(std::optional<deluge::project::ClipAddress> clip, const deluge::project::Song* model);

	/// @brief Install the ports the scene reads and writes through: the app installs its own at boot
	///        (`gui::shell_host::install_scene_ports`), a spec the app's or a copy with fakes in it.
	///
	/// With none, or a null port, the scene reads no clip fact, no waveform, the SLOW play cursor, not playing and
	/// no clocks, writes nothing, and is not frame-driven.
	/// @param ports The set, or null. Borrowed, not owned; it must outlive the scene's use of it.
	void set_ports(const deluge::ui::scenes::bind::Ports* ports) { ports_ = ports; }
	/// @brief Give the scene the global controls whose clip-arm flash its macros column reads and re-arms.
	/// @param controls The service, or null (the column then shows the flash's off phase and arms nothing).
	void set_global_controls(deluge::ui::scenes::services::GlobalControls* controls) { global_controls_ = controls; }

	/// @name The two marker latches: drawn on the pads, and widening the length and zoom limits while up
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

	/// @return The furthest the view scrolls: the clip's loop length, plus one while the end marker is up.
	[[nodiscard]] uint32_t max_length() const;
	/// @return The widest the view zooms: the clip's own limit, doubled while the end marker is up.
	[[nodiscard]] uint32_t max_zoom() const;
	/// @return Whether the view may scroll and zoom: not while a take records with no clock, nor in the arranger.
	[[nodiscard]] bool scrollable_and_zoomable() const;

	/// @brief Drop the time-stretch by making the clip exactly as long as the sample it plays, with the length's popup:
	///        the X and Y chord, and the menu's action of the same name.
	void set_clip_length_equal_to_sample_length();

	/// @brief Take the song's clip scroll and zoom again after something else changed them (an undo), sliding to them
	///        as `initiateXScroll` and `initiateXZoom` do.
	void navigation_restored();

	/// @return The clip the scene shows, or nothing.
	[[nodiscard]] const std::optional<deluge::project::ClipAddress>& clip() const { return clip_; }

	/// @return Whether the SHIFT shortcut overview covers the main pads (`ClipView::shouldRenderShortcutsOverview`).
	[[nodiscard]] bool overview_shown() const { return overview_shown_; }

	/// @brief The scene's mode word.
	/// @return `kUiModeNone`, or an OR of `kUiModeHoldingSongButton` / `kUiModeHoldingHorizontalEncoder`.
	[[nodiscard]] uint32_t ui_mode() const { return ui_mode_; }

	/// @brief The scroll and zoom this scene renders from, and the gestures that move it.
	/// @return The state, for the encoder arms and for the specs.
	/// @{
	[[nodiscard]] timeline::TimelineState& timeline() { return timeline_; }
	[[nodiscard]] const timeline::TimelineState& timeline() const { return timeline_; }
	/// @}

	/// @brief End a running or pending slide at once, as its last step would: the view shows itself and input is
	///        taken again. A host that does not run the slide's frames (a spec comparing settled frames) calls it.
	void finish_slides();
	/// @return Whether a scroll or zoom is sliding, or starts on the next frame.
	[[nodiscard]] bool sliding() const {
		return pending_slide_.kind != PendingSlide::Kind::NONE || scroll_.active || zoom_.active;
	}

	/// @brief The play cursor's column, as this scene last computed it.
	/// @return The column, or `ui::widgets::grid::kNoTickSquare` for no cursor.
	[[nodiscard]] int32_t last_tick_square() const { return last_tick_square_; }

	/// @brief The blink phase the two marker columns are drawn in.
	/// @return True on the bright half of the blink.
	[[nodiscard]] bool blink_on() const { return blink_on_; }

	deluge::ui::shell::SceneKind kind() const override { return kAudioClipSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::widgets::grid::GridFrame& frame) override;
	/// @brief BACK off, as the view's focus leaves it, and SESSION lit while it is held; a scene above overrides
	///        either.
	void leds(deluge::ui::widgets::core::LedFrame& frame) override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	void tick(uint32_t dt_ms) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	/// @brief The view's focus coming back as a scene above closes: the restore point and both markers down.
	void on_resume() override;
	/// @brief A change to the clip scrolls the view back left when it now lies past the clip's end; the shell owns
	///        every redraw a change asks for.
	///
	/// @param dirty What changed.
	void on_model_changed(const deluge::base::notify::DirtySet& dirty) override;

	/// @brief True: the audio clip view is a root view.
	bool is_root() const override { return true; }
	/// @brief False: this scene is the grid base, and paints every pad including the sidebar.
	bool is_grid_overlay() const override { return false; }
	/// @brief True: it covers the whole grid, so nothing beneath it draws.
	bool occludes_grid() const override { return true; }
	/// @brief False: its OLED page is opaque.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	/// @brief False: a root view takes input.
	bool input_transparent() const override { return false; }

	/// @brief When the marker blink next changes phase, while a marker is up. The play cursor is `frame_driven()`.
	///
	/// @return Milliseconds from now, or `std::nullopt`.
	std::optional<uint32_t> next_timer() const override;
	/// @brief True while the play cursor is live (the flash cursor is not OFF and the clip plays): the column may move
	///        on any frame.
	bool frame_driven() const override;
	/// @brief The grid only: neither the cursor nor the marker blink touches the OLED page, whose scrolling text arms
	///        its own repaints.
	RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }
	/// @brief Every column, sidebar included, while the shortcut overview shows (`ClipView::getGreyoutColsAndRows`);
	///        nothing otherwise.
	deluge::ui::widgets::grid::GreyoutMask greyout_mask() const override;
	/// @brief True while the overview shows, whose greyout `renderClipShortcutsOverview` applies at once; it fades
	///        out when the overview goes (`reassessGreyout`).
	bool greyout_changes_instantly() const override { return overview_shown_; }

private:
	/// @brief The view's own state as its focus comes back: the restore point and both markers down. The BACK LED
	///        is `leds()`'s; the knobs' focus on the clip is the legacy root view's, which hosts this scene.
	void focus_regained();

	/// @brief `ClipView::maybeStartShortcutOverview` for a button edge, and `stopShortcutOverview` for the turns that
	///        call it: a SHIFT edge, or SHIFT with an instrument button, starts the overview afresh while the setting
	///        is "menu and clip"; any other press, and a horizontal or vertical turn, ends it until the next.
	/// @param shift Whether SHIFT is held (or latched) as the event arrives.
	void note_overview_input(const deluge::ui::shell::UiEvent& event, bool shift);
	/// @brief Take the song's clip scroll and zoom, with `supportsTriplets()` false as the view has it.
	void adopt_navigation();
	/// @return Whether the overview shows with SHIFT @p shift (held or latched): the setting is "menu and clip" and
	///         nothing has ended it since it started.
	[[nodiscard]] bool overview_wanted(bool shift) const;
	/// @return Whether the overlay setting is "menu and clip", the one that shows the overview over a clip.
	[[nodiscard]] bool overview_setting_on() const;
	/// @brief `renderClipShortcutsOverview`'s pads: each main pad the colour of the menu shortcut it opens.
	void paint_overview(deluge::ui::widgets::grid::GridFrame& main);

	/// @brief `set_clip_length_equal_to_sample_length`'s edit and popup alone, for an input `handle` follows up.
	void clip_length_to_sample_length();

	/// @brief The play cursor's column for this frame, latched when it moves.
	/// @return The column for every row, or `ui::widgets::grid::kNoTickSquare`.
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
	/// @return Is there a waveform: the take while recording linearly, otherwise the clip's file?
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
	/// @brief Where the arms that leave the view, or open a menu over it, go.
	[[nodiscard]] deluge::ui::scenes::bind::RootViews* root_views() const;
	/// @brief The song's session macros, which the SESSION hold shows and runs.
	[[nodiscard]] deluge::ui::scenes::bind::SessionMacros* macros() const;
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

	/// @name The mode word's operations, with the firmware's exclusive-value and flag arithmetic
	/// @{
	/// @brief Enter @p mode: an exclusive value replaces the low byte, a flag is ORed in.
	/// @param mode One of the two `kUiMode*` values.
	void enter_ui_mode(uint32_t mode);
	/// @brief Leave @p mode.
	/// @param mode One of the two `kUiMode*` values.
	void exit_ui_mode(uint32_t mode);
	/// @param mode One of the two `kUiMode*` values.
	/// @return Whether it is active.
	[[nodiscard]] bool ui_mode_active(uint32_t mode) const;
	/// @return Whether the word is clear.
	[[nodiscard]] bool no_ui_mode_active() const { return ui_mode_ == kUiModeNone; }
	/// @}

	/// @name The input arms
	/// @{
	/// @brief A button.
	/// @param event The button event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result button_action(const deluge::ui::shell::UiEvent& event,
	                                        const deluge::ui::shell::UiContext& ctx);
	/// @brief A pad.
	/// @param event The pad event.
	/// @param ctx   The world around it.
	/// @return What the shell should do.
	deluge::ui::shell::Result pad_action(const deluge::ui::shell::UiEvent& event,
	                                     const deluge::ui::shell::UiContext& ctx);
	/// @brief Route @p event to the arm that takes it; `handle` is this, then `publish_navigation`.
	/// @param event The event. @param ctx Its context.
	/// @return What the arm returned.
	deluge::ui::shell::Result dispatch(const deluge::ui::shell::UiEvent& event,
	                                   const deluge::ui::shell::UiContext& ctx);
	/// @brief Write the view's scroll and zoom as the song's clip navigation when either moved since the last write.
	void publish_navigation();
	/// @brief A horizontal turn: SHIFT and X edit the length without stretching, everything else goes on.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief A horizontal turn's clip arms: SHIFT stretches the length, Y shifts the contents, otherwise navigate.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result clip_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief Navigate: zoom with X held, scroll with no mode. Its first act forgets the linear-recording restore
	/// point,
	///        even on a turn it then refuses, and no earlier arm forgets it.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn: `now_ms` for the zoom pause, `shift` for the scroll's gate.
	/// @return What the shell should do.
	deluge::ui::shell::Result nav_horizontal_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief A vertical turn: SHIFT changes the clip's colour.
	/// @param offset Signed detents.
	/// @param ctx    The world around the turn.
	/// @return What the shell should do.
	deluge::ui::shell::Result vertical_encoder_action(int32_t offset, const deluge::ui::shell::UiContext& ctx);
	/// @brief A select turn: the track's audio mode, or with SHIFT the clip to another audio output.
	/// @param offset Signed detents.
	/// @param shift  Is shift held?
	/// @return True if anything changed, so the caller can ask for a repaint.
	bool select_encoder_action(int32_t offset, bool shift);
	/// @brief Put the end marker down, as most buttons do.
	/// @return True if the end marker was up and has been put down.
	bool deactivate_marker_if_necessary();
	/// @brief Paint the view's main columns into @p main: the waveform, the grey beyond the clip and the markers.
	/// @param main A frame over the main columns.
	/// @param retry_frame The frame whose gate asks again for a column still pending.
	void paint_main(deluge::ui::widgets::grid::GridFrame& main, deluge::ui::widgets::grid::GridFrame& retry_frame);
	/// @brief Note a slide for the next frame when the view is not where it was: a zoom slides about the pin square, a
	///        scroll a column a step; either holds input until it ends.
	/// @param before_scroll The scroll before the input. @param before_zoom The zoom before it.
	void note_slide(int32_t before_scroll, uint32_t before_zoom);
	/// @brief The song's macros in the first sidebar column, the held pad brightened, and the clip-arm flash kept going
	///        while any clip they name is armed.
	/// @param f The root grid frame.
	void render_macros(deluge::ui::widgets::grid::GridFrame& f);
	/// @brief Clear the clip: its sample, and its automation unless the device keeps automation on a clear.
	/// @return The l10n string id of the popup to show, or 0 with no clip to clear.
	uint16_t clear_clip();
	/// @}

	/// @brief Show the zoom as a note length per column, until X goes up.
	void show_zoom_level() const;

	/// @name The length gestures
	/// @{
	/// @brief SHIFT, X and a turn: the clip's length without stretching its sample.
	/// @param offset Signed detents.
	/// @return What the shell should do.
	deluge::ui::shell::Result edit_clip_length_without_timestretching(int32_t offset);
	/// @brief The new length from the view's squares, its request, and the scroll and zoom after.
	/// @param offset     Signed detents.
	/// @param old_length The clip's length before the edit.
	/// @param kind       STRETCH for the shift turn, NO_STRETCH for the shift turn with the encoder's button held.
	/// @return The new length.
	uint32_t change_clip_length(int32_t offset, uint32_t old_length, deluge::ui::scenes::bind::ResizeKind kind);
	/// @brief The view's scroll and zoom after a length request, and the length's popup.
	/// @param old_length The length before the request.
	/// @param new_length The length after it.
	void after_loop_length_change(int32_t old_length, int32_t new_length);
	/// @brief Tell the clip port where a redo of the last length change scrolls back to: the view as it is now.
	void note_undo_scroll();
	/// @brief Show @p number as bars, beats and sixteenths in a popup (`TimelineState::bars_and_beats`).
	/// @param number         The tick position or length to describe.
	/// @param count_from_one True to number bars/beats from one.
	void show_bars_and_beats(uint32_t number, bool count_from_one);
	/// @}

	/// The clip this scene shows and the song's record it lives in (see `open`), and the ports `set_ports` installed.
	/// @{
	std::optional<deluge::project::ClipAddress> clip_;
	const deluge::project::Song* model_ = nullptr;
	const deluge::ui::scenes::bind::Ports* ports_ = nullptr;
	deluge::ui::scenes::services::GlobalControls* global_controls_ = nullptr;
	/// The slide the next frame starts, if the view moved.
	struct PendingSlide {
		enum class Kind : uint8_t { NONE, SCROLL, ZOOM };
		Kind kind = Kind::NONE;
		int8_t direction = 1;                     ///< SCROLL: +1 when the view moved right
		deluge::ui::gfx::grid::ZoomParams zoom{}; ///< ZOOM: the two levels and the pin square
	} pending_slide_;
	/// The main columns as last settled (what a slide starts from), and the two slides.
	std::optional<deluge::ui::gfx::grid::PadGrid> shown_;
	deluge::ui::widgets::grid::ScrollTransition scroll_;
	deluge::ui::widgets::grid::ZoomTransition zoom_;
	/// The two side-scrollers' state: the output's name, and the clip's section-and-name line.
	deluge::ui::widgets::core::ScrollText name_scroll_{};
	deluge::ui::widgets::core::ScrollText info_scroll_{};
	/// @}

	/// The redraws of a frame whose read left columns pending, with no give-up. The scene keeps no columns: the
	/// waveforms port does.
	deluge::ui::scenes::services::PeakRetry retry_{deluge::ui::scenes::services::PeakRetry::Patience::UNTIL_ANSWERED};

	/// The scroll and zoom; the audio clip view has no triplets view.
	timeline::TimelineState timeline_{.supports_triplets = false};
	/// The scroll and zoom last written as the song's clip navigation, or adopted from it.
	struct Published {
		int32_t x_scroll = 0;
		uint32_t x_zoom = 0;
	} published_;

	/// Whether each marker is up.
	/// @{
	bool end_marker_visible_ = false;
	bool start_marker_visible_ = false;
	/// @}
	/// The shortcut overview: ended by an input since it last started (`exitedShortcutOverview`), and on the pads.
	/// @{
	bool overview_exited_ = false;
	bool overview_shown_ = false;
	/// @}
	/// The markers' blink phase.
	bool blink_on_ = false;
	/// Milliseconds accumulated towards the next `blink_on_` toggle.
	uint32_t blink_accum_ms_ = 0;

	/// The cursor's latched column, and whether the next frame must send it whatever it is.
	/// @{
	int32_t last_tick_square_ = 0;
	bool must_redraw_tick_squares_ = false;
	/// The cursor moved this frame, which the fast cursor flashes.
	bool tick_squares_set_ = false;
	/// Each row's square as last flashed.
	std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> flashed_ticks_ = filled_no_tick();
	static constexpr std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> filled_no_tick() {
		std::array<uint8_t, deluge::ui::gfx::grid::kDelugeGrid.rows> a{};
		a.fill(deluge::ui::widgets::grid::kNoTickSquare);
		return a;
	}
	/// @}

	/// @brief The flash-cursor setting: the device's `pads/playCursor` row; Slow, its default, with no device port.
	project::FlashCursor flash_cursor() const {
		return bind::read_device<project::FlashCursor>(device(), project::device_path::kPlayCursor);
	}

	/// The mode word (see the file's notes).
	uint32_t ui_mode_ = kUiModeNone;

	/// When SESSION went down, on the shell's clock: a release within a short press leaves for the song.
	uint32_t song_button_pressed_ms_ = 0;
};

/// @brief The application's one audio clip view.
/// @return The singleton.
AudioClipScene& audio_clip_scene();

} // namespace deluge::ui::scenes::audio_clip
