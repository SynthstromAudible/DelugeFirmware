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

/// deluge_render — headless "render a Deluge project to stems" utility.
///
/// Reuses the firmware's existing offline stem-export engine (StemExport) and the
/// host BSP. Flow:
///   1. main() packs the host project directory into a temporary FAT image (mtools),
///      points the disk-image diskio backend at it (DELUGE_SD_IMAGE), and arms the
///      render driver by overriding `startupConditionalTask` before deluge_main().
///   2. Once the card mounts, the scheduler runs deluge_render_driver(): it loads the
///      requested song (the same path setupStartupSong uses), selects the playback
///      context for the chosen stem mode, and runs stemExport.startStemExportProcess()
///      — which renders the whole arrangement faster-than-realtime and writes per-stem
///      WAVs into the image (SAMPLES/EXPORTS/<song>/<TYPE>##/).
///   3. The driver copies those WAVs back out to a host directory and quick_exit()s.
///
/// Usage: deluge_render --project <dir> --song SONGS/NAME.XML [--mode TRACK|MIXDOWN|CLIP|DRUM] [--out <dir>]
///        deluge_render --image <img.fat> --song SONGS/NAME.XML ...   (skip packing)
/// Requires mtools (mformat, mcopy) on PATH for --project. Deterministic by default.

#include "definitions_cxx.hpp" // StemExportType
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h" // openUI, changeRootUI
#include "gui/views/arranger_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/session_view.h"
#include "model/song/song.h"
#include "processing/stem_export/stem_export.h"

#include "OSLikeStuff/scheduler_api.h" // TaskHandle

#include "fatfs/ff.h"         // f_opendir/f_readdir/f_open/f_read (copy-out)
#include "libdeluge/system.h" // deluge_platform_init

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <strings.h> // strcasecmp
#include <sys/stat.h>
#include <unistd.h>

// The app boot + run loop (deluge.cpp). Ends in startTaskManager()'s infinite loop;
// the render driver owns the process exit.
extern "C" int32_t deluge_main(void);
// The SD-ready conditional task hook (deluge.cpp). Default is setupStartupSong; we
// override it below so the render driver runs instead.
extern TaskHandle startupConditionalTask;

namespace {

char g_temp_image[512] = {0}; // packed temp image to unlink on exit (empty if --image)

void cleanup_temp_image(void) {
	if (g_temp_image[0]) {
		unlink(g_temp_image);
		g_temp_image[0] = '\0';
	}
}

StemExportType parse_mode(const char* s) {
	if (s == nullptr) {
		return StemExportType::TRACK;
	}
	if (strcasecmp(s, "MIXDOWN") == 0) {
		return StemExportType::MIXDOWN;
	}
	if (strcasecmp(s, "CLIP") == 0) {
		return StemExportType::CLIP;
	}
	if (strcasecmp(s, "DRUM") == 0) {
		return StemExportType::DRUM;
	}
	return StemExportType::TRACK;
}

// ---- copy the produced stem WAVs out of the image to a host directory ----------
void copy_stems_out(const char* folder, const char* out_dir) {
	mkdir(out_dir, 0755); // best-effort; ignore EEXIST

	DIR dir;
	if (f_opendir(&dir, folder) != FR_OK) {
		fprintf(stderr, "[render] cannot open stem folder '%s' in image\n", folder);
		return;
	}

	static char buf[65536];
	int count = 0;
	FILINFO fno;
	while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != '\0') {
		if (fno.fattrib & AM_DIR) {
			continue;
		}
		size_t len = strlen(fno.fname);
		if (len < 4 || strcasecmp(fno.fname + len - 4, ".WAV") != 0) {
			continue;
		}

		char src[600];
		char dst[700];
		snprintf(src, sizeof src, "%s/%s", folder, fno.fname);
		snprintf(dst, sizeof dst, "%s/%s", out_dir, fno.fname);

		FIL fil;
		if (f_open(&fil, src, FA_READ) != FR_OK) {
			fprintf(stderr, "[render] cannot read '%s'\n", src);
			continue;
		}
		int ofd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (ofd < 0) {
			fprintf(stderr, "[render] cannot create '%s'\n", dst);
			f_close(&fil);
			continue;
		}
		UINT br = 0;
		while (f_read(&fil, buf, sizeof buf, &br) == FR_OK && br > 0) {
			size_t off = 0;
			while (off < br) {
				ssize_t w = write(ofd, buf + off, br - off);
				if (w <= 0) {
					break;
				}
				off += (size_t)w;
			}
		}
		close(ofd);
		f_close(&fil);
		count++;
		fprintf(stderr, "[render]   %s\n", dst);
	}
	f_closedir(&dir);
	fprintf(stderr, "[render] copied %d stem(s) to %s\n", count, out_dir);
}

