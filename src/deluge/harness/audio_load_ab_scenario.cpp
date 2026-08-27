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

#include "harness/audio_load_ab_scenario.h"

#ifdef DELUGE_HOST

#include "definitions_cxx.hpp"
#include "libdeluge/audio_load.h" // deluge_audio_load_async_active
#include "model/sample/sample.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/owner.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

namespace {

/// One sample the load asks for, in the order the load asks for it.
///
/// The order is behaviour, not presentation. `alternateLoadDirStatus` is per-LOAD state that each
/// sample MUTATES as it goes (a regular-path miss is what earns the alternate-dir probe), so the
/// try order for every later sample depends on what the earlier ones did. A list that resolved
/// every file from the regular directory would never exercise that at all.
struct Case {
	/// Label for the printed line — short, stable, and independent of the path.
	const char* label;
	/// The nominal path, as a song's XML would name it.
	const char* path;
};

/// The load, in order. Deliberately mixed, and the mix is the fixture's whole content:
///
/// - `both` exists in the regular directory AND the alternate one, and is asked for FIRST — while
///   `alternateLoadDirStatus` is still MIGHT_EXIST, so only the regular path is even proposed.
/// - `only_alt` exists only in the alternate directory. Its regular-path miss is what probes that
///   directory and promotes the status for everything after it.
/// - `both2` is the same two-locations shape as `both`, but asked for AFTER that promotion, so its
///   request carries BOTH candidates and the order between them is live. Without this case the
///   candidate order is never actually under test: every earlier two-location lookup happens while
///   only one candidate exists, and every later file exists in one place, so any order finds the
///   same file. (Established by negative control: reversing the loader's candidate order left the
///   six-case version of this list completely unchanged.)
/// - `only_reg` and `stereo24` follow, under the promoted status; `stereo24` differs in both of the
///   geometry fields the comparison names first (2 channels, 24-bit).
/// - `garbage` opens and fails to parse, `missing` never opens: between them they pin that the two
///   backings fail with the same `Error` IDENTITY, not merely that both fail.
constexpr std::array<Case, 7> kCases{{
    {"both", "SAMPLES/BOTH.WAV"},
    {"only_alt", "SAMPLES/ONLYALT.WAV"},
    {"both2", "SAMPLES/BOTH2.WAV"},
    {"only_reg", "SAMPLES/ONLYREG.WAV"},
    {"stereo24", "SAMPLES/STEREO24.WAV"},
    {"garbage", "SAMPLES/GARBAGE.WAV"},
    {"missing", "SAMPLES/MISSING.WAV"},
}};

/// How many times one case may answer LOAD_IN_PROGRESS before it is called wedged. Each deferral is
/// one dispatch round trip through the platform driver's executor, so this is generous: a healthy
/// async load takes one or two.
constexpr int32_t kMaxDeferrals = 2000;

/// Cursor over kCases.
size_t g_caseIndex = 0;
/// Deferrals recorded for the current case.
int32_t g_deferralsThisCase = 0;
/// Deferrals recorded across the whole run.
int32_t g_deferralsTotal = 0;
/// In-process assertion failures.
int32_t g_failures = 0;
/// Set false by dispatch_step(), true by the dispatched closure on its way out.
bool g_stepDone = true;
/// Set once the summary has been printed, so complete() latches.
bool g_summaryPrinted = false;

/// Plain CRC32 (IEEE 802.3, reflected) — a stable fingerprint of the bytes read back through the
/// Sample's own stream. Same routine host_loadcheck_main.cpp uses for band data.
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

/// The two invariants an async load must preserve (design §4b, §6): the manager holds no half-built
/// object (adopted and reasoned, but with zero geometry), and no load leaked a reason.
///
/// This is `host_loadcheck_main.cpp`'s `checkLoadInvariants` transplanted, not shared: that one
/// lives in an anonymous namespace inside a different binary's `main()` file (the C-host
/// `deluge_loadcheck`), which this binary does not link. Keeping them textually parallel is
/// deliberate — a change to what "half-built" means has to land in both.
///
/// @param label Printed with any failure, to say which case broke it.
/// @return true if both invariants hold across every resident AudioFile.
bool checkLoadInvariants(const char* label) {
	bool ok = true;
	for (int32_t i = 0; i < static_cast<int32_t>(audioFileManager.audioFiles.size()); i++) {
		AudioFile* f = audioFileManager.audioFiles[i];

		if (f->type == AudioFileType::SAMPLE) {
			auto* s = static_cast<Sample*>(f);
			if (s->byteDepth == 0 || s->numChannels == 0) {
				printf("FAIL [audioLoadAB/%s]: zero-geometry Sample in cache: %s\n", label, s->filePath.c_str());
				ok = false;
			}
		}

		if (f->leaseCount() != 0) {
			printf("FAIL [audioLoadAB/%s]: leaked reason(s) on %s (leaseCount=%u)\n", label, f->filePath.c_str(),
			       f->leaseCount());
			ok = false;
		}
	}
	return ok;
}

/// Read the first bytes of the Sample back through its OWN read source and fingerprint them.
///
/// This is the half of "identical geometry" that a field-by-field comparison cannot give: a build
/// whose read stream was never bound loads with no error and then plays silence forever, and two
/// builds that opened DIFFERENT files agree on every geometry field whenever those files happen to
/// share a header shape. The fingerprint covers the file's header and the first data bytes, and the
/// fixture gives every file a distinct data fill, so it says which file this actually is.
///
/// @param s The published Sample.
/// @return The CRC32 of the first 64 bytes, or 0 if no read source could be made or the read failed
///         (both of which are themselves reported by the caller, since 0 is also a legal CRC).
uint32_t streamFingerprint(Sample& s, bool& readable) {
	std::array<std::byte, 64> probe{};
	auto readSource = s.stream().make_read_source();
	if (readSource == nullptr || !readSource->read(0, probe).has_value()) {
		readable = false;
		return 0;
	}
	readable = true;
	return crc32(probe.data(), probe.size());
}

/// Print one case's whole outcome as a single stable, diffable line.
///
/// Every field here must match between the two builds. Nothing that legitimately differs between
/// them (deferral counts, which backing ran, timings, pointers) appears on this line — those go to
/// `AB-SUMMARY`, which the diff does not cover.
void printCaseLine(const Case& c, AudioFile* file, Error error, const std::string& fileNameAfter) {
	if (file == nullptr) {
		printf("AB-CASE %-9s | error=%d | file=none | asked=\"%s\" | altDirStatus=%d\n", c.label,
		       static_cast<int>(error), fileNameAfter.c_str(),
		       static_cast<int>(audioFileManager.alternateLoadDirStatus));
		return;
	}

	auto& s = static_cast<Sample&>(*file);
	bool readable = false;
	const uint32_t fingerprint = streamFingerprint(s, readable);
	if (!readable) {
		printf("FAIL [audioLoadAB/%s]: the published Sample cannot be read -- its stream was never bound, so it "
		       "would load without error and play silence\n",
		       c.label);
		g_failures++;
	}

	printf("AB-CASE %-9s | error=%d | file=\"%s\" | alt=\"%s\" | asked=\"%s\" | channels=%u byteDepth=%u "
	       "rawFormat=%u rate=%u dataStart=%u dataLen=%llu lengthSamples=%llu loop=%u/%u wtCycle=%u midiNote=%.4f | "
	       "readable=%d streamCrc=%08X | altDirStatus=%d\n",
	       c.label, static_cast<int>(error), s.filePath.c_str(), s.loadedFromAlternatePath.c_str(),
	       fileNameAfter.c_str(), s.numChannels, s.byteDepth, static_cast<unsigned>(s.rawDataFormat), s.sampleRate,
	       s.audioDataStartPosBytes, static_cast<unsigned long long>(s.audioDataLengthBytes),
	       static_cast<unsigned long long>(s.lengthInSamples), s.fileLoopStartSamples, s.fileLoopEndSamples,
	       s.waveTableCycleSize, static_cast<double>(s.midiNoteFromFile), readable ? 1 : 0, fingerprint,
	       static_cast<int>(audioFileManager.alternateLoadDirStatus));
}

/// Print the per-run facts that are EXPECTED to differ between the two builds, and assert the one
/// thing each build must independently be able to say about itself: that the backing it was built
/// with is the backing that actually ran.
///
/// This is what stops the A/B being vacuous. Two runs of the synchronous path agree perfectly and
/// prove nothing; the async run has to show that it went through the collector — that at least one
/// load came back LOAD_IN_PROGRESS and was answered on a later tick — rather than having been
/// served synchronously (or from memory) with the loader never consulted.
void printSummary() {
	const bool active = deluge_audio_load_async_active();
	printf("AB-SUMMARY asyncActive=%d cases=%zu deferrals=%d failures=%d\n", active ? 1 : 0, kCases.size(),
	       g_deferralsTotal, g_failures);

	if (active && g_deferralsTotal == 0) {
		printf("FAIL [audioLoadAB]: the async backing is active but no load was ever deferred -- every case was "
		       "answered without the collector, so this run is not the async leg it claims to be\n");
		g_failures++;
	}
	if (!active && g_deferralsTotal != 0) {
		printf("FAIL [audioLoadAB]: the async backing is inactive but %d load(s) reported LOAD_IN_PROGRESS\n",
		       g_deferralsTotal);
		g_failures++;
	}
	// Reprint with the summary's own failures folded in, so a reader of the last line sees the real
	// count rather than the count as it stood before these two checks ran.
	printf("AB-RESULT failures=%d\n", g_failures);
	fflush(stdout);
}

/// One load attempt for the current case, on the storage-owner worker fiber.
///
/// Either it defers (LOAD_IN_PROGRESS: return, the platform driver lets the loader task run and
/// dispatches again) or it settles the case: record the line, check the invariants, drop the file's
/// residency so nothing later is answered from memory, and advance.
void runOneStep() {
	const Case& c = kCases[g_caseIndex];

	// getAudioFileFromFilename may REWRITE fileName to the location the file was actually found at,
	// so it takes a fresh copy each attempt and the rewritten value is printed as `asked=`.
	std::string fileName = c.path;
	Error error = Error::NONE;
	AudioFile* file =
	    audioFileManager.getAudioFileFromFilename(fileName, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE,
	                                              /*makeWaveTableWorkAtAllCosts=*/false,
	                                              /*mayLoadAsync=*/true);

	if (error == Error::LOAD_IN_PROGRESS) {
		g_deferralsThisCase++;
		g_deferralsTotal++;
		if (g_deferralsThisCase <= kMaxDeferrals) {
			return; // Ask again next tick — the contract LOAD_IN_PROGRESS imposes on its caller.
		}
		printf("FAIL [audioLoadAB/%s]: still LOAD_IN_PROGRESS after %d attempts -- the load never completed\n", c.label,
		       g_deferralsThisCase);
		g_failures++;
		// Fall through and record it as wedged, so the run finishes and both builds' lines stay
		// comparable rather than one of them ending early.
		file = nullptr;
		error = Error::LOAD_IN_PROGRESS;
	}

	printCaseLine(c, file, error, fileName);

	if (!checkLoadInvariants(c.label)) {
		g_failures++;
	}

	// Drop residency before the next case. Nothing here shares a path, so this is not what keeps the
	// comparison honest on its own -- but a resident AudioFile short-circuits the load before the
	// async gate is even consulted, and leaving the cache to grow across cases would make that
	// short-circuit one edit away.
	audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(c.path);

	g_caseIndex++;
	g_deferralsThisCase = 0;

	if (g_caseIndex == kCases.size()) {
		printSummary();
		g_summaryPrinted = true;
	}
}

} // namespace

