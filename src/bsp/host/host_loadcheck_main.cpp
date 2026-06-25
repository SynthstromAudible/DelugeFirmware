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

/// deluge_loadcheck — headless "load these audio files and print their parsed descriptors" utility.
///
/// Drives the real file-loading path (AudioFileManager::getAudioFileFromFilename → ClusterByteSource →
/// parseAudioFileHeader → buildSample) against a FAT disk image, so it exercises the construction path
/// end-to-end (FAT cluster walk + on-demand cluster streaming + header parse). It is the Tier-2 regression
/// net for the file-loading redesign: load known WAV/AIFF files off a card image and dump every descriptor
/// field; a diff in the output across a refactor flags a behavioural change.
///
/// Usage: deluge_loadcheck (--project <dir> | --image <img.fat>) --file SAMPLES/A.WAV [--file ... | --wavetable PATH]
/// Requires mtools (mformat, mcopy) on PATH for --project. Deterministic.

#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/wave_table/wave_table.h"

#include "OSLikeStuff/scheduler_api.h" // TaskHandle
#include "libdeluge/system.h"          // deluge_platform_init

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

extern "C" int32_t deluge_main(void);
extern TaskHandle startupConditionalTask;

namespace {

char g_temp_image[512] = {0};
// Files to load, captured from argv. `wavetable` selects AudioFileType::WAVETABLE.
struct LoadItem {
	std::string path;
	bool wavetable;
};
std::vector<LoadItem> g_items;

void cleanup_temp_image() {
	if (g_temp_image[0] != '\0') {
		unlink(g_temp_image);
		g_temp_image[0] = '\0';
	}
}

const char* errorName(Error e) {
	switch (e) {
	case Error::NONE:
		return "NONE";
	case Error::FILE_CORRUPTED:
		return "FILE_CORRUPTED";
	case Error::FILE_UNSUPPORTED:
		return "FILE_UNSUPPORTED";
	case Error::FILE_UNREADABLE:
		return "FILE_UNREADABLE";
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE:
		return "NOT_WAVETABLE";
	case Error::FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		return "NOT_WAVETABLE_STEREO";
	case Error::SD_CARD:
		return "SD_CARD";
	case Error::INSUFFICIENT_RAM:
		return "INSUFFICIENT_RAM";
	default:
		return "OTHER";
	}
}

// Plain CRC32 (IEEE 802.3, reflected) over a byte range — a cheap stable fingerprint of decoded band data.
uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int b = 0; b < 8; b++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

// Print a WaveTable's decoded structure + a fingerprint of each band's data. The fingerprint covers the
// output of setup()'s read-then-FFT pipeline, so a regression in the cluster-streaming data read shows as a
// CRC diff. Band data is numCycles cycles of cycleSizeNoDuplicates 16-bit samples (duplicates excluded so the
// fingerprint doesn't depend on the WAVETABLE_NUM_DUPLICATE_SAMPLES_AT_END_OF_CYCLE constant).
void dumpWaveTable(const char* path, const WaveTable& wt) {
	printf("LOADED %s | WAVETABLE channels=%u numCycles=%d numBands=%zu", path, wt.numChannels, wt.numCycles,
	       static_cast<size_t>(wt.bands.size()));
	for (size_t b = 0; b < wt.bands.size(); b++) {
		const WaveTableBand& band = wt.bands[b];
		const size_t numSamples = static_cast<size_t>(wt.numCycles) * band.cycleSizeNoDuplicates;
		const uint32_t crc =
		    band.dataAccessAddress != nullptr ? crc32(band.dataAccessAddress, numSamples * sizeof(int16_t)) : 0;
		printf(" | band%zu cycleSize=%u mag=%u crc=%08X", b, band.cycleSizeNoDuplicates, band.cycleSizeMagnitude, crc);
	}
	printf("\n");
}

// Print one Sample's full parsed descriptor as a stable, greppable line.
void dumpSample(const char* path, const Sample& s) {
	printf("LOADED %s | channels=%u byteDepth=%u rawFormat=%u sampleRate=%u "
	       "dataStart=%u dataLen=%llu lengthSamples=%llu midiNote=%.4f loopStart=%u loopEnd=%u wtCycle=%u\n",
	       path, s.numChannels, s.byteDepth, static_cast<unsigned>(s.rawDataFormat), s.sampleRate,
	       s.audioDataStartPosBytes, static_cast<unsigned long long>(s.audioDataLengthBytes),
	       static_cast<unsigned long long>(s.lengthInSamples), static_cast<double>(s.midiNoteFromFile),
	       s.fileLoopStartSamples, s.fileLoopEndSamples, s.waveTableCycleSize);
}

void deluge_loadcheck_driver() {
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	int failures = 0;
	for (const LoadItem& item : g_items) {
		std::string path = item.path;
		Error error = Error::NONE;
		AudioFile* audioFile = audioFileManager.getAudioFileFromFilename(
		    path, /*mayReadCard=*/true, &error, /*suppliedFilePointer=*/nullptr,
		    item.wavetable ? AudioFileType::WAVETABLE : AudioFileType::SAMPLE, /*makeWaveTableWorkAtAllCosts=*/false);

		if (audioFile == nullptr) {
			printf("FAILED %s | error=%s (%d)\n", item.path.c_str(), errorName(error), static_cast<int>(error));
			failures++;
			continue;
		}
		if (item.wavetable) {
			dumpWaveTable(item.path.c_str(), static_cast<WaveTable&>(*audioFile));
		}
		else {
			dumpSample(item.path.c_str(), static_cast<Sample&>(*audioFile));
		}
	}

	fflush(nullptr);
	quick_exit(failures == 0 ? 0 : 1);
}

bool pack_image(const char* project, char* out_path, size_t out_size) {
	char tmpl[] = "/tmp/deluge_loadcheck_XXXXXX.img";
	int fd = mkstemps(tmpl, 4);
	if (fd < 0) {
		perror("[loadcheck] mkstemps");
		return false;
	}
	close(fd);
	snprintf(out_path, out_size, "%s", tmpl);

	// >= 2.5 GB sparse image so it formats as a valid FAT32 with 32 KB clusters (the geometry the
	// firmware's FatFS expects; see host_render_main.cpp for the rationale).
	const long long bytes = 2560LL << 20;
	char cmd[2200];
	snprintf(cmd, sizeof cmd, "truncate -s %lld '%s'", bytes, out_path);
	if (system(cmd) != 0) {
		fprintf(stderr, "[loadcheck] truncate failed\n");
		return false;
	}
	snprintf(cmd, sizeof cmd, "mformat -i '%s' -F -c 64 ::", out_path);
	if (system(cmd) != 0) {
		fprintf(stderr, "[loadcheck] mformat failed (is mtools installed?)\n");
		return false;
	}
	snprintf(cmd, sizeof cmd, "mcopy -s -Q -i '%s' '%s'/* ::/", out_path, project);
	if (system(cmd) != 0) {
		fprintf(stderr, "[loadcheck] mcopy failed\n");
		return false;
	}
	fprintf(stderr, "[loadcheck] packed '%s' -> %s\n", project, out_path);
	return true;
}

void usage(const char* argv0) {
	fprintf(stderr, "usage: %s (--project <dir> | --image <img>) (--file PATH | --wavetable PATH)...\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
	const char* project = nullptr;
	const char* image = nullptr;

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
		else if (strcmp(a, "--file") == 0 && v) {
			g_items.push_back({v, false});
			i++;
		}
		else if (strcmp(a, "--wavetable") == 0 && v) {
			g_items.push_back({v, true});
			i++;
		}
		else {
			usage(argv[0]);
			return 1;
		}
	}

	if (g_items.empty() || (project == nullptr && image == nullptr)) {
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

	if (getenv("DELUGE_HOST_DETERMINISTIC") == nullptr) {
		setenv("DELUGE_HOST_DETERMINISTIC", "1", 1);
	}
	if (getenv("DELUGE_HOST_AUDIO") == nullptr) {
		setenv("DELUGE_HOST_AUDIO", "off", 1);
	}

	startupConditionalTask = deluge_loadcheck_driver; // override before deluge_main registers it

	deluge_platform_init();
	deluge_main(); // never returns; deluge_loadcheck_driver quick_exit()s

	cleanup_temp_image();
	return 0;
}
