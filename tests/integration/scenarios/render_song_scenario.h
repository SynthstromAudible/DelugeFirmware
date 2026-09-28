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

/// @file
/// @brief Play a song as the device plays it: loaded through the real load browser, rendered in real time on the
/// audio tier, and reported with the evidence that each of those really happened.
///
/// Configured from the environment, so each gate is a registry entry:
///   - `RENDER_SONG`: the song's card path (required).
///   - `RENDER_BLOCKS`: the 128-frame blocks to render after playback starts (default 3000, about 8.7 s).
///   - `RENDER_PCM_OUT`: where the harness writes the captured output (see `audio_host.rs`).
///
/// Lines:
///   - `RS-LOADED name=<n> outputs=<k> view=<v> playing_clips=<c> notes=<m>`: the song that loaded, the view it
///     opened in (arrangement or session playback follows it), and the session clips set to play and their notes. A
///     failure unless the song loaded is the one asked for: a load that gives up leaves the previous song in place, and
///     a browser that cannot find the file loads the nearest one instead; either plays just as well.
///   - `RS-RENDER blocks=<n> stops=<k> first_stop=<b> voices_max=<v> direness_max=<d>`: blocks rendered since
///     playback started; how many times playback stopped, and at which block first (a failure if it stopped at all);
///     the most voices sounding at once; and the highest CPU direness seen, which culls voices when it rises.
///   - `RS-VOICES audio=<n> storage=<n> interaction=<n>`: sample-voice renders by tier during playback.
///   - `RS-TIER-SYNC-AUDIO total=<n>`: synchronous card calls the render made; the audio tier must make none.
///   - `RS-STREAM reads=<n>`: card reads while playing, which is the sample data streaming in.
///   - `RS-UNDERRUN wait=<n> unassign=<n>`: sample data playback needed and did not have.
///   - `RS-CAPTURE frames=<n>`: frames captured.
///   - `RS-RESULT failures=<n>`.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_render_song_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_render_song_complete(void);

/// @brief Run one tick of the scenario.
void deluge_scenario_render_song_dispatch_step(void);

/// @brief Whether the last dispatched tick has finished.
bool deluge_scenario_render_song_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_render_song_failures(void);

} // extern "C"
