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
/// @brief Play one synth preset as the device plays it: loaded by the blank boot song from `SYNTHS/PRESET.XML`,
/// played from a DIN MIDI note script, and rendered in real time on the audio tier. One case of the synth golden
/// masters.
///
/// Configured from the environment:
///   - `RENDER_TREE`: the card tree holding `SYNTHS/PRESET.XML` (golden_vt_render fills the card from it).
///   - `RENDER_MIDI`: the note script, `<frame> <hex bytes>` per line, frames counted from the start of capture.
///   - `RENDER_SECONDS`: how long to capture (default 2). Whole 128-frame blocks are captured, so the capture can run
///     up to a block past it.
///   - `RENDER_INPUT`: `1` feeds the audio input the harness's test signal from the start of capture (see
///     `audio/host.rs`), for presets whose sources read the input. Silence otherwise.
///   - `RENDER_PCM_OUT`: where the harness writes the captured output (see `audio/host.rs`).
///
/// Lines:
///   - `RP-LOADED name=<n> swapped=<0|1>`: the synth the blank song holds once its preset load has finished. A
///     failure unless the preset replaced the built-in default synth: a failed load leaves that synth, named `0`, in
///     place and renders just as well.
///   - `RP-MIDI events=<n> released=<n>`: the script's events, and how many of them the app read before capture
///     ended. A failure unless it read them all.
///   - `RP-RENDER blocks=<n> voices_max=<v> direness_max=<d>`: blocks rendered while capturing, the most voices
///     sounding at once, and the highest CPU direness seen. A failure if direness rose at all: direness culls voices
///     and lowers quality, and the golden masters record full-quality renders.
///   - `RP-TIER-SYNC-AUDIO total=<n>`: synchronous card calls the render made; the audio tier must make none.
///   - `RP-CAPTURE frames=<n> input=<0|1>`: frames captured, and whether the test input signal was fed.
///   - `RP-RESULT failures=<n>`.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_render_preset_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_render_preset_complete(void);

/// @brief Run one tick of the scenario.
void deluge_scenario_render_preset_dispatch_step(void);

/// @brief Whether the last dispatched tick has finished.
bool deluge_scenario_render_preset_step_done(void);

/// @brief How many in-process assertions failed.
int32_t deluge_scenario_render_preset_failures(void);

} // extern "C"
