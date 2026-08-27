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
/// Drives the real file-loading path (AudioFileManager::getAudioFileFromFilename → FileByteSource →
/// parseAudioFileHeader → buildSample) against a project directory served through the host efatfs
/// passthrough (host_efatfs_passthrough.cpp, DELUGE_SD_ROOT), so it exercises the construction path
/// end-to-end (directory walk + on-demand cluster streaming + header parse). It is the Tier-2 regression
/// net for the file-loading redesign: load known WAV/AIFF files off the project directory and dump every
/// descriptor field; a diff in the output across a refactor flags a behavioural change.
///
/// Usage: deluge_loadcheck [--project <dir> (--file SAMPLES/A.WAV | --wavetable PATH)...]
/// Deterministic. No external tools required — the project directory IS the SD card.
///
/// With no --file/--wavetable at all, main() returns immediately after running
/// checkProposeLoadCandidates()'s I/O-free policy checks -- it does not call deluge_platform_init() or
/// deluge_main(), so there is no app boot, no card fixture, and --project is ignored. This is what the
/// harness registry entry for this binary runs as a standing gate (harness/registry.toml); running it
/// with zero arguments from any directory, including the repo root, leaves the tree untouched.

#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/wave_table/wave_table.h"

#include "OSLikeStuff/scheduler_api.h" // TaskHandle
#include "libdeluge/system.h"          // deluge_platform_init

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" int32_t deluge_main(void);
extern TaskHandle startupConditionalTask;

namespace {

// Files to load, captured from argv. `wavetable` selects AudioFileType::WAVETABLE.
struct LoadItem {
	std::string path;
	bool wavetable;
};
std::vector<LoadItem> g_items;

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

/// Assert the two invariants an async load must preserve (design §4b, §6): the manager holds no
/// half-built object (adopted, reasoned, but with zero geometry), and no load leaked a reason.
/// Both hold trivially on the synchronous path — pinning them here makes a later async break loud.
///
/// The geometry half only applies to Samples (a WaveTable has no byteDepth/numChannels concept of
/// its own in this sense). The leaked-reason half applies to every AudioFile: this driver never
/// calls addReason() itself, and buildAudioFileFromCard's own protect-during-setup reason is always
/// dropped before getAudioFileFromFilename returns (removeReason("E399") — "Setup done ... the
/// caller re-leases"), so every object reachable here should sit at leaseCount() == 0 between items.
bool checkLoadInvariants(const char* label) {
	bool ok = true;
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* f = audioFileManager.audioFiles[i];

		if (f->type == AudioFileType::SAMPLE) {
			Sample* s = static_cast<Sample*>(f);
			if (s->byteDepth == 0 || s->numChannels == 0) {
				printf("FAIL [%s]: zero-geometry Sample in cache: %s\n", label, s->filePath.c_str());
				ok = false;
			}
		}

		if (f->leaseCount() != 0) {
			printf("FAIL [%s]: leaked reason(s) on %s (leaseCount=%u)\n", label, f->filePath.c_str(), f->leaseCount());
			ok = false;
		}
	}
	return ok;
}

