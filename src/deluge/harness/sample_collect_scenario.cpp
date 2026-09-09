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

#include "harness/sample_collect_scenario.h"

#ifdef DELUGE_HOST

#include "gui/ui/browser/browser.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "io/file.hpp"
#include "model/song/song.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include "util/string.h"
#include <cstdio>
#include <string>
#include <vector>

// The GUI headers reach `src/lib/printf.h`, which soft-aliases printf to an implementation this
// harness has no `putchar_` for -- every line below would become an undefined symbol at link time.
#ifdef printf
#undef printf
#endif

/// Reaches the protected ENTER gesture, and `collectingSamples`, which is what makes this a
/// collect-media save rather than an ordinary one.
class SampleCollectProbe {
public:
	static bool openOn(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		if (!openUI(&saveSongUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	static bool listingInProgress() { return Browser::isListingInProgress(); }
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void collectMedia() { saveSongUI.collectingSamples = true; }
	static void pressEnter() { saveSongUI.enterKeyPress(); }
};

namespace {

constexpr const char* kSaveDir = "SONGS";
constexpr const char* kSaveName = "SCTEST01";
constexpr const char* kSampleA = "SAMPLES/COLL1.WAV";
constexpr const char* kSampleB = "SAMPLES/COLL2.WAV";

enum class Stage : uint8_t { Residents, Fixture, FixtureWait, Save, SaveWait, Verify, Report, Done };

Stage g_stage = Stage::Residents;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_waitTicks = 0;
int32_t g_resident = 0;
std::vector<AudioFile*> g_referenced;

constexpr int32_t kMaxWaitTicks = 200000;

uint32_t crc32(const void* data, size_t len) {
	const auto* p = static_cast<const uint8_t*>(data);
	uint32_t crc = 0xFFFFFFFFu;
	for (size_t i = 0; i < len; i++) {
		crc ^= p[i];
		for (int32_t k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
		}
	}
	return ~crc;
}

bool readWholeFile(const char* path, std::vector<uint8_t>& out) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened.has_value()) {
		return false;
	}
	auto size = opened->size();
	if (!size || *size == 0) {
		return false;
	}
	out.assign(*size, 0);
	auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(out.data()), *size));
	(void)opened->close();
	return read.has_value() && read->size() == *size;
}

/// Make a fixture WAV resident AND referenced: `isProjectReferenced()` is a lease count, and a save
/// only collects files the song is holding.
void makeReferenced(const char* path) {
	std::string fileName = path;
	Error error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/false);
	if (file == nullptr) {
		printf("SC-SETUP FAIL %s did not load (error %d)\n", path, static_cast<int>(error));
		g_failures++;
		return;
	}
	file->addReason();
	g_referenced.push_back(file);
	++g_resident;
}

/// SC-ORDERED. The property the whole snapshot design exists to provide.
bool listStillUsable() {
	bool ok = true;
	for (AudioFile* file : g_referenced) {
		if (audioFileManager.audioFiles.searchForExactObject(file) < 0) {
			printf("SC-ORDERED FAIL %s is resident but no longer findable in the list\n", file->filePath.c_str());
			ok = false;
		}
	}
	for (size_t i = 1; i < audioFileManager.audioFiles.size(); i++) {
		const char* previous = audioFileManager.audioFiles[i - 1]->filePath.c_str();
		const char* current = audioFileManager.audioFiles[i]->filePath.c_str();
		if (strcasecmp(previous, current) > 0) {
			printf("SC-ORDERED FAIL the list is out of order at %zu: \"%s\" before \"%s\"\n", i, previous, current);
			ok = false;
		}
	}
	return ok;
}

