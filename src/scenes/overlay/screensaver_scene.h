#pragma once

/// @file
/// @brief `ScreensaverScene` -- the idle animation: a blank panel, a starfield, or a field of
///        Deluge rain, drawn over the whole OLED after a configurable period with no input.
///
/// Ports the **drawing** half of legacy's `hid::display::Screensaver`
/// (`hid/display/screensaver.h`). The *policy* half -- when to show it and when to wake --
/// already shipped as `shell::Screensaver` (`shell/screensaver.h`), which pushes this scene by
/// kind; this scene is what that service finds.
///
/// The two animations live in `ui/oled/rainfall.h` and `ui/oled/starfield.h`, because `src/scenes/**` may include
/// neither `gui/` nor `hid/` (`scripts/layers.toml`'s `scenes-includes-gui` rule). Legacy's `Screensaver` draws the
/// same two objects; see `ui/oled/rainfall.h`'s file doc.
///
/// @par What chooses the mode, and where legacy chooses it
///
/// The `ui/screensaver/mode` DEVICE row (`project::device_path::kScreensaverMode`), read through the installed device
/// port (`set_device`) as a `project::ScreensaverMode`. Legacy reads its global on every question:
/// `Screensaver::timerEvent` (`hid/display/screensaver.cpp`) for the OFF early-out, the activation switch that
/// scatters, the per-frame switch that advances and the frame interval, and `Screensaver::render` again for the switch
/// that draws. This scene reads the row where legacy's timer event does -- in `on_enter` and in every `tick` -- and
/// keeps it in a member that `render_oled`, `next_timer` and `mode()` answer from, so the shell's several
/// `next_timer()` questions per tick cost no row read. The setting cannot change while the screensaver shows: a mode
/// edit is legacy's `settingsChanged()` path (`shell::Screensaver::set_idle_ms`, and see the policy list below), which
/// takes the screensaver down first. With no port installed the row reads its default, `DELUGE`.
///
/// @par The legacy policy this scene leaves to `shell::Screensaver`
///
///   * **`settingsChanged()`** (`Screensaver::settingsChanged` in `hid/display/screensaver.cpp`): wake, then re-arm or
///     cancel for the new settings -- `shell::Screensaver::set_idle_ms`, reached through
///     `Shell::set_screensaver_idle_ms` from the boot wiring and the device port's store for the screensaver rows
///     (`scenes/registry.h`'s boot-wiring record).
///   * **The inhibitors** (`Screensaver::timerEvent` in `hid/display/screensaver.cpp`): the working animation, a
///     permanent-looking popup or a running stem export takes the screensaver down and re-arms the full timeout. The
///     working animation is the shell's own scene; the other two are the host's fact
///     (`Shell::set_screensaver_inhibit`). The 1000 ms re-check that runs them while the screensaver shows is the
///     service's `next_timer`, so this scene's own timer is only the animation's.
///
///     A `BLANK` -> `STARSCAPE`/`DELUGE` change cannot happen while this scene is up: changing the
///     setting needs physical input, and input runs `shell::Screensaver::note_activity` first, which
///     pops this scene. The next push re-reads the mode and scatters the right field.
///   * **`project::ScreensaverMode::OFF`** (`Screensaver::arm`, `Screensaver::timerEvent` and `Screensaver::render` in
///     `hid/display/screensaver.cpp`): the port of "off" is `shell::Screensaver`'s `idle_ms == 0` (that service's own
///     class note), decided by whatever wires the shell up. If this scene is nonetheless pushed with the mode at `OFF`
///     it draws nothing and asks for no timer, which is the closest thing to legacy's "never activates".
///   * **The dirty-frame protocol** (`Screensaver::consumeFrameDirty` in `hid/display/screensaver.h`, and
///     `OLED::sendMainImage`'s use of it at `hid/display/oled.cpp`): legacy's guard against
///     unrelated timers re-blitting an unchanged screensaver frame. The shell's `RepaintGate` is
///     that guard here, which is why `timer_repaints()` below claims the OLED and not the grid.
///
/// @par Why this is a `SceneKind::Screensaver` and not a `FirstApp + N`
///
/// `shell::Screensaver` pushes `SceneId{SceneKind::Screensaver, seed}` by name
/// (`shell::Screensaver::tick`). The kind is one of the shell's own six, and -- unlike
/// `Popup`/`Console`/`Working`/`Loading` -- the shell deliberately does **not** overwrite the
/// registry slot for it (`Shell::Shell`), precisely so an application can register this.
///
/// @warning **Must not raise `Action::Kind::LockInput`**, and does not -- see the warning on
///          `Action` in `shell/scene.h`. `handle()` is unreachable (`input_transparent()` is true).