/// Pin proposeLoadCandidates()'s policy (which paths a load tries, in what order — no I/O). A later
/// async loader consumes this function as its only source of truth for candidate paths, so its order
/// is behaviour: get it wrong and a load silently opens the wrong file (or misses one it used to find).
bool checkProposeLoadCandidates() {
	bool ok = true;
	using Candidates = std::array<std::string, AudioFileManager::kMaxLoadCandidates>;

	// proposes the alternate directory (bare-name form) before the regular path when it exists
	{
		// Order is behaviour: resolveFileSize tries the alternate dir first whenever it exists, and
		// the async loader opens candidates in the order this function returns them. For a single-level
		// "SAMPLES/..." path the flattened and bare alternate names coincide, so this exercises the
		// 2-distinct-candidate shape (duplicate alt name, then the regular path).
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/kick.wav", c.data(), c.size());
		if (count != 3) {
			printf("FAIL [proposeLoadCandidates]: expected 3 candidates for a single-level SAMPLES/ path, got %zu\n",
			       count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.wav\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] \"SAMPLES/ALT/kick.wav\", got \"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[2] \"SAMPLES/kick.wav\", got \"%s\"\n",
				       c[2].c_str());
				ok = false;
			}
		}
	}

	// a nested SAMPLES/ path proposes the flattened alt name, then the bare alt name, then the regular
	// path -- this is the case a two-candidate policy previously dropped (the bare-name-in-alt-dir
	// fallback), so it's the one that most needs pinning.
	{
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/FOLDER/kick.wav", c.data(), c.size());
		if (count != 3) {
			printf("FAIL [proposeLoadCandidates]: expected 3 candidates for a nested SAMPLES/ path, got %zu\n", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/FOLDER_kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] (flattened) "
				       "\"SAMPLES/ALT/FOLDER_kick.wav\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "SAMPLES/ALT/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] (bare) \"SAMPLES/ALT/kick.wav\", got "
				       "\"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
			if (c[2] != "SAMPLES/FOLDER/kick.wav") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[2] (regular) \"SAMPLES/FOLDER/kick.wav\", got "
				       "\"%s\"\n",
				       c[2].c_str());
				ok = false;
			}
		}
	}

	// a path outside SAMPLES/ skips the flattened-name candidate: bare alt name, then regular path.
	{
		audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::DOES_EXIST;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("PRESETS/SYNTHS/kick.xml", c.data(), c.size());
		if (count != 2) {
			printf("FAIL [proposeLoadCandidates]: expected 2 candidates for a non-SAMPLES/ path, got %zu\n", count);
			ok = false;
		}
		else {
			if (c[0] != "SAMPLES/ALT/kick.xml") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/ALT/kick.xml\", got \"%s\"\n",
				       c[0].c_str());
				ok = false;
			}
			if (c[1] != "PRESETS/SYNTHS/kick.xml") {
				printf("FAIL [proposeLoadCandidates]: expected candidate[1] \"PRESETS/SYNTHS/kick.xml\", got "
				       "\"%s\"\n",
				       c[1].c_str());
				ok = false;
			}
		}
	}

	// proposes only the regular path when no alternate directory exists
	{
		audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;
		Candidates c;
		size_t count = audioFileManager.proposeLoadCandidates("SAMPLES/kick.wav", c.data(), c.size());
		if (count != 1) {
			printf("FAIL [proposeLoadCandidates]: expected 1 candidate with no alternate dir known, got %zu\n", count);
			ok = false;
		}
		else if (c[0] != "SAMPLES/kick.wav") {
			printf("FAIL [proposeLoadCandidates]: expected candidate[0] \"SAMPLES/kick.wav\", got \"%s\"\n",
			       c[0].c_str());
			ok = false;
		}
	}

	// Leave the manager in its default (no-alternate-dir-known) state for the real loads below.
	audioFileManager.alternateAudioFileLoadPath.clear();
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::NONE_SET;

	return ok;
}

void deluge_loadcheck_driver() {
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	int failures = 0;
	if (!checkProposeLoadCandidates()) {
		failures++;
	}
	for (const LoadItem& item : g_items) {
		std::string path = item.path;
		Error error = Error::NONE;
		AudioFile* audioFile = audioFileManager.getAudioFileFromFilename(
		    path, /*mayReadCard=*/true, &error, item.wavetable ? AudioFileType::WAVETABLE : AudioFileType::SAMPLE,
		    /*makeWaveTableWorkAtAllCosts=*/false);

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

		if (!checkLoadInvariants(item.path.c_str())) {
			failures++;
		}
	}

	fflush(nullptr);
	quick_exit(failures == 0 ? 0 : 1);
}

void usage(const char* argv0) {
	fprintf(stderr, "usage: %s [--project <dir> (--file PATH | --wavetable PATH)...]\n", argv0);
	fprintf(stderr, "  With no --file/--wavetable at all, runs only the I/O-free policy checks\n");
	fprintf(stderr, "  (checkProposeLoadCandidates) and exits -- no app boot, no card fixture, --project\n");
	fprintf(stderr, "  ignored. This is what the harness registry runs as a gate.\n");
	fprintf(stderr, "  With --file/--wavetable, --project <dir> is required.\n");
}

} // namespace

int main(int argc, char** argv) {
	const char* project = nullptr;

	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
		if (strcmp(a, "--project") == 0 && v) {
			project = v;
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

	if (g_items.empty()) {
		// Pure policy checks only: no card, no filesystem, no app boot. Deliberately does NOT call
		// deluge_platform_init()/deluge_main() here, even with --project defaulted to something benign --
		// booting the app has real side effects (RuntimeFeatureSettings::readSettingsFromFile()
		// unconditionally mkdir's SETTINGS/ under whatever DELUGE_SD_ROOT resolves to), and this mode's
		// whole point is to be safe to run from anywhere -- including the repo root, with zero arguments,
		// which is exactly what the harness registry entry does -- with no fixture and no cleanup
		// required. --project is unused and ignored here.
		const bool ok = checkProposeLoadCandidates();
		fflush(nullptr);
		return ok ? 0 : 1;
	}

	if (project == nullptr) {
		usage(argv[0]);
		return 1;
	}

	// The project directory IS the SD card: host_efatfs_passthrough.cpp serves every task-context
	// file operation (including the streaming-read path, which is efatfs-only) straight from it.
	setenv("DELUGE_SD_ROOT", project, 1);

	if (getenv("DELUGE_HOST_DETERMINISTIC") == nullptr) {
		setenv("DELUGE_HOST_DETERMINISTIC", "1", 1);
	}
	if (getenv("DELUGE_HOST_AUDIO") == nullptr) {
		setenv("DELUGE_HOST_AUDIO", "off", 1);
	}

	startupConditionalTask = deluge_loadcheck_driver; // override before deluge_main registers it

	deluge_platform_init();
	deluge_main(); // never returns; deluge_loadcheck_driver quick_exit()s

	return 0;
}
