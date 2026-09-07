#pragma once

/// @file
/// @brief `CountInScene` -- the record count-in digit: a small centred, framed popup box showing
///        the beats remaining before recording starts.
///
/// Ports the count-in half of `drain_notifications` (`gui/ui/notify_drain.cpp:92-114`). This is
/// **not** a `View`/UI method at all -- the digit itself is `PlaybackHandler::count_in_visual()`
/// (`playback_handler.h:175`), moved by `PlaybackHandler::setupPlayback()`'s count-in entry
/// (`playback_handler.cpp:441-447`, resetting it to 0 and setting `UI_MODE_RECORD_COUNT_IN`) and
/// by `PlaybackHandler::considerPlaybackTiming()`'s tick tree, both when a count-in position first
/// starts recording (`:1037-1039`, resetting the count to 0) and, otherwise, on every remaining
/// beat (`:1044-1051`). The popup lifecycle -- when it draws, and when it stops -- is
/// `notify_drain.cpp`'s own edge-detected block, reproduced here in `tick()`.
///
/// @par The brief's "four facts" -- two of which are not facts on this codebase
///
/// The task brief asks for four things read out of `notify_drain.cpp:92-113`: the digit is
/// right-aligned, blink speed is 2, the popup cancels at zero, and the mode clear is masked.
/// Reading the call site alone, that looks right --
/// `display->displayPopup(buffer, 0, true, 255, 2)` does pass `alignRight = true` and
/// `blinkSpeed = 2`. But those two arguments are **dead** on this codebase, and shipping either
/// behaviour would be new UX legacy does not have (Global Constraint 1) -- the same shape Task 1
/// found in its "7SEG/OLED split" note:
///
///   * `Display::displayPopup` (`hid/display/display.h:48-49`: `bool alignRight = false,
///     uint8_t drawDot = 255, int32_t blinkSpeed = 1`) is a `numeric_driver`-era interface written
///     for the retired 7SEG display, and `OLED` is its **only** surviving override --
///     `grep -rn "class .*Display\|: public Display" src/deluge/hid/display/*.h` finds nothing
///     else. `OLED::displayPopup` (`oled.h:154-157`) takes `alignRight`/`drawDot`/`blinkSpeed` as
///     **unnamed** parameters and reads none of the three; its whole body is
///     `popupText(newText, !numFlashes, type)`.
///   * `OLED::popupText` (`oled.cpp:605-644`) centres every line
///     (`textPixelX = (OLED_MAIN_WIDTH_PIXELS - lineWidths[l]) >> 1`, `:629`) and has no blink or
///     phase of any kind -- the only timer it touches is `uiTimerManager.setTimer(TimerName::DISPLAY,
///     800)` for a **non-persistent** popup (`:636-638`), and this one is not: `numFlashes == 0`
///     makes `persistent` true (`popupText`'s `bool persistent = !numFlashes` at the call site,
///     `oled.h:156`), which takes the `else` branch (`oled.cpp:640-643`) and *cancels* any pending
///     timeout.
///     `grep -rn blinkSpeed src/deluge` turns up no reader anywhere in the tree, and `alignRight`'s
///     only other use is an unrelated local in `instrument_clip_view.cpp:7009`.
///
///   So on the shipped OLED-only build the digit is centred, not right-aligned, and it never
///   blinks. This scene reproduces the real, observable behaviour -- centred, static -- not the
///   two dead call-site arguments; this is an error in the task brief, not a decision made here.
///
///   The other two facts are real:
///
///   * **Cancelled at zero**: `notify_drain.cpp:102-103`, the `else` arm's `display->cancelPopup()`.
///   * **The mode clear is masked, and real, but has no port here**: `notify_drain.cpp:104-111`
///     clears `currentUIMode &= UI_MODE_AUDITIONING` rather than assigning 0, because
///     `UI_MODE_AUDITIONING` (`1 << 30`) is a flag ORed on top of the exclusive
///     `UI_MODE_RECORD_COUNT_IN`, and an exact clear would drop it whenever a count-in ends during
///     an audition-pad or keyboard hold. `currentUIMode` is declared in `gui/ui/ui.h:30`, which
///     `src/scenes` may never include (Global Constraint 3) -- this scene has no way to touch it.
///     This is the same wall `services::GlobalControls` already documents for its own
///     `UiMode::RecordCountIn` (`services/global_controls.h:236-256`, `:323-329`): the mode is
///     something a future root scene's `ViewState` carries and clears, not this overlay. Recorded
///     here as a citation-backed gap, not silently assumed away.
///
/// @par This is a poll, not a caller-driven popup -- unlike `ParamPopupScene`
///
/// `ParamPopupScene::show()` is called by a caller sitting inside the input pipeline (a future
/// `ModKnobs::mod_encoder_action`), so that caller's own `Result` can ask for a repaint the same
/// tick. The count-in digit has no such caller: legacy's own mechanism is a poll -- "the tick tree
/// only moves `currentVisualCountForCountIn` ... the digit is formatted and shown [in the drain],
/// in task context" (`notify_drain.cpp:92-93`) -- run every drain pass, unconditionally, whether or
/// not any input arrived. The only port-side hook that runs unconditionally on that shape is
/// `Scene::tick()` armed by `next_timer()`, exactly as `RecorderScene`'s completion poll is
/// (`recorder_scene.h`'s class doc, `kRecorderPollMs`) -- so `tick()` here reads
/// `playbackHandler.count_in_visual()` itself (`playback/playback_handler.h` is on this layer's
/// allowed list) rather than taking it as a parameter, and `next_timer()` asks for another tick at
/// `kCountInPollMs` **unconditionally**, forever -- not only while a count-in is showing, because
/// this scene also has to notice one *starting*, and nothing else would wake it to look.
///
/// (An earlier version of this scene took the count as a `show(int32_t)` parameter, driven by the
/// spec the way `ParamPopupScene::show()` is. Its own spec caught the mistake: driving `show()`
/// directly, with no repaint request of any kind between calls, left the shell's OLED gate
/// unarmed after the first tick, so every call after the first rendered the *previous* digit's
/// stale pixels -- `Shell::tick`'s step 5 renders a surface only when its gate reports
/// `needs_frame()`, and nothing but a scene's own `next_timer()`/`render_oled` arm, or a real input
/// event, ever asks for that. A caller-driven scene with no caller in the input pipeline needs the
/// poll shape, not the caller shape.)
///
/// @par What this scene actually does
///
///   * `tick()` -- `notify_drain.cpp:96`'s edge-detector (`if (count_in != shown_count_in)`) against
///     `playbackHandler.count_in_visual()`, plus its `if (count_in > 0)` arm (`:97-101`): format the
///     digit and (re)draw the box. Idempotent -- an unchanged count changes nothing, matching
///     legacy's own guard exactly (legacy does not redraw either).
///   * The `else` arm (`:102-111`): stop drawing (legacy's `cancelPopup()`); the masked mode clear
///     is the documented gap above.
///   * `after_render()` -- fires `Action::Kind::Remove` the tick this scene stops showing, so
///     whatever pushes it can push-and-forget; never `Pop`, which would also take down anything
///     legitimately stacked above it (see the warning on `Action`, `shell/scene.h`).
///
/// @warning **Must not raise `Action::Kind::LockInput`**, and does not -- see the warning on
///          `Action` in `shell/scene.h`. `handle()` is unreachable (`input_transparent()` is true).

