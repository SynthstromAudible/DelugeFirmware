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

/// @file
/// @brief The startup song's crash fail-safe.
///
/// ## What it is
///
/// A canary file written before the startup song is loaded and removed after. If the Deluge dies
/// during the load, the surviving canary tells the NEXT boot to skip that song rather than crash on
/// it again. Without it, a song that crashes on load crashes on every boot, forever.
///
/// ## Why it is a state machine
///
/// The canary must be **durably on the card before the load begins**. Under the byte channel a write
/// is requested and lands later, so the load cannot simply follow the request on the same line — it
/// has to wait for the collect to report the bytes are there. Observing that takes a later tick than
/// issuing it, which is what turns a straight-line function into phases.
///
/// That requirement is what separates this consumer from the settings family, which can fire its
/// writes and forget them: a settings toggle lost to a power cut costs one toggle, while a canary
/// lost to one costs the whole mechanism.
///
/// ## Why it lives here rather than in `deluge.cpp`
///
/// It is a self-contained crash-recovery mechanism with its own persistent state, and the phases
/// roughly doubled its size. `deluge.cpp` keeps only the task registrations.

namespace deluge::storage {

/// @brief Start the fail-safe. Does no I/O of its own; the routine does all of it.
///
/// The `startupConditionalTask` in `deluge.cpp` points here, and that indirection must stay: the
/// headless stem renderer (`src/bsp/host/host_render_main.cpp`) overrides that handle with its own
/// driver before `deluge_main()` runs.
void armStartupSong();

/// @brief Advance the fail-safe by one phase. Registered as a repeating task.
///
/// Cheap and reentrant once the machine has finished — it returns immediately from `Idle` and `Done`.
void startupSongRoutine();

/// @brief Whether the machine has finished (or was never armed).
///
/// For the gate, which must not read the tier count partway through a run -- a count read mid-machine
/// reports whatever happened to have executed rather than what the attempt does.
[[nodiscard]] bool startupSongIdle();

/// @brief Whether the machine is waiting on the canary write to land.
///
/// For the gate, which asserts the ORDERING GUARANTEE: the canary must be readable on the card by the
/// time the machine leaves this phase, because everything after it leads to the load. Observing that
/// transition is the only way to test the guarantee without a load, and this harness cannot run one
/// (`openUI` needs a UI stack it does not have).
/// @brief Whether the canary write was CONFIRMED terminal before the machine advanced past it.
///
/// The ordering guarantee stated as observable state, and the half that can be TESTED. Its companion
/// — is the canary readable on the card — proves the effect, but on a host where the channel
/// completes within a tick that check passes whether the machine waited or not. This one distinguishes
/// them: it is set only in the branch that saw a terminal answer.
[[nodiscard]] bool startupSongCanaryConfirmed();

[[nodiscard]] bool startupSongAwaitingCanary();

/// @brief The canary path for the attempt in flight, or empty when there is none.
///
/// For the gate, so it checks the same path the machine wrote rather than rebuilding the name and
/// testing its own arithmetic.
[[nodiscard]] const char* startupSongCanaryPath();

/// @brief Cleanup owed to the startup song load, run as its completion.
///
/// This used to sit on the straight line after `performLoad()` returned. The load now finishes from
/// the run loop, so the cleanup travels with it — otherwise the canary would be removed while the
/// song was still loading, and a load that then bricked the device would look like a clean boot next
/// time.
void finishStartupSong();

} // namespace deluge::storage
