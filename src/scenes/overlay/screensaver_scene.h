#pragma once

/// @file
/// @brief `ScreensaverScene` -- the idle animation: a blank panel, a starfield, or a field of
///        Deluge rain, drawn over the whole OLED after a configurable period with no input.
///
/// Ports the **drawing** half of legacy's `hid::display::Screensaver`
/// (`hid/display/screensaver.h:35-106`). The *policy* half -- when to show it and when to wake --
/// already shipped as `shell::Screensaver` (`shell/screensaver.h`), which pushes this scene by
/// kind; this scene is what that service could not find until now.
///
/// The two animations themselves moved out of `hid/display/` to `ui/oled/rainfall.h` and
/// `ui/oled/starfield.h` in the same task, because `src/scenes/**` may include neither `gui/` nor
/// `hid/` (`check_layer_rules.py`'s `scenes-includes-gui` row). Legacy's `Screensaver` still draws
/// the same two objects, now through the moved symbols; see `ui/oled/rainfall.h`'s file doc.
///
/// @par What chooses the mode, and where legacy chooses it
///
/// `FlashStorage::screensaverMode` (`storage/flash_storage.h:82`), the same global legacy reads --
/// and read **live**, on every question, exactly as legacy does: `Screensaver::timerEvent` reads it
/// at `hid/display/screensaver.cpp:125` (the OFF early-out), `:150` (the activation switch that
/// scatters), `:163` (the per-frame switch that advances) and `:185` (the frame interval), and
/// `Screensaver::render` reads it again at `:235` (the switch that draws). Nothing here snapshots
/// it at `on_enter`, because legacy does not: a mode edit while the screensaver is showing is
/// legacy's `settingsChanged()` path (ported by F1 task 8 as `shell::Screensaver::set_idle_ms`, and
/// see the gaps below for what of it reaches this scene), and outside that path the remaining
/// behaviour is whatever the live read produces -- which is what legacy would do too.
///
/// @par The four legacy behaviours that have no port here, and why
///
/// Each is policy, and policy is `shell::Screensaver`'s half; none is dropped silently.
///
///   * **`settingsChanged()`** (`screensaver.cpp:188-229`): wake, then re-arm or cancel for the new
///     settings. **Ported by F1 task 8** -- not here, where it never belonged, but as
///     `shell::Screensaver::set_idle_ms` (`shell/screensaver.h`), which is that reconfiguration, and
///     `Shell::set_screensaver_idle_ms`, which is how an application reaches it. Legacy's own
///     callers are `deluge_boot` and the two settings items
///     (`gui/menu_item/defaults/screensaver.h`), and they all funnel through `settingsChanged()`,
///     so the port is pushed from there. The residue is the *ported* menu's two setters
///     (`menu/nodes/settings.cpp`), which write the same globals and cannot call `hid/`; see
///     `scenes/registry.h`'s boot-wiring record.
///   * **The inhibitors** (`screensaver.cpp:129-145`): a permanent popup, the working animation or
///     a running stem export wakes the screensaver and re-arms, re-checked every tick. The port's
///     popup/working scenes are the shell's own (`Notices`, `shell/shell.h:123-129`) and stack
///     *above* whatever is showing, so the question "is one present" is the shell's to ask, not
///     this scene's. This is the reason `kInhibitRecheckMS`'s 1000 ms re-arm has no port either --
///     it exists only to run that check (`screensaver.cpp:51-54`'s own note says so), so a blank
///     screensaver here reports no timer at all rather than waking the shell every second to do
///     nothing. Recorded as a gap; a `shell::Screensaver` that learns about the notices would
///     restore it.
///
///     **That re-arm costs one more thing than the inhibitor check**, and it is the only place the
///     port's observable timing differs from legacy's: legacy's 1000 ms tick also re-reads the mode
///     (`:140` and `:162`), so a `BLANK` -> `STARSCAPE`/`DELUGE` change made while the screensaver
///     is showing starts animating within a second, where this scene -- reporting no timer -- stays
///     blank for ever. It is unreachable in practice rather than merely unlikely: the only way to
///     change the setting is the menu (`gui/menu_item/defaults/screensaver.h:34-41`), reaching it
///     needs physical input, and input runs `shell::Screensaver::note_activity` first, which pops
///     this scene (`shell/screensaver.cpp:9-17`) -- so the mode cannot change while it is up. The
///     next push re-reads the mode and scatters the right field.
///   * **`ScreensaverMode::OFF`** (`screensaver.cpp:93-95,125-127,223-226`): the port of "off" is
///     `shell::Screensaver`'s `idle_ms == 0` (that service's own class note), decided by whatever
///     wires the shell up. If this scene is nonetheless pushed with the mode at `OFF` it draws
///     nothing and asks for no timer, which is the closest thing to legacy's "never activates".
///   * **The dirty-frame protocol** (`screensaver.h:95-109`, `Screensaver::consumeFrameDirty`, and
///     `OLED::sendMainImage`'s use of it at `hid/display/oled.cpp:342-362`): legacy's guard against
///     unrelated timers re-blitting an unchanged screensaver frame. The shell's `RepaintGate` is
///     that guard here, which is why `timer_repaints()` below claims the OLED and not the grid.
///
/// @par Why this is a `SceneKind::Screensaver` and not a `FirstApp + N`
///
/// `shell::Screensaver` pushes `SceneId{SceneKind::Screensaver, seed}` by name
/// (`shell/screensaver.cpp:60`). The kind is one of the shell's own six, and -- unlike
/// `Popup`/`Console`/`Working`/`Loading` -- the shell deliberately does **not** overwrite the
/// registry slot for it (`shell/shell.cpp:177-184`), precisely so an application can register this.
///
/// @warning **Must not raise `Action::Kind::LockInput`**, and does not -- see the warning on
///          `Action` in `shell/scene.h`. `handle()` is unreachable (`input_transparent()` is true).