#include "shell/scene.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 7` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kCountInSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 7);

/// @brief How often `tick()` polls `playbackHandler.count_in_visual()` with no input at all.
///
/// Matches `RecorderScene::kRecorderPollMs`'s shape (same reasoning: a fact only a poll can
/// discover), not its value -- there is no requirement the two ever agree, and this scene's own
/// digit changes at musical-tick granularity, comfortably slower than either poll.
inline constexpr uint32_t kCountInPollMs = 20;

/// @brief The record count-in digit popup. One instance for the whole application (`registry.h`).
class CountInScene final : public deluge::ui::shell::Scene {
public:
	deluge::ui::shell::SceneKind kind() const override { return kCountInSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	/// @brief The popup routes nothing: legacy never calls `buttonAction`/`padAction` on it either.
	/// @return Always `Result::unhandled()`.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	/// @brief The poll: `notify_drain.cpp:92-113`, reproduced against
	///        `playbackHandler.count_in_visual()`. See the class doc's "poll, not a caller-driven
	///        popup" note for why this, and not a `show()` a caller invokes, is the entry point.
	/// @param dt_ms Unused: nothing here counts down a duration, only compares the model's own
	///              current value against what was last shown.
	void tick(uint32_t dt_ms) override;
	/// @brief Files `Action::Kind::Remove` the tick the popup stops showing; see the class doc.
	deluge::ui::shell::Result after_render() override;
	/// @return `kCountInPollMs`, unconditionally -- this scene must notice a count-in *starting* as
	///         well as ending or changing, and nothing else would wake it to look. See the class
	///         doc's "poll, not a caller-driven popup" note.
	std::optional<uint32_t> next_timer() const override { return kCountInPollMs; }
	/// @return `{oled=true, grid=false}` -- this scene paints the OLED only; arming the grid for a
	///         surface it never touches would be a permanent blit every poll (shell spec §15.1,
	///         §15.3), the same reasoning `ParamPopupScene` states explicitly.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = true, .grid = false}; }

	/// @brief True: the scene beneath keeps painting the OLED around this popup's small centred
	///        box.
	bool oled_transparent(deluge::ui::shell::SurfaceRole role) const override {
		return role == deluge::ui::shell::SurfaceRole::Main;
	}
	/// @brief True: must not be mistaken for the grid base -- `Shell::render_grid` picks the
	///        topmost *non-overlay* scene as the grid base, so a popup declaring itself not-an-
	///        overlay becomes eligible to be the base and renders the grid blank.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: this popup composites nothing on the grid, so it must not halve the
	///        brightness of a grid it never touches (`shell/scene.h`'s `dims_grid` note).
	bool dims_grid() const override { return false; }
	/// @brief False: never takes input, so the scene beneath keeps its own input focus.
	bool input_transparent() const override { return true; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return True while the popup is drawn.
	bool showing() const { return showing_; }
	/// @return The digit text last shown, for specs.
	std::string_view shown_text() const { return {digit_, digit_len_}; }
	/// @}

private:
	static constexpr size_t kTextCap = 11; // `char buffer[12]` (notify_drain.cpp:98).
	char digit_[kTextCap + 1] = {};
	size_t digit_len_ = 0;

	bool showing_ = false;     // Whether the box is currently drawn.
	bool was_showing_ = false; // Set true whenever showing_ becomes true; after_render's "just
	                           // stopped showing" edge.
	int32_t shown_count_ = 0;  // `notify_drain.cpp:94`'s `shown_count_in` static.
};

/// @brief The application's one record count-in popup.
/// @return The singleton.
CountInScene& count_in_scene();

} // namespace deluge::scenes::overlay
