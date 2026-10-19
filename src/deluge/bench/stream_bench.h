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
/// @brief The streaming benchmark: a device image built with DELUGE_STREAM_BENCH plays two songs from the card
/// and reports where each streaming chunk's service time went.
///
/// After boot it loads DELUGE_STREAM_BENCH_SONG_A and measures three phases, each over freshly zeroed counters:
///   - `idle`: 5 s loaded and stopped;
///   - `steady`: 30 s playing;
///   - `load-while-playing`: loading DELUGE_STREAM_BENCH_SONG_B while playing, until 15 s after its load arms its
///     song swap (or completes). A load during playback completes only at the swap, at the end of the current loop.
///
/// Each phase prints, over RTT, a line at a time:
///   - `STREAM-BENCH-PHASE phase=<p> ms=<n> underrun_wait=<n> underrun_unassign=<n> depth_mean_x100=<n>
///     depth_max=<n>`: how long it ran, the sample data playback needed and did not have, and the loader queue's
///     depth;
///   - `STREAM-BENCH-HIST phase=<p> metric=<m> unit=<us|frames> n=<n> mean=<n> max=<n> b=<counts>`: a log2
///     histogram (bucket 0 holds 0, bucket i holds [2^(i-1), 2^i)) for each of `queue_wait` and `fill`
///     (DelugeResourceStats) and `wake`, `fs_wait`, `read`, `finish`, `gap`, `pop` and `begin` (DelugeFillTrace);
///   - `STREAM-BENCH-SCANS phase=<p> resident=<calls>/<slots> ptr=<calls>/<slots> free=<calls>/<slots>
///     evict=<calls>/<slots>`: the resource manager's chunk-table lookups and the slots they visited;
///   - `STREAM-BENCH-FS phase=<p> long_waits=<i>,<s>,<a> max_us=<i>,<s>,<a>`: filesystem waits of 2 ms or more by
///     the tier that held the mutex (interaction, storage, audio);
///   - `STREAM-BENCH-JOB phase=<p> name="<job>" max_us=<n> runs=<n>`: the slowest app-loop jobs.
///
/// Along the way: `STREAM-BENCH-BEGIN first="<path>" second="<path>"`, `STREAM-BENCH-LOADED song=<name> ok=<0|1>`
/// for each completed load (a failure unless the song loaded is the one asked for: a browser that cannot find the file
/// loads the nearest one), `STREAM-BENCH-ARMED ms=<n>` when the second load arms its swap, and
/// `STREAM-BENCH-WAIT listing ...` every 5 s while a listing is outstanding, with what it waits on.
/// Then `STREAM-BENCH-DONE failures=<n>`; a stage that cannot finish prints `STREAM-BENCH-FAIL <what>` first.
/// tools/stream_bench.py boots the image and collects the lines.

namespace deluge::bench {

/// Adds the benchmark's job to the app loop.
void registerStreamBench();

/// Notes that one run of the song load's phase @p phase (LoadSongUI::LoadPhase) took @p us µs. A run of 50 ms or more
/// prints `STREAM-BENCH-LOADSTEP phase=<name> ms=<n>`.
void noteLoadStep(int phase, uint64_t us);

} // namespace deluge::bench
