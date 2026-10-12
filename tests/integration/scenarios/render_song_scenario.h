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
///   - `RENDER_PCM_OUT`: where the harness writes the captured output (see `audio/host.rs`).
///   - `RENDER_FILL_DELAY_MS`, `RENDER_FILL_READ_US`: the storage model while playing -- how long the fill task waits
///   to
///     be scheduled, and how long each chunk's read takes. Both 0 by default.
///   - `RENDER_FILL_BURST`: `<start block>:<blocks>:<ms>`, a window of playback in which the fill task waits `ms`
///     instead: a song load or a large listing competing for the storage tier.
///   - `RENDER_STEREO`: `1` plugs headphones in from the scenario's start, so the app renders in stereo as the device
///     does through them (ping-pong delay alternates, panning applies). Mono otherwise, as through the device's
///     speaker.
///   - `RENDER_DELAY_SWITCH`: `<block>,<block>,...`, ascending: at each block, counted from playback's start, every
///     audio track's delay switches between analog and digital by the Edit a press of the gold knob on its delay
///     makes. None by default.
///   - `RENDER_MODFX_SWITCH`: `<block>:<type>[:<owner>],...`, ascending, each type as a file writes it: at each block,
///     counted from playback's start, the mod FX type of every audio track, or of the owner named (`master`, or the
///     outputs of that name: a synth, a kit, an audio track), is set to it by the Edit the mod FX type menu makes. None
///     by default.
///   - `RENDER_MODFX_CLEAR`: `<block>[:<owner>],...`, ascending: at each block, the mod FX memory of every audio
///     track, or of the owner named as for the switch, is cleared by ModControllableAudio::clearModFXMemory, as a
///     sound's cut tail clears it (a chain's grain only once the render has made its processor). None by default.
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
///   - `RS-DELAY-OFF silenced=<n>`: blocks in which a delay that is OFF had feedback enough to sound and sound coming
///     in (deluge::effects::delay_off_silenced_blocks); the check requires none.
///   - `RS-DELAY-ARENA shortfalls=<n> regions=<r> jobs=<j>`: tapes the render could not take from the delay arena
///     (the check requires none), the regions it holds at the end, and how often its job maintained it.
///   - `RS-TEMPO block=<b> bpm=<t>`: the song's tempo set at block @c b (RENDER_TEMPO).
///   - `RS-DELAY-SWITCH block=<b> analog=<0|1>`: one audio track's delay switched at block @c b, and its mode after.
///     Then, after the run, `RS-DELAY-SWITCH switches=<n> asked=<m> tracks=<k>`: a failure unless every switch asked
///     for switched exactly one audio track's delay.
///   - `RS-MODFX-SWITCH block=<b> type=<t> ring=<0|1> owner=<o>`: one owner's mod FX set to type @c t at block @c b,
///     and whether it holds a ring after. After the run, `RS-MODFX-SWITCH applied=<n> asked=<m> types=<t,...> ring=<r>
///     noring=<k>`: the track switches made, the switches asked for, the type each set in order, and how many left the
///     track holding a ring and how many holding none; a failure unless each switch set exactly one track's.
///   - `RS-MODFX-CLEAR block=<b> type=<t> ring=<0|1> owner=<o>`: one owner's comb mod FX memory cleared at block
///     @c b, its type @c t, and whether it held a ring, which the clear zeroes (otherwise it zeroes the phaser's
///     memories); `RS-MODFX-CLEAR block=<b> type=grainFX grain=1 owner=<o>` for a grain's. After the run,
///     `RS-MODFX-CLEAR applied=<n> asked=<m> ring=<r> noring=<k> grain=<g>`: the track clears made, the clears asked
///     for, how many comb clears cleared a ring and how many none, and how many cleared a grain; a failure unless each
///     clear cleared exactly one track's.
///   - `RS-CAPTURE frames=<n> stereo=<0|1>`: frames captured, and whether headphones were plugged in. A failure if
///     the app rendered in the other width at any tick while playing.
///   - `RS-MODEL fill_delay_ms=<n> fill_read_us=<n> burst=<start>:<blocks>:<ms>`: the storage model it ran under.
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