extern "C" {

void deluge_scenario_audio_load_ab_begin(void) {
	g_caseIndex = 0;
	g_deferralsThisCase = 0;
	g_deferralsTotal = 0;
	g_failures = 0;
	g_stepDone = true;
	g_summaryPrinted = false;

	// The state a song load starts from, which is the only state in which the alternate-dir ordering
	// this compares means anything: the directory MIGHT exist, and nothing has probed it yet.
	audioFileManager.alternateAudioFileLoadPath = "SAMPLES/ALT";
	audioFileManager.alternateLoadDirStatus = AlternateLoadDirStatus::MIGHT_EXIST;
	audioFileManager.thingTypeBeingLoaded = ThingType::SONG;

	// Line-buffer stdout for the rest of the run. The platform driver ends the process with a raw
	// `exit_group` syscall (golden_vt_render's `hard_exit`, which deliberately skips C++ static
	// destructors), and that flushes NOTHING of C's stdio — so a block-buffered stdout, which is
	// what stdout is whenever this harness's output is piped or redirected, discards every line
	// this scenario printed. Losing exactly the output the gate diffs.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	printf("AB-BEGIN asyncActive=%d cases=%zu\n", deluge_audio_load_async_active() ? 1 : 0, kCases.size());
}

bool deluge_scenario_audio_load_ab_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_audio_load_ab_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// Onto the storage-owner worker fiber: the attempt opens files and binds a read stream, which is
	// card I/O and has to run where card I/O is allowed. Same dispatch shape as
	// deluge_scenario_start_recorder_roundtrip.
	const bool queued = deluge::storage::Owner::run(
	    [](void*) {
		    runOneStep();
		    g_stepDone = true;
	    },
	    nullptr);
	if (!queued) {
		// The worker queue was full, so the closure will NOT run: release the latch rather than
		// letting the platform driver wait forever for a step that was dropped.
		g_stepDone = true;
	}
}

bool deluge_scenario_audio_load_ab_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_audio_load_ab_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
