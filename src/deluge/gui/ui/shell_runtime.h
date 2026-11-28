/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "gui/ui/shell_clock.h"
#include "gui/ui/shell_guest_surfaces.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include <cstdint>

class UI;

namespace deluge::gui::ui {

/// @brief The firmware's one shell: its surfaces, its clock, its tick and its timer, shared by every legacy `UI`
///        that hosts scenes (the guest, `ShellHostUI`, and the root views whose body is a scene).
///
/// A host owns none of these. It feeds input into `shell()`, calls `tick` from its `timerCallback`, and re-arms with
/// `rearm`; the runtime keeps the one stack, so a menu opened over a root scene is the same shell's next scene.
///
/// @par The unit mismatch: an absolute deadline against a duration
///
/// `Shell::next_wake()` is an **absolute** millisecond deadline, recomputed at the bottom of every
/// tick. `UITimerManager::setTimer` takes a **duration**
/// and converts it to audio samples with `ms * 44` (`gui/ui_timer_manager.cpp`). The
/// conversion is therefore `in = (int32_t)(deadline - now)`, in the wrap-safe difference form
/// `Shell::compute_next_wake` and `RepaintGate` both use -- a plain `<` across the ~49.7-day `uint32_t` wrap picks the
/// wrong one of two straddling deadlines -- and then clamped to `kMinRearmMs .. kMaxRearmMs`.
///
///  - **A deadline already past** (`in <= 0`) arms `kMinRearmMs`, not zero. It is not an error: the
///    shell asks for `now_ms` routinely (a router repaint armed at a zero-millisecond deadline),
///    and there is nothing to catch up on -- the tick that just ran was
///    the one it was waiting for.
///  - **Arming earlier than asked is always safe; arming later is not.** The next tick recomputes
///    `next_wake()` from scratch, so an early wake costs one tick and corrects itself. That is why
///    the clamp is one-sided in its consequences and why `kMaxRearmMs` may exist at all.
///
/// @par Why the floor is 1 ms and not 0 -- a 0 floor is a busy loop
///
/// `Scene::next_timer()` may legally return **0**, meaning "next tick" (`Scene::next_timer`), and
/// `Shell::next_wake()` reports `now_ms` routinely (a router repaint, a fact raised mid-tick). A
/// per-frame animation is not one of those: it is `Scene::frame_driven`, woken at the frame period.
/// The shell only ticks when its host asks it to, and **this class arms that ask**, so the floor is here.
///
/// `setTimer(UI_SPECIFIC, 0)` would put `triggerTime` at exactly `AudioEngine::audioSampleTimer`
/// (`UITimerManager::setTimerSamples` in `gui/ui_timer_manager.cpp`) and `timeNextEvent` with it. **`kMinRearmMs` (1 ms
/// = 44 samples) is the smallest duration `setTimer`'s `ms * 44` can express that leaves `triggerTime` strictly in the
/// future**, and that is the whole of the argument: with zero, `triggerTime` is already behind the audio sample timer
/// by the time `UITimerManager::routine` is next called at all -- audio advances at 44.1 kHz, far faster than the main
/// loop the routine is called from -- so `timeNextEvent` is past, the early return there (`gui/ui_timer_manager.cpp`)
/// does not fire, and the timer's own due test in `UITimerManager::routine` is true. The whole timer sweep and the
/// shell's whole tick would then run on **every** pass of the main loop, for as long as a host kept re-arming at
/// zero. With 44 samples they run at most once a millisecond.
///
/// 1 ms rather than the 15 ms graphics routine, because a larger floor would put its latency on the repaint after
/// *every* button press, and legacy has none there: `doAnyPendingUIRendering` runs from the main loop rather than
/// from a timer (`gui/ui/ui.cpp`). Frame animations are paced by `ShellConfig::frame_ms`, not by this floor.
///
/// @note Precisely at the instant of arming, `timeNextEvent - audioSampleTimer` is **0**, and
///       `routine`'s guard is `>= 0`, so the early return *is* taken and the timer does not fire
///       that instant -- it becomes due one audio sample (~22.7 us) later. A zero arm does not
///       "defeat the early return"; the conclusion rests on the sample timer advancing between two
///       calls of `routine`, not on the guard being wrong.
class ShellRuntime {
public:
	/// @brief Build the shell over @p board and @p registry.
	/// @param registry The application's scene table. `Shell` copies the pointers out of it into its own table, so a
	///                 temporary is fine.
	/// @param board    Board capability descriptor; the real Deluge unless a spec says otherwise.
	/// @param profile  The matching board profile.
	explicit ShellRuntime(const deluge::ui::shell::SceneRegistry& registry,
	                      const DelugeBoard& board = deluge::ui::shell::kDelugeBoard,
	                      const deluge::ui::shell::BoardProfile& profile = deluge::ui::shell::deluge_profile());
	ShellRuntime(const ShellRuntime&) = delete;
	ShellRuntime& operator=(const ShellRuntime&) = delete;

