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

/// @brief Host-only C-ABI bridge for the streaming-underrun harness (the Rust/Embassy
///        `host_app` BSP, `src/bsp/rust/src/scenario.rs`).
///
/// Lets the harness drive a REAL song load, REAL playback, and a REAL concurrent output
/// recording headlessly — the exact application entry points a user's LOAD/PLAY/RECORD
/// button presses reach (LoadSongUI, PlaybackHandler, AudioRecorder), minus the button/HID
/// layer. None of it changes their behaviour; every function is a thin, direct call into
/// (or dispatch onto the storage owner of) existing production code.
///
/// @note `DELUGE_HOST`-only (see sim/CMakeLists.txt's `add_compile_definitions(DELUGE_HOST)`):
///       compiled into every x86 build off this tree (deluge_host/deluge_render/deluge_loadcheck,
///       and the Rust Embassy `host_app`), never into the ARM device firmware. Unreachable —
///       and so behaviourally inert — from deluge_host/deluge_render/deluge_loadcheck's own
///       drivers; only the Rust harness calls these.
///
/// ## Why two-phase song load
/// On the Embassy BSP, `Browser::beginListing` (which `openUI(&loadSongUI)` triggers)
/// dispatches the directory listing onto the single storage-owner fiber ASYNCHRONOUSLY —
/// unlike the legacy/cooperative host_bsp (`host_render_main.cpp`'s `deluge_render_driver`),
/// where every `Owner::run` dispatch happens to run inline. So the harness must poll for the
/// listing to finish before committing the load, exactly as a real LOAD-button UI flow does
/// (open browser → listing completes asynchronously → user presses LOAD → the load itself
/// dispatches as a SEPARATE fiber job). See `deluge_scenario_begin_song_load` /
/// `deluge_scenario_song_listing_in_progress` / `deluge_scenario_commit_song_load` below.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

/// @brief Point `currentSong` at @p full_path and open the load browser (mirrors
///        `host_render_main.cpp`'s headless load sequence).
///
/// Only DISPATCHES the async directory listing (`Browser::beginListing`) onto the
/// storage-owner fiber — poll deluge_scenario_song_listing_in_progress() before calling
/// deluge_scenario_commit_song_load().
/// @param full_path Song path (e.g. `"SONGS/Cordae.XML"` — same shape
///                   `Song::setSongFullPath` expects).
/// @return False only if `currentSong` isn't set yet (boot hasn't reached
///         `deluge_boot()`'s `setupBlankSong()`).
bool deluge_scenario_begin_song_load(const char* full_path);

/// @brief Whether the async listing dispatched by deluge_scenario_begin_song_load() is
///        still in flight.
/// @return True while the listing is in progress.
bool deluge_scenario_song_listing_in_progress();

/// @brief Dispatch deluge_scenario_begin_song_load() itself onto the storage-owner
///        worker fiber, rather than calling it inline.
///
/// For a caller running OFF the worker fiber (this scenario's own Rust task, unlike
/// the streaming-underrun harness's `scenario.rs`, which happens to only ever reach
/// this from a safe context): `openUI(&loadSongUI)`'s `opened()` chain does a real
/// storage read (`FavouritesManager::loadFavouritesBank` ->
/// `StorageManager::fileExists`) before the async listing dispatch even happens.
/// Reached off-fiber under `sim_latency`, that read can livelock the whole process
/// (a non-yielding busy-loop that can never let the `Timer`-firing task it's
/// waiting on get polled). Poll deluge_scenario_song_load_begin_done() for
/// completion, then deluge_scenario_song_load_begin_ok() for the result (the same
/// value deluge_scenario_begin_song_load() itself would have returned).
/// @param full_path Song path (e.g. `"SONGS/Cordae.XML"`).
void deluge_scenario_start_song_load(const char* full_path);

/// @brief Whether the dispatch started by deluge_scenario_start_song_load() has run.
/// @return True once the dispatched call has run.
bool deluge_scenario_song_load_begin_done();

/// @brief Once deluge_scenario_song_load_begin_done() is true, the result
///        deluge_scenario_begin_song_load() itself returned.
/// @return The result deluge_scenario_begin_song_load() would have returned.
bool deluge_scenario_song_load_begin_ok();

/// @brief Commit the load: dispatch `LoadSongUI::performLoad()` onto the storage-owner
///        fiber.
///
/// Mirrors `LoadSongUI::enterKeyPress()`'s non-folder branch (same dispatch, same
/// callback), minus its `LASTOPENED` settings-file write (irrelevant to the harness).
/// `performLoad()` itself yields internally (waiting for essential-sample clusters); poll
/// deluge_scenario_song_load_in_progress() for completion.
/// @return False if the owner queue rejected the dispatch (never actually ran).
bool deluge_scenario_commit_song_load();

/// @brief Whether a load committed by deluge_scenario_commit_song_load() is still running.
/// @return `LoadSongUI::isLoadingSong()`.
bool deluge_scenario_song_load_in_progress();

/// @brief Switch to the arrangement view and press PLAY (`PlaybackHandler::playButtonPressed`,
///        internal-clock branch).
///
/// Starts real-time arrangement playback of the current song, which is what drives the
/// real sample-streaming loader.
void deluge_scenario_start_playback();