// ---- the render driver task (runs once the card mounts) ------------------------
void deluge_render_driver(void) {
	// performLoad() and the stem-export process both yield to the scheduler, which can
	// re-fire this conditional task mid-flight — re-entering performLoad while the first
	// export's playback + SampleRecorder are live (song-swap path → cardRoutine spin).
	// The driver is strictly one-shot (it owns the process exit), so ignore re-entry.
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	const char* song_path = getenv("DELUGE_RENDER_SONG");
	const char* mode_str = getenv("DELUGE_RENDER_MODE");
	const char* out_dir = getenv("DELUGE_RENDER_OUT");
	StemExportType mode = parse_mode(mode_str);

	if (song_path == nullptr || song_path[0] == '\0') {
		fprintf(stderr, "[render] DELUGE_RENDER_SONG not set\n");
		fflush(NULL);
		quick_exit(2);
	}

	// Smoke-test convenience: with DELUGE_RENDER_MAKE_TEMPLATE=1 write a fresh
	// template song at song_path first (a writable image), so the tool can be
	// exercised end-to-end without supplying a real project.
	if (getenv("DELUGE_RENDER_MAKE_TEMPLATE") != nullptr) {
		fprintf(stderr, "[render] writing template song to %s\n", song_path);
		currentSong->writeTemplateSong(song_path);
	}

	// Load the song — the exact sequence setupStartupSong uses (deluge.cpp).
	fprintf(stderr, "[render] loading %s\n", song_path);
	currentSong->setSongFullPath(song_path);
	if (!openUI(&loadSongUI)) {
		fprintf(stderr, "[render] could not open the song loader\n");
		fflush(NULL);
		quick_exit(3);
	}
	loadSongUI.performLoad();

	// Select the playback context the chosen stem mode expects (mirrors the on-device
	// view from which each export is triggered).
	switch (mode) {
	case StemExportType::MIXDOWN:
	case StemExportType::TRACK:
		changeRootUI(&arrangerView);
		break;
	case StemExportType::CLIP:
		changeRootUI(&sessionView);
		break;
	case StemExportType::DRUM:
		changeRootUI(&instrumentClipView);
		break;
	}

	fprintf(stderr, "[render] exporting (mode=%s)\n", mode_str ? mode_str : "TRACK");
	stemExport.renderOffline = true;
	stemExport.exportToSilence = true;
	stemExport.exportMixdown = (mode == StemExportType::MIXDOWN);
	stemExport.startStemExportProcess(mode); // runs the whole export synchronously

	const char* folder = stemExport.lastFolderNameForStemExport.get();
	fprintf(stderr, "[render] export complete; stems at image:/%s\n", (folder && folder[0]) ? folder : "(none)");
	if (out_dir != nullptr && out_dir[0] != '\0' && folder != nullptr && folder[0] != '\0') {
		copy_stems_out(folder, out_dir);
	}

	fflush(NULL);
	quick_exit(0);
}

// ---- pack a host project directory into a temporary FAT image (mtools) ----------
long long dir_size_bytes(const char* dir) {
	char cmd[1100];
	snprintf(cmd, sizeof cmd, "du -sb '%s' 2>/dev/null", dir);
	FILE* p = popen(cmd, "r");
	if (p == nullptr) {
		return 0;
	}
	long long b = 0;
	if (fscanf(p, "%lld", &b) != 1) {
		b = 0;
	}
	pclose(p);
	return b;
}

