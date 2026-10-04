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

#include <cstdint>
#include <cstdio>

class FileWriter;
class Song;

/// @file
/// @brief Host drivers: whole-program checks run as stepped scenarios on the booted app.
///
/// A driver is a command-line program -- `int main(int argc, char** argv)` over song files on the host filesystem --
/// that golden_vt_render runs in place of a process of its own. `GOLDEN_SCENARIO=<name>` selects it, and its command
/// line comes from the environment:
///
///     DRV_ARGS   its arguments, separated by US (0x1f); argv[0] is the scenario's name
///     DRV_OUT    a host file to write what it prints to stdout; without it, each line goes to stdout as `DRV-OUT
///     <line>` DRV_ERR    the same for stderr, as `DRV-ERR <line>` DRV_DIRECT set: both go straight to the process's
///     stdout and stderr as printed, so they survive a crash
///
/// It runs once, in one interaction-tier step, and the scenario then prints `DRV-EXIT code=<n>`: the value its main
/// returned, which is the exit status its process used to have. tests/golden/driver.py turns a run back into that
/// process's exit status, stdout and stderr, which is what the runners in tests/golden compare.
///
/// A driver loads and saves songs of its own beside the booted one, and sets currentSong to them as it goes. The step
/// restores currentSong, the serialization format, the active reader and the sample clock afterwards, so the render
/// never sees a driver's song: it does not run inside the step.
namespace deluge::scenarios::driver {

/// Where a driver writes what its process wrote to stdout and stderr.
struct Io {
	FILE* out;
	FILE* err;
};

using Main = int (*)(int argc, char** argv, Io io);

void begin();
bool complete();
void dispatch(Main main);
bool stepDone();
int32_t failures();

/// @brief Start serializing into memory through @p writer, for endCapture() to collect.
void beginCapture(FileWriter& writer);

/// @brief Copy what @p writer wrote since beginCapture() to @p buf, and hand the writer back to file writing.
/// @return The number of bytes written, or -1 when they do not fit in @p cap.
long endCapture(FileWriter& writer, char* buf, uint32_t cap);

/// @brief Save @p song into @p buf in the format songs are saved in (deluge::storage::kSongSaveFormat).
/// @return The number of bytes written, or -1 when they do not fit in @p cap.
long saveSongCapture(Song& song, char* buf, uint32_t cap);

} // namespace deluge::scenarios::driver

/// The five functions of a driver scenario named @p name, running @p mainFn. Their declarations are in
/// scenarios/drivers/driver_scenarios.h.
#define DELUGE_DRIVER_SCENARIO(name, mainFn)                                                                           \
	extern "C" void deluge_scenario_##name##_begin(void) {                                                             \
		deluge::scenarios::driver::begin();                                                                            \
	}                                                                                                                  \
	extern "C" bool deluge_scenario_##name##_complete(void) {                                                          \
		return deluge::scenarios::driver::complete();                                                                  \
	}                                                                                                                  \
	extern "C" void deluge_scenario_##name##_dispatch_step(void) {                                                     \
		deluge::scenarios::driver::dispatch(&mainFn);                                                                  \
	}                                                                                                                  \
	extern "C" bool deluge_scenario_##name##_step_done(void) {                                                         \
		return deluge::scenarios::driver::stepDone();                                                                  \
	}                                                                                                                  \
	extern "C" int32_t deluge_scenario_##name##_failures(void) {                                                       \
		return deluge::scenarios::driver::failures();                                                                  \
	}