/// @brief Whether playback (either clock) is active.
/// @return `PlaybackHandler::isEitherClockActive()`.
bool deluge_scenario_playback_active();

/// @brief Start a real-time recording of the master output to a WAV file on the card
///        (`AudioRecorder::beginOutputRecording` — the same mechanism a RECORD button
///        press with nothing armed starts).
///
/// Runs concurrently with playback/streaming.
/// @return Whether the recording was started successfully.
bool deluge_scenario_start_recording();

/// @brief Debug-only: the raw `currentUIMode` bitmask (`gui/ui/ui.h`/`ui.cpp`'s
///        `currentUIMode` global).
///
/// Lets a harness stuck waiting on deluge_scenario_song_load_in_progress() distinguish
/// which phase of `LoadSongUI::performLoad()` it's parked in (e.g.
/// `UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES` vs. `..._UNESSENTIAL_SAMPLES_ARMED`) without a
/// debugger attach. Not part of the scenario's normal control flow.
/// @return The current `currentUIMode` bitmask.
uint32_t deluge_scenario_debug_ui_mode();

/// @brief Debug-only: finer-grained load-state bits than deluge_scenario_debug_ui_mode().
///
/// Distinguishes "still genuinely loading" from "the UI object just hasn't been
/// reassigned yet" while a harness is stuck waiting for load completion.
/// @return Bit 0 = `LoadSongUI::isLoadingSong()` itself; bit 1 =
///         `getCurrentUI() == &loadSongUI` (whether the UI has transitioned away from the
///         load screen yet).
uint32_t deluge_scenario_debug_load_state();

/// @brief Dispatch a full offline `StemExport` run onto the storage-owner worker
///        fiber (mirrors `host_render_main.cpp`'s `deluge_render_driver`: sets
///        `renderOffline`/`exportToSilence`, then `StemExport::startStemExportProcess`).
///
/// Returns immediately — the export itself runs later (and, once dispatched, to
/// completion in one synchronous fiber op, since `StemExport::renderWait` drives
/// `AudioEngine::routine()` directly in an offline render rather than yielding).
/// Poll deluge_scenario_stem_export_done() for completion.
/// @param mode `StemExportType` (CLIP=0, TRACK=1, DRUM=2, MIXDOWN=3 — see
///             `definitions_cxx.hpp`), carried as `int32_t` per the FFI fixed-width
///             convention rather than the C++ enum type directly.
void deluge_scenario_start_stem_export(int32_t mode);

/// @brief Whether the export dispatched by deluge_scenario_start_stem_export() has
///        finished.
///
/// Deliberately NOT implemented as `!stemExport.processStarted`: that flag reads
/// false both BEFORE the dispatched closure has run at all (the owner queue hasn't
/// drained it yet) and AFTER it has finished — indistinguishable from here, and the
/// two states straddle this function's very first call (immediately after
/// deluge_scenario_start_stem_export() returns, the dispatch is still merely
/// queued). Instead, a private latch is set only once the dispatched closure
/// itself returns — see the .cpp.
/// @return True once the export has fully completed (or aborted).
bool deluge_scenario_stem_export_done();

/// @brief Copy every `.WAV` the just-finished export wrote (`StemExport::lastExportFolderPath`) out of the
///        mounted card image to a real host directory, then remove them and that folder from the image.
///
/// The removal keeps the cached, shared card image from filling up across runs, and keeps every render of a
/// song landing in the same unnumbered folder.
///
/// Mirrors `host_render_main.cpp`'s `copy_stems_out`: reads each stem via the C
/// FatFS API (the same one `StemExport`/`AudioRecorder` wrote through) and writes
/// it out with the host libc file API. Call only after
/// deluge_scenario_stem_export_done() reports true.
/// @param out_dir Host directory to copy the stems into (created if missing).
void deluge_scenario_copy_stems_out(const char* out_dir);

/// @brief Dispatch the SampleRecorder byte-exact round-trip oracle
///        (`deluge_scenario_run_recorder_roundtrip`, harness/recorder_roundtrip_scenario.cpp)
///        onto the storage-owner worker fiber.
///
/// Returns immediately — the oracle runs later on the fiber (its file I/O + the finalized
/// probe's async fill drain need that context). Poll deluge_scenario_recorder_roundtrip_done(),
/// then read deluge_scenario_recorder_roundtrip_failures(). Assumes an empty formatted
/// `DELUGE_SD_IMAGE` is mounted and `DELUGE_SD_ROOT` is UNSET (so the finalized probe reads
/// efatfs on the same image the recorder wrote, no POSIX mirror).
void deluge_scenario_start_recorder_roundtrip();

/// @brief Whether the round-trip dispatched by deluge_scenario_start_recorder_roundtrip() has
///        finished (its worker-fiber closure returned).
/// @return True once the dispatched closure has returned.
bool deluge_scenario_recorder_roundtrip_done();

/// @brief Once deluge_scenario_recorder_roundtrip_done() is true, the oracle's failure count
///        (0 = all cases passed). -1 until the dispatched closure has set it.
/// @return The oracle's failure count, or -1 if not yet available.
int32_t deluge_scenario_recorder_roundtrip_failures();

} // extern "C"

#endif // DELUGE_HOST
