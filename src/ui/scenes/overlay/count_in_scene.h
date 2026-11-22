#pragma once

/// @file
/// @brief `CountInScene` -- the record count-in digit: a small centred, framed popup box showing
///        the beats remaining before recording starts.
///
/// Ports the count-in half of `drain_notifications` (`gui/ui/notify_drain.cpp`). This is
/// **not** a `View`/UI method at all -- the digit is the installed transport port's
/// `bind::Transport::count_in_visual()`, which the host answers from `PlaybackHandler::count_in_visual`: the count-in
/// entry of `PlaybackHandler::setupPlaybackUsingInternalClock` moves it (resetting it to 0 and setting
/// `UI_MODE_RECORD_COUNT_IN`), and so does `PlaybackHandler::actionSwungTick`'s count-in branch, both when the count-in
/// ends and recording starts (resetting the count to 0) and, otherwise, on every remaining beat. The popup lifecycle --
/// when it draws, and when it stops -- is `notify_drain.cpp`'s own edge-detected block, reproduced here in `tick()`.
///
/// @par Who pushes it
///
/// Nothing in the app pushes this scene: the registry registers it, and the live count-in popup on the device is
/// the legacy notification drain's. The host installs its transport port at boot (`set_transport`), so a scene pushed
/// later reads the app's count; with no port installed it reads 0 and shows nothing.
///
/// @par Four apparent facts of the count-in popup -- two of which are not facts on this codebase
///
/// `notify_drain.cpp`'s count-in block appears to say four things: the digit is right-aligned,
/// blink speed is 2, the popup cancels at zero, and the mode clear is masked.
/// Reading the call site alone, that looks right --
/// `display->displayPopup(buffer, 0, true, 255, 2)` does pass `alignRight = true` and
/// `blinkSpeed = 2`. But those two arguments are **dead** on this codebase, and shipping either
/// behaviour would be new UX legacy does not have (the same shape as the "7SEG/OLED split" note in
/// `param_popup_scene.h`):
///
///   * `Display::displayPopup` (`hid/display/display.h`: `bool alignRight = false,
///     uint8_t drawDot = 255, int32_t blinkSpeed = 1`) is a `numeric_driver`-era interface written
///     for the retired 7SEG display, and `OLED` is its **only** surviving override --
///     `grep -rn "class .*Display\|: public Display" src/deluge/hid/display/*.h` finds nothing
///     else. `OLED::displayPopup` (`oled.h`) takes `alignRight`/`drawDot`/`blinkSpeed` as
///     **unnamed** parameters and reads none of the three; its whole body is
///     `popupText(newText, !numFlashes, type)`.
///   * `OLED::popupText` (`oled.cpp`) centres every line (`textPixelX = (OLED_MAIN_WIDTH_PIXELS - lineWidths[l]) >>
///     1`) and has no blink or phase of any kind -- the only timer it touches is
///     `uiTimerManager.setTimer(TimerName::DISPLAY, 800)` for a **non-persistent** popup, and this one is not:
///     `numFlashes == 0` makes `persistent` true (`popupText`'s `bool persistent = !numFlashes` at the call site,
///     `OLED::displayPopup` in `oled.h`), which takes the `else` branch (`OLED::popupText` in `oled.cpp`) and *cancels*
///     any pending timeout. `grep -rn blinkSpeed src/deluge` turns up no reader anywhere in the tree, and
///     `alignRight`'s only other use is an unrelated local in `instrument_clip_view.cpp`.
///
///   So on the shipped OLED-only build the digit is centred, not right-aligned, and it never
///   blinks. This scene reproduces the real, observable behaviour -- centred, static -- not the
///   two dead call-site arguments.
///
///   The other two facts are real:
///
///   * **Cancelled at zero**: `drain_notifications` (`notify_drain.cpp`), the `else` arm's `display->cancelPopup()`.
///   * **The mode clear is masked, and real, but has no port here**: `notify_drain.cpp`
///     clears `currentUIMode &= UI_MODE_AUDITIONING` rather than assigning 0, because
///     `UI_MODE_AUDITIONING` (`1 << 30`) is a flag ORed on top of the exclusive
///     `UI_MODE_RECORD_COUNT_IN`, and an exact clear would drop it whenever a count-in ends during
///     an audition-pad or keyboard hold. `currentUIMode` is declared in `gui/ui/ui.h`, which
///     `src/ui/scenes` may never include (Global Constraint 3) -- this scene has no way to touch it.
///     This is the same wall `services::GlobalControls` already documents for its own
///     `UiMode::RecordCountIn` (`services/global_controls.h`): the mode is
///     something a future root scene's `ViewState` carries and clears, not this overlay. Recorded
///     here as a citation-backed gap, not silently assumed away.
///
/// @par This is a poll, not a caller-driven popup -- unlike `ParamPopupScene`
///
/// `ParamPopupScene::show()` is called by a caller sitting inside the input pipeline
/// (`GuestKnobPresentation::show_value_popup`), so that caller's own `Result` can ask for a repaint the same
/// tick. The count-in digit has no such caller: legacy's own mechanism is a poll -- "the tick tree
/// only moves `currentVisualCountForCountIn` ... the digit is formatted and shown [in the drain],
/// in task context" (`notify_drain.cpp`) -- run every drain pass, unconditionally, whether or
/// not any input arrived. The only port-side hook that runs unconditionally on that shape is
/// `Scene::tick()` armed by `next_timer()`, exactly as `RecorderScene`'s completion poll is
/// (`recorder_scene.h`'s class doc, `kRecorderPollMs`) -- so `tick()` here reads the transport port's
/// `count_in_visual()` itself rather than taking it as a parameter, and `next_timer()` asks for another tick at
/// `kCountInPollMs` **unconditionally**, forever -- not only while a count-in is showing, because
/// this scene also has to notice one *starting*, and nothing else would wake it to look.
///
/// A `show(int32_t)` driven the way `ParamPopupScene::show()` is would not work here: with no repaint request
/// between calls, the shell's OLED gate stays unarmed after the first tick, so every call after the first renders the
/// *previous* digit's pixels -- `Shell::tick`'s step 5 renders a surface only when its gate reports `needs_frame()`,
/// and nothing but a scene's own `next_timer()`/`render_oled` arm, or a real input event, ever asks for that. A scene
/// with no caller in the input pipeline needs the poll shape, not the caller shape.
///
/// @par What this scene actually does
///
///   * `tick()` -- the edge-detector of `drain_notifications` (`notify_drain.cpp`) (`if (count_in != shown_count_in)`)
///     against the transport port's `count_in_visual()`, plus its `if (count_in > 0)` arm: format the digit and
///     (re)draw the box. Idempotent -- an unchanged count changes nothing, matching legacy's own guard exactly (legacy
///     does not redraw either).
///   * The `else` arm: stop drawing (legacy's `cancelPopup()`); the masked mode clear
///     is the documented gap above.
///   * `after_render()` -- fires `Action::Kind::Remove` the tick this scene stops showing, so
///     whatever pushes it can push-and-forget; never `Pop`, which would also take down anything
///     legitimately stacked above it (see the warning on `Action`, `ui/shell/scene.h`).
///
/// @warning **Must not raise `Action::Kind::LockInput`**, and does not -- see the warning on
///          `Action` in `ui/shell/scene.h`. `handle()` is unreachable (`input_transparent()` is true).