#include "project/device_settings.h"
#include "scenes/bind/device_reader.h"
#include "shell/scene.h"

#include "ui/oled/rainfall.h"
#include "ui/oled/starfield.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: the shell's own `Screensaver`, not an
///        application kind. See the class doc.
inline constexpr deluge::ui::shell::SceneKind kScreensaverSceneKind = deluge::ui::shell::SceneKind::Screensaver;

/// @brief Milliseconds between animation frames, for the two animated modes.
///
/// `kFrameIntervalMS` of `hid/display/screensaver.cpp`, value for value.
inline constexpr uint32_t kFrameIntervalMs = 50;

/// @brief The idle animation. One instance for the whole application (`registry.h`).
class ScreensaverScene final : public deluge::ui::shell::Scene {
public:
	deluge::ui::shell::SceneKind kind() const override { return kScreensaverSceneKind; }

	/// @brief Activate: scatter whichever animation the mode names.
	///
	/// The port of `Screensaver::timerEvent`'s `if (!active_)` arm
	/// (`hid/display/screensaver.cpp`) -- the switch that calls `scatter()` on exactly one
	/// of the two fields and leaves `BLANK`/`OFF` alone.
	///
	/// @param id The push. `id.arg` is `shell::Screensaver`'s `seed` (its low 16 bits,
	///           `shell::Screensaver::tick`); a non-zero one reseeds the animation's generator so a
	///           spec can reproduce a field. **Zero -- `ShellConfig::seed`'s shipped default
	///           (`ShellConfig::seed`) -- reseeds nothing**, which is what makes this an exact port:
	///           legacy has no seed at all and simply lets the generator run on across activations
	///           (`Rainfall::scatter`'s own note).
	/// @return Always true.
	bool on_enter(const deluge::ui::shell::SceneId& id) override;

	/// @brief Advance one animation frame once `kFrameIntervalMs` of ticks have accumulated.
	///
	/// The port of `Screensaver::timerEvent`'s `else` arm (`hid/display/screensaver.cpp`)
	/// plus the `setTimer(..., kFrameIntervalMS)` re-arm at its end. The shell calls `tick()`
	/// every tick rather than only at a deadline (`Shell::tick_stack`), so the
	/// deadline is kept here: elapsed time accumulates and the counter is **reset to zero**, not
	/// decremented by the interval, because legacy re-arms its timer from the moment the timer
	/// fired -- a late tick is one frame there, and must be one frame here.
	/// @param dt_ms Milliseconds since the previous tick.
	void tick(uint32_t dt_ms) override;

	/// @brief Draw the current frame.
	///
	/// The port of `Screensaver::render` (`hid/display/screensaver.cpp`) and the two per-mode renderers it dispatches
	/// to (`Screensaver::renderStarfield` and `Screensaver::renderRainfall`). Legacy's leading `canvas_.clear()`
	/// (`hid/display/screensaver.cpp`) has no counterpart here because `Shell::render_oled` clears the canvas before
	/// the pass.
	/// @param role  The OLED surface being drawn.
	/// @param frame The frame to draw into.
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;