#include "shell/scene.h"

#include "ui/oled/rainfall.h"
#include "ui/oled/starfield.h"

#include "definitions_cxx.hpp"

#include <cstdint>
#include <optional>

namespace deluge::scenes::overlay {

/// @brief The `SceneKind` this scene is registered under: the shell's own `Screensaver`, not an
///        application kind. See the class doc.
inline constexpr deluge::ui::shell::SceneKind kScreensaverSceneKind = deluge::ui::shell::SceneKind::Screensaver;

/// @brief Milliseconds between animation frames, for the two animated modes.
///
/// `hid/display/screensaver.cpp:47-49`'s `kFrameIntervalMS`, value for value.
inline constexpr uint32_t kFrameIntervalMs = 50;

/// @brief The idle animation. One instance for the whole application (`registry.h`).
class ScreensaverScene final : public deluge::ui::shell::Scene {
public:
	deluge::ui::shell::SceneKind kind() const override { return kScreensaverSceneKind; }

	/// @brief Activate: scatter whichever animation the mode names.
	///
	/// The port of `Screensaver::timerEvent`'s `if (!active_)` arm
	/// (`hid/display/screensaver.cpp:147-161`) -- the switch that calls `scatter()` on exactly one
	/// of the two fields and leaves `BLANK`/`OFF` alone.
	///
	/// @param id The push. `id.arg` is `shell::Screensaver`'s `seed` (its low 16 bits,
	///           `shell/screensaver.cpp:60`); a non-zero one reseeds the animation's generator so a
	///           spec can reproduce a field. **Zero -- `ShellConfig::seed`'s shipped default
	///           (`shell/shell.h:46`) -- reseeds nothing**, which is what makes this an exact port:
	///           legacy has no seed at all and simply lets the generator run on across activations
	///           (`Rainfall::scatter`'s own note).
	/// @return Always true.
	bool on_enter(const deluge::ui::shell::SceneId& id) override;