#include "ui/scenes/bind/ports/transport.h"
#include "ui/shell/scene.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::ui::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: `FirstApp + 7` (`registry.h`).
inline constexpr deluge::ui::shell::SceneKind kCountInSceneKind =
    static_cast<deluge::ui::shell::SceneKind>(static_cast<uint8_t>(deluge::ui::shell::SceneKind::FirstApp) + 7);

/// @brief How often `tick()` polls the transport port's `count_in_visual()` with no input at all.
///
/// Matches `RecorderScene::kRecorderPollMs`'s shape (same reasoning: a fact only a poll can
/// discover), not its value -- there is no requirement the two ever agree, and this scene's own
/// digit changes at musical-tick granularity, comfortably slower than either poll.
inline constexpr uint32_t kCountInPollMs = 20;

/// @brief The record count-in digit popup. One instance for the whole application (`registry.h`).
class CountInScene final : public deluge::ui::shell::Scene {
public:
	deluge::ui::shell::SceneKind kind() const override { return kCountInSceneKind; }
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::widgets::oled::OledFrame& frame) override;
	/// @brief The popup routes nothing: legacy never calls `buttonAction`/`padAction` on it either.
	/// @return Always `Result::unhandled()`.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	/// @brief The poll: `drain_notifications` (`notify_drain.cpp`), reproduced against the transport port's
	///        `count_in_visual()`. See the class doc's "poll, not a caller-driven
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
	///         surface it never touches would be a permanent blit every poll, the same reasoning `ParamPopupScene`
	///         states explicitly.
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
	/// @brief False: never takes input, so the scene beneath keeps its own input focus.
	bool input_transparent() const override { return true; }

	/// @brief Read the count-in from @p transport, or show nothing when it is null.
	/// @param transport The transport port; not owned, it must outlive the scene's use of it.
	void set_transport(const deluge::ui::scenes::bind::Transport* transport) { transport_ = transport; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return True while the popup is drawn.
	bool showing() const { return showing_; }
	/// @return The digit text last shown, for specs.
	std::string_view shown_text() const { return {digit_, digit_len_}; }
	/// @}

private:
	static constexpr size_t kTextCap = 11; // `char buffer[12]` (`drain_notifications` in `notify_drain.cpp`).
	const deluge::ui::scenes::bind::Transport* transport_ = nullptr; ///< The installed port; null reads no count-in.
	char digit_[kTextCap + 1] = {};
	size_t digit_len_ = 0;

	bool showing_ = false;     // Whether the box is currently drawn.
	bool was_showing_ = false; // Set true whenever showing_ becomes true; after_render's "just
	                           // stopped showing" edge.
	int32_t shown_count_ = 0;  // `notify_drain.cpp`'s `shown_count_in` static.
};

/// @brief The application's one record count-in popup.
/// @return The singleton.
CountInScene& count_in_scene();

} // namespace deluge::ui::scenes::overlay