bool pack_image(const char* project, char* out_path, size_t out_size) {
	char tmpl[] = "/tmp/deluge_render_XXXXXX.img";
	int fd = mkstemps(tmpl, 4); // ".img" suffix
	if (fd < 0) {
		perror("[render] mkstemps");
		return false;
	}
	close(fd);
	snprintf(out_path, out_size, "%s", tmpl);

	// Generous size: 2x the tree + 64MB headroom (FAT32 wants >= ~33MB), rounded to 512.
	long long bytes = dir_size_bytes(project) * 2 + (64LL << 20);
	bytes = (bytes + 511) & ~511LL;

	char cmd[2200];
	snprintf(cmd, sizeof cmd, "truncate -s %lld '%s'", bytes, out_path);
	if (system(cmd) != 0) {
		fprintf(stderr, "[render] truncate failed\n");
		return false;
	}
	snprintf(cmd, sizeof cmd, "mformat -i '%s' -F ::", out_path);
	if (system(cmd) != 0) {
		fprintf(stderr, "[render] mformat failed (is mtools installed?)\n");
		return false;
	}
	// Copy the project tree's top-level entries (SONGS/, SAMPLES/, ...) into the image root.
	snprintf(cmd, sizeof cmd, "mcopy -s -Q -i '%s' '%s'/* ::/", out_path, project);
	if (system(cmd) != 0) {
		fprintf(stderr, "[render] mcopy failed\n");
		return false;
	}
	fprintf(stderr, "[render] packed '%s' -> %s (%lld bytes)\n", project, out_path, bytes);
	return true;
}

void usage(const char* argv0) {
	fprintf(stderr,
	        "usage: %s (--project <dir> | --image <img>) --song SONGS/NAME.XML\n"
	        "          [--mode TRACK|MIXDOWN|CLIP|DRUM] [--out <dir>]\n",
	        argv0);
}

} // namespace

int main(int argc, char** argv) {
	const char* project = nullptr;
	const char* image = nullptr;
	const char* song = nullptr;
	const char* mode = "TRACK";
	const char* out = "./stems";

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
		if (strcmp(a, "--project") == 0 && v) {
			project = v;
			i++;
		}
		else if (strcmp(a, "--image") == 0 && v) {
			image = v;
			i++;
		}
		else if (strcmp(a, "--song") == 0 && v) {
			song = v;
			i++;
		}
		else if (strcmp(a, "--mode") == 0 && v) {
			mode = v;
			i++;
		}
		else if (strcmp(a, "--out") == 0 && v) {
			out = v;
			i++;
		}
		else {
			usage(argv[0]);
			return 1;
		}
	}

	if (song == nullptr || (project == nullptr && image == nullptr)) {
		usage(argv[0]);
		return 1;
	}

	if (project != nullptr) {
		if (!pack_image(project, g_temp_image, sizeof g_temp_image)) {
			return 1;
		}
		at_quick_exit(cleanup_temp_image);
		atexit(cleanup_temp_image);
		setenv("DELUGE_SD_IMAGE", g_temp_image, 1);
	}
	else {
		setenv("DELUGE_SD_IMAGE", image, 1);
	}

	setenv("DELUGE_RENDER", "1", 1);
	setenv("DELUGE_RENDER_SONG", song, 1);
	setenv("DELUGE_RENDER_MODE", mode, 1);
	setenv("DELUGE_RENDER_OUT", out, 1);
	if (getenv("DELUGE_HOST_DETERMINISTIC") == nullptr) {
		setenv("DELUGE_HOST_DETERMINISTIC", "1", 1); // sample-accurate, faster-than-realtime
	}

	startupConditionalTask = deluge_render_driver; // override before deluge_main registers it

	deluge_platform_init();
	deluge_main(); // never returns; deluge_render_driver quick_exit()s

	cleanup_temp_image();
	return 0;
}