void runTick() {
	switch (g_stage) {
	case Stage::Residents: {
		makeReferenced(kSampleA);
		makeReferenced(kSampleB);
		printf("SC-SETUP resident=%d\n", g_resident);
		g_stage = (g_failures == 0) ? Stage::Fixture : Stage::Report;
		break;
	}
	case Stage::Fixture: {
		changeRootUI(&arrangerView);
		(void)SampleCollectProbe::openOn(kSaveDir);
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!SampleCollectProbe::listingInProgress()) {
			g_stage = Stage::Save;
		}
		break;
	}
	case Stage::Save: {
		SampleCollectProbe::typeName(kSaveName);
		SampleCollectProbe::collectMedia();
		SampleCollectProbe::pressEnter();
		g_stage = Stage::SaveWait;
		break;
	}
	case Stage::SaveWait: {
		// Both machines: the collection parks between ticks, and the save around it does too.
		if (!saveSongUI.collectInFlight() && !SaveUI::saveInFlight()) {
			g_stage = Stage::Verify;
			break;
		}
		if (++g_waitTicks > kMaxWaitTicks) {
			printf("SC-COPIED FAIL the save never finished (%d ticks)\n", g_waitTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::Verify: {
		// SC-COPIED: every referenced file arrived AT ITS DESTINATION, byte-identical to its source.
		//
		// The destination is the point. Checking `filePath` would pass by reading the ORIGINAL file,
		// which of course is still there -- a collect-media save that transferred nothing at all would
		// look perfect. `loadedFromAlternatePath` is where a collected file now lives, and its being
		// EMPTY after a collect-media save is itself the failure.
		for (AudioFile* file : g_referenced) {
			// Where the collected copy should be. It is COMPUTED the same way the save computes it,
			// and not read back off the AudioFile: `loadedFromAlternatePath` is only recorded for
			// RECORDINGS, which get a generated name. An ordinary sample is copied into the song's
			// folder without the file object remembering that, so asking the object would report
			// "never collected" for a collection that worked perfectly.
			std::string expected;
			if (audioFileManager.setupAlternateAudioFileDir(expected, kSaveDir, kSaveName) != Error::NONE) {
				printf("SC-COPIED FAIL could not work out where %s should have been collected to\n",
				       file->filePath.c_str());
				g_failures++;
				continue;
			}
			expected += "/";
			const int32_t dirLength = static_cast<int32_t>(expected.size());
			if (audioFileManager.setupAlternateAudioFilePath(expected, dirLength, file->filePath) != Error::NONE) {
				printf("SC-COPIED FAIL could not build the collected path for %s\n", file->filePath.c_str());
				g_failures++;
				continue;
			}

			std::vector<uint8_t> source;
			std::vector<uint8_t> collected;
			const bool haveSource = readWholeFile(file->filePath.c_str(), source);
			if (!readWholeFile(expected.c_str(), collected)) {
				printf("SC-COPIED FAIL %s was not collected to %s\n", file->filePath.c_str(), expected.c_str());
				g_failures++;
				continue;
			}
			const uint32_t collectedCrc = crc32(collected.data(), collected.size());
			printf("SC-COPIED to=%s bytes=%zu crc=%08X\n", expected.c_str(), collected.size(), collectedCrc);
			if (haveSource && crc32(source.data(), source.size()) != collectedCrc) {
				printf("SC-COPIED FAIL the collected copy of %s differs from its source\n", file->filePath.c_str());
				g_failures++;
			}
		}
		if (!listStillUsable()) {
			g_failures++;
		}
		else {
			printf("SC-ORDERED ok entries=%zu\n", audioFileManager.audioFiles.size());
		}
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		// Both arms are reported, and the gate holds them to account for every transfer between
		// them: either number alone reads as a success on a build where half the files quietly took
		// the other arm.
		printf("SC-SUMMARY served=%d ownerServed=%d offOwnerTicks=%d\n", (int)SaveSongUI::transferChannelServedOps(),
		       (int)SaveSongUI::transferOwnerServedOps(), (int)SaveSongUI::offOwnerTransferTicks());
		printf("SC-RESULT failures=%d\n", g_failures);
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_sample_collect_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Residents;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_waitTicks = 0;
	g_resident = 0;
	g_referenced.clear();
}

bool deluge_scenario_sample_collect_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_sample_collect_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after it.
	// Setting g_stepDone directly races the owner and the scenario sails past work it just queued.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_sample_collect_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_sample_collect_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