	/// @brief Advance one animation frame once `kFrameIntervalMs` of ticks have accumulated.
	///
	/// The port of `Screensaver::timerEvent`'s `else` arm (`hid/display/screensaver.cpp:162-177`)
	/// plus the `setTimer(..., kFrameIntervalMS)` re-arm at `:161-162`. The shell calls `tick()`
	/// every tick rather than only at a deadline (`Shell::tick_stack`, `shell.cpp:553-563`), so the
	/// deadline is kept here: elapsed time accumulates and the counter is **reset to zero**, not
	/// decremented by the interval, because legacy re-arms its timer from the moment the timer
	/// fired -- a late tick is one frame there, and must be one frame here.
	/// @param dt_ms Milliseconds since the previous tick.
	void tick(uint32_t dt_ms) override;

	/// @brief Draw the current frame.
	///
	/// The port of `Screensaver::render` (`hid/display/screensaver.cpp:231-245`) and the two
	/// per-mode renderers it dispatches to (`:225-248`). Legacy's leading `canvas_.clear()`
	/// (`:210`) has no counterpart here because `Shell::render_oled` clears the canvas before the
	/// pass (`shell.cpp:642`).
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;

	/// @brief The screensaver routes nothing: legacy's is a canvas override, never a `UI`.
	/// @return Always `Result::unhandled()`.
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;

	/// @return The milliseconds left until the next animation frame, or `std::nullopt` for a mode
	///         that has no frames (`BLANK`, `OFF`) -- see the class doc on why legacy's 1000 ms
	///         blank re-check has no port.
	std::optional<uint32_t> next_timer() const override;

	/// @return `{oled=true, grid=false}` -- the screensaver never touches the pads (legacy writes
	///         only to its own OLED canvas, `hid/display/screensaver.h:30-34`), so arming the grid
	///         would be a repaint of a surface this scene does not draw.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = true, .grid = false}; }

	/// @brief **False for `Main`, true for every other role**: the screensaver replaces the main
	///        panel and nothing else.
	///
	/// Legacy renders into its own canvas and `OLED::sendMainImage` substitutes that canvas for
	/// `main` alone (`hid/display/oled.cpp:342-348`) -- no other panel is touched. Opaque on `Main`
	/// is therefore what "replaces the panel" means, and it is also what makes waking cheap: the
	/// scene beneath was never overwritten, so popping this scene is the whole of the wake
	/// (`shell/screensaver.cpp:10-17`, `hid/display/screensaver.h:30-34`).
	bool oled_transparent(deluge::ui::shell::SurfaceRole role) const override {
		return role != deluge::ui::shell::SurfaceRole::Main;
	}
	/// @brief True: this scene composites nothing on the grid, so the scene beneath must stay the
	///        grid base rather than being displaced by a layer that would render it blank.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: legacy dims no pad while the screensaver shows -- it never touches `PadLEDs`
	///        at all -- so a grid this scene does not draw must not be halved
	///        (`shell/scene.h`'s `dims_grid` note).
	bool dims_grid() const override { return false; }
	/// @brief True: legacy's screensaver is a canvas override, not a `UI`. Input reaches it only
	///        through `shell::Screensaver::note_activity`, which pops this scene *before* the event
	///        is lowered (`shell/shell.cpp:248-250`), exactly as legacy's `dispatchInputEvent`
	///        calls `noteActivity()` and then continues into the real UI's handler.
	bool input_transparent() const override { return true; }

	/// @name Observable state
	/// For specs.
	/// @{
	/// @return The mode this scene is currently drawing, read live from `FlashStorage`.
	::ScreensaverMode mode() const;
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
	/// `hid/display/screensaver.cpp:59-62`'s `isAnimated`, which is the same test its `:162` frame
	/// interval and its `:140` advance switch make.
	static bool animated(::ScreensaverMode mode);

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