	/// @brief The shell; the input lowering, the services and the stack are reached through here.
	[[nodiscard]] deluge::ui::shell::Shell& shell() { return shell_; }
	/// @brief The surfaces the shell renders through.
	[[nodiscard]] GuestSurfaceSet& surfaces() { return surfaces_; }
	/// @copydoc surfaces()
	[[nodiscard]] const GuestSurfaceSet& surfaces() const { return surfaces_; }
	/// @brief The config the shell was built from; `edges_per_detent` is read back out of it.
	[[nodiscard]] const deluge::ui::shell::ShellConfig& config() const { return config_; }

	/// @name The timer's two clamps
	/// @{
	/// @brief What `UITimerManager::setTimer` multiplies a millisecond duration by
	///        (`gui/ui_timer_manager.cpp`).
	static constexpr int32_t kTimerSamplesPerMs = SampleClockMs::kSamplesPerMs;
	/// @brief The shortest re-arm a host will ask for. See `@par Why the floor is 1 ms`.
	static constexpr int32_t kMinRearmMs = 1;
	/// @brief The longest, and it is arithmetic rather than policy: `setTimer` computes `ms * 44`
	///        into an `int32_t`, so anything above this is signed overflow. A deadline further out
	///        than ~13.6 hours is woken early instead, which is always safe (see `@par The unit
	///        mismatch`).
	static constexpr int32_t kMaxRearmMs = 2147483647 / kTimerSamplesPerMs;
	/// @}

	/// @brief The shell's clock, in milliseconds, for every hook that has to stamp one, and for
	///        host code that reads a scene's clock-driven state between ticks.
	///
	/// It is legacy's UI clock: `AudioEngine::audioSampleTimer` at 44 samples a millisecond, the count
	/// `UITimerManager::setTimer` arms legacy's timers in and the shell's own wake (`UI_SPECIFIC`) with
	/// them. On one base, a blink the shell times and one legacy times keep their phase, and both stop
	/// when the audio stops, as legacy's do. A wall clock is forbidden inside `src/ui/shell/**` and
	/// `src/ui/**` (`shell-no-clock`); supplying one is exactly what a host is for.
	///
	/// @return Milliseconds since start, wrapping every ~49.7 days, carried on across the counter's own
	///         wrap (`SampleClockMs`). The shell's own arithmetic is unsigned differences throughout, so
	///         the wrap costs one interval, not correctness.
	[[nodiscard]] uint32_t nowMs() const;
	/// @brief The clock `main` times an encoder's acceleration on: the scheduler's seconds (`getSystemTime()`), as
	///        `main`'s `ContinuousEncoder::calcNextKnobSpeed` and `HorizontalMenu::calcNextKnobSpeed` read them, in
	///        milliseconds. The gold knobs are timed by the board instead (`ContinuousEncoder::actionTurns`).
	///        Legacy keeps the two apart, and so does the shell (`UiContext::acceleration_clock_ms`).
	/// @return Milliseconds since the scheduler started; 0 before it has.
	[[nodiscard]] static uint32_t accelerationClockMs();

	/// @brief Run one shell tick for @p owner: the screensaver's legacy-side inhibitors, the acceleration clock, the
	///        notice queue while @p owner is the current UI, then `Shell::tick`.
	/// @param now_ms The clock now. @param owner The `UI` whose timer is running the tick.
	void tick(uint32_t now_ms, const ::UI* owner);
	/// @brief Whether a tick is running now; a stack reset from inside one would pull the stack from under it.
	[[nodiscard]] bool ticking() const { return ticking_; }

	/// @brief Re-arm `TimerName::UI_SPECIFIC` from the shell's own deadline, while @p owner is the current UI.
	///
	/// `UI_SPECIFIC` is the current UI's timer: `UITimerManager::routine` dispatches it to `getCurrentUI()`, so an
	/// arm while something else is current would deliver this shell's tick to a stranger. Called right after `tick`,
	/// because `Shell::next_wake()` is only authoritative immediately after a tick recomputed it. A `std::nullopt`
	/// deadline arms nothing ("sleep until the next input"), except while the lowering holds the SD-routine lock,
	/// whose deferred queue waits on a fact no scene can name, so the shell keeps polling. A slicer load out polls
	/// too (`kShellSlicerPollMs`).
	/// @param now_ms The clock the tick just ran on. @param owner The host that ran it.
	void rearm(uint32_t now_ms, const ::UI* owner);
	/// @brief Ask for a tick at the earliest moment legacy can express.
	///
	/// For every path that changes the shell's state from *outside* a tick: `Shell::next_wake()` is stale there, so
	/// the only correct answer is "soon". Arming earlier than necessary costs one tick; not arming at all freezes the
	/// shell until something else happens to.
	static void armTickSoon();

private:
	/// The milliseconds `nowMs()` counts from the audio-sample counter; a reading moves it on, hence `mutable`.
	mutable SampleClockMs clock_;
	/// Declared before `shell_`, which copies it.
	deluge::ui::shell::ShellConfig config_{.greys_in_send_path = true,
	                                       .frame_ms = deluge::ui::shell::kGraphicsRoutineMs};
	GuestSurfaceSet surfaces_;
	deluge::ui::shell::Shell shell_;
	bool ticking_ = false;
};

/// @brief The firmware's one shell runtime.
/// @return The runtime, built over the scenes' registry and the Deluge board.
[[nodiscard]] ShellRuntime& shellRuntime();

} // namespace deluge::gui::ui
