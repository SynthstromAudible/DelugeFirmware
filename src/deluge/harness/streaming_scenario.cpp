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

#include "harness/streaming_scenario.h"

#ifdef DELUGE_HOST

#include "harness/recorder_roundtrip_scenario.h" // deluge_scenario_run_recorder_roundtrip

#include "definitions_cxx.hpp"
#include "gui/ui/audio_recorder.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "io/file.hpp"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/stem_export/stem_export.h"
#include "storage/owner.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <span>
#include <string>
#include <strings.h> // strcasecmp
#include <sys/stat.h>
#include <unistd.h>

namespace {

/// Set false at the start of every deluge_scenario_start_stem_export() dispatch,
/// true only once the dispatched worker-fiber closure itself returns. See
/// deluge_scenario_stem_export_done()'s header doc for why this can't just read
/// `stemExport.processStarted` directly.
bool g_stem_export_dispatch_finished = false;

/// Same start/finished-latch shape as g_stem_export_dispatch_finished, for
/// deluge_scenario_start_song_load()/deluge_scenario_song_load_begin_done()
/// below.
bool g_song_load_begin_finished = false;
bool g_song_load_begin_result = false;
std::string g_song_load_begin_path;

/// Same start/finished-latch shape as g_stem_export_dispatch_finished, for the
/// recorder-roundtrip scenario dispatched onto the storage-owner worker fiber.
/// `g_recorder_roundtrip_failures` is -1 until the dispatched closure sets it to the
/// oracle's returned failure count.
bool g_recorder_roundtrip_dispatch_finished = false;
int32_t g_recorder_roundtrip_failures = -1;

} // namespace