	/// @brief The screensaver routes nothing: legacy's is a canvas override, never a `UI`.
	/// @return Always `Result::unhandled()`.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;

	/// @return The milliseconds left until the next animation frame, or `std::nullopt` for a mode
	///         that has no frames (`BLANK`, `OFF`); the inhibitor re-check is `shell::Screensaver`'s.
	std::optional<uint32_t> next_timer() const override;

	/// @return `{oled=true, grid=false}` -- the screensaver never touches the pads (legacy writes only to its own OLED
	///         canvas, per the `Screensaver` class note in `hid/display/screensaver.h`), so arming the grid would be a
	///         repaint of a surface this scene does not draw.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = true, .grid = false}; }

	/// @brief **False for `Main`, true for every other role**: the screensaver replaces the main
	///        panel and nothing else.
	///
	/// Legacy renders into its own canvas and `OLED::sendMainImage` substitutes that canvas for
	/// `main` alone (`hid/display/oled.cpp`) -- no other panel is touched. Opaque on `Main`
	/// is therefore what "replaces the panel" means, and it is also what makes waking cheap: the
	/// scene beneath was never overwritten, so popping this scene is the whole of the wake
	/// (`shell/screensaver.cpp`, `hid/display/screensaver.h`).
	bool oled_transparent(deluge::ui::shell::SurfaceRole role) const override {
		return role != deluge::ui::shell::SurfaceRole::Main;
	}
	/// @brief True: this scene composites nothing on the grid, so the scene beneath must stay the
	///        grid base rather than being displaced by a layer that would render it blank.
	bool is_grid_overlay() const override { return true; }
	/// @brief True: legacy's screensaver is a canvas override, not a `UI`. Input reaches it only
	///        through `shell::Screensaver::note_activity`, which pops this scene *before* the event
	///        is lowered (`Shell::on_board_event`), exactly as legacy's `dispatchInputEvent`
	///        calls `noteActivity()` and then continues into the real UI's handler.
	bool input_transparent() const override { return true; }

	/// @brief Read the mode from @p device, or the row's default when it is null. Not owned; it must outlive the
	///        scene's use of it.
	/// @param device The host's device settings, or null.
	void set_device(const deluge::scenes::bind::DeviceSettings* device) { device_ = device; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return The mode this scene is drawing: the row as `on_enter` or the latest `tick` read it; `OFF` before the
	///         first `on_enter`.
	[[nodiscard]] deluge::project::ScreensaverMode mode() const { return mode_; }
	/// @return The rain field, for a spec comparing it against the pre-move oracle.
	const deluge::ui::oled::Rainfall& rainfall() const { return rainfall_; }
	/// @return The star field, for the same reason.
	const deluge::ui::oled::Starfield& starfield() const { return starfield_; }
	/// @return Milliseconds accumulated toward the next animation frame.
	uint32_t since_frame_ms() const { return since_frame_ms_; }
	/// @}

private:
	/// @brief Is the current mode one that redraws every frame?
	///
	/// `hid/display/screensaver.cpp`'s `isAnimated`, which is the same test its frame
	/// interval and its advance switch make.
	/// @param mode The screensaver mode.
	/// @return Whether it animates.
	static bool animated(deluge::project::ScreensaverMode mode);

	const deluge::scenes::bind::DeviceSettings* device_ = nullptr; ///< the installed port; null reads the row's default
	deluge::project::ScreensaverMode mode_ = deluge::project::ScreensaverMode::OFF; ///< the mode last read
	deluge::ui::oled::Rainfall rainfall_;
	deluge::ui::oled::Starfield starfield_;
	/// Milliseconds accumulated since the last animation frame; legacy's `TimerName::SCREENSAVER`
	/// re-arm, kept here because the shell ticks every tick rather than only at a deadline.
	uint32_t since_frame_ms_ = 0;
};

/// @brief The application's one screensaver.
/// @return The singleton.
ScreensaverScene& screensaver_scene();

} // namespace deluge::scenes::overlay