extern "C" {

bool deluge_scenario_begin_song_load(const char* full_path) {
	if (currentSong == nullptr) {
		return false;
	}
	currentSong->setSongFullPath(full_path);
	return openUI(&loadSongUI);
}

bool deluge_scenario_song_listing_in_progress() {
	return LoadSongUI::isListingInProgress();
}

void deluge_scenario_start_song_load(const char* full_path) {
	// deluge_scenario_begin_song_load() (above) is called SYNCHRONOUSLY by its
	// existing callers (e.g. the streaming-underrun harness's scenario.rs), which
	// is fine there because THEY only ever reach it from a context that either IS
	// the worker fiber already or never touches the newer storage paths this one
	// does. This scenario's own caller is neither: it runs on a plain Rust task,
	// off the worker fiber, and `openUI(&loadSongUI)`'s opened() chain
	// (LoadSongUI::opened -> FavouritesManager::loadFavouritesBank ->
	// StorageManager::fileExists) does a real storage read. Under `sim_latency`
	// (on by default for golden_vt_render) that read needs a `Timer` fired by a
	// SEPARATE Embassy task (`sim_latency::pump`) to ever resolve — off-fiber it
	// runs through a non-yielding `embassy_futures::block_on` that can never let
	// that task get polled, livelocking the whole process. So dispatch
	// deluge_scenario_begin_song_load() itself onto the worker fiber (same
	// pattern as deluge_scenario_start_stem_export() above), rather than calling
	// it directly. deluge_scenario_begin_song_load() is UNCHANGED and still
	// callable directly by any context that's actually safe doing so.
	g_song_load_begin_finished = false;
	g_song_load_begin_result = false;
	g_song_load_begin_path = full_path;
	bool dispatched = deluge::storage::Owner::run(
	    [](void*) {
		    g_song_load_begin_result = deluge_scenario_begin_song_load(g_song_load_begin_path.c_str());
		    g_song_load_begin_finished = true;
	    },
	    nullptr);
	if (!dispatched) {
		// Owner queue rejected the dispatch — nothing will ever flip the latch.
		g_song_load_begin_result = false;
		g_song_load_begin_finished = true;
	}
}

bool deluge_scenario_song_load_begin_done() {
	return g_song_load_begin_finished;
}

bool deluge_scenario_song_load_begin_ok() {
	return g_song_load_begin_result;
}

bool deluge_scenario_commit_song_load() {
	// beginLoad() owns its own dispatch onto the storage owner and returns straight away; the scenario
	// then polls deluge_scenario_song_load_in_progress() as it did before.
	loadSongUI.beginLoad();
	return true;
}

bool deluge_scenario_song_load_in_progress() {
	return loadSongUI.isLoadingSong();
}

void deluge_scenario_start_playback() {
	changeRootUI(&arrangerView);
	playbackHandler.playButtonPressed(kInternalButtonPressLatency);
}

bool deluge_scenario_playback_active() {
	return playbackHandler.isEitherClockActive();
}

bool deluge_scenario_start_recording() {
	return audioRecorder.beginOutputRecording();
}

uint32_t deluge_scenario_debug_ui_mode() {
	return currentUIMode;
}

uint32_t deluge_scenario_debug_load_state() {
	// bit 0: isLoadingSong() itself (the OR of the two conditions below).
	// bit 1: getCurrentUI() == &loadSongUI (the UI object hasn't transitioned away yet).
	uint32_t bits = 0;
	if (loadSongUI.isLoadingSong()) {
		bits |= 1u;
	}
	if (getCurrentUI() == &loadSongUI) {
		bits |= 2u;
	}
	return bits;
}

void deluge_scenario_start_stem_export(int32_t mode) {
	StemExportType stemMode = static_cast<StemExportType>(mode);
	stemExport.renderOffline = true;
	stemExport.exportToSilence = true;
	stemExport.exportMixdown = (stemMode == StemExportType::MIXDOWN);
	// A MIXDOWN is the full master output, so it must include the song-level FX — see
	// host_render_main.cpp's deluge_render_driver() (the field-for-field reference this
	// mirrors) for the mechanical reason: the offline render path only feeds a recorder
	// whose channel is OFFLINE_OUTPUT, which is selected only when includeSongFX is set.
	if (stemMode == StemExportType::MIXDOWN) {
		stemExport.includeSongFX = true;
	}
	// Start the export and return. It no longer runs as one long call: each step is queued onto the
	// storage owner from the export's own pump task, so completion is observed rather than awaited.
	stemExport.startStemExportProcess(stemMode);
}

bool deluge_scenario_stem_export_done() {
	return !stemExport.isExportInFlight();
}

void deluge_scenario_start_recorder_roundtrip() {
	g_recorder_roundtrip_dispatch_finished = false;
	g_recorder_roundtrip_failures = -1;
	// Dispatch the whole oracle onto the storage-owner worker fiber (same shape as
	// deluge_scenario_start_stem_export above): its file I/O — recorder writes, deluge::io::File
	// reads, open_read_stream — and the finalized probe's deluge_streaming_drain_queue_blocking()
	// yield-and-drain must run ON the fiber to block/resume against the async fill task.
	deluge::storage::Owner::run(
	    [](void*) {
		    g_recorder_roundtrip_failures = deluge_scenario_run_recorder_roundtrip();
		    g_recorder_roundtrip_dispatch_finished = true;
	    },
	    nullptr);
}

bool deluge_scenario_recorder_roundtrip_done() {
	return g_recorder_roundtrip_dispatch_finished;
}

int32_t deluge_scenario_recorder_roundtrip_failures() {
	return g_recorder_roundtrip_failures;
}

void deluge_scenario_copy_stems_out(const char* out_dir) {
	const char* folder = stemExport.lastFolderNameForStemExport.c_str();
	if (folder == nullptr || folder[0] == '\0') {
		fprintf(stderr, "[golden-scenario] no stem folder recorded; nothing to copy\n");
		return;
	}

	mkdir(out_dir, 0755); // host POSIX output dir; best-effort, ignore EEXIST

	auto dir = deluge::io::Directory::open(folder);
	if (!dir.has_value()) {
		fprintf(stderr, "[golden-scenario] cannot open stem folder '%s' in image\n", folder);
		return;
	}

	static char buf[65536];
	int count = 0;
	while (true) {
		auto entry = dir->read();
		if (!entry.has_value() || !entry->has_value()) {
			break; // read error OR end-of-directory
		}
		const DelugeDirEntry& e = **entry;
		if (e.is_directory) {
			continue;
		}
		size_t len = strlen(e.name);
		if (len < 4 || strcasecmp(e.name + len - 4, ".WAV") != 0) {
			continue;
		}

		char src[600];
		char dst[700];
		snprintf(src, sizeof src, "%s/%s", folder, e.name);
		snprintf(dst, sizeof dst, "%s/%s", out_dir, e.name);

		auto in = deluge::io::File::open(src, DELUGE_FILE_READ);
		if (!in.has_value()) {
			fprintf(stderr, "[golden-scenario] cannot read '%s'\n", src);
			continue;
		}
		int ofd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (ofd < 0) {
			fprintf(stderr, "[golden-scenario] cannot create '%s'\n", dst);
			continue; // `in` auto-closes (RAII)
		}
		while (true) {
			auto chunk = in->read(std::span<std::byte>(reinterpret_cast<std::byte*>(buf), sizeof buf));
			if (!chunk.has_value() || chunk->empty()) {
				break; // read error OR EOF
			}
			size_t off = 0;
			size_t br = chunk->size();
			while (off < br) {
				ssize_t w = write(ofd, buf + off, br - off);
				if (w <= 0) {
					break;
				}
				off += static_cast<size_t>(w);
			}
		}
		close(ofd);
		count++;
		fprintf(stderr, "[golden-scenario]   %s\n", dst);
	}
	fprintf(stderr, "[golden-scenario] copied %d stem(s) to %s\n", count, out_dir);
}

} // extern "C"

#endif // DELUGE_HOST
