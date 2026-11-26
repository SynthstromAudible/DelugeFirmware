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

/// @file
/// @brief Drives a sample's sidecar through the card: written once its index completes, restored on reload with no
///        audio read, refused once its file changes, skipped on a card that will not take it, written over a stray
///        `.TMP`, and written for a partial index only as it grows.
///
/// Each stage ticks the idle job (`AudioFileManager::backgroundWaveformScan`) once per step, as the app's slow routine
/// does; the byte channel's operations run between steps. The checks read the card directly through `deluge::io`.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app` (the harness directory is globbed into the app's sources).

#include "scenarios/support/scenario_declare.h"
#include "scenarios/support/scenario_report.h"

#include "definitions_cxx.hpp"
#include "deluge_storage_sample_peaks.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "model/sample/pitch_scan.h"
#include "model/sample/sample.h"
#include "storage/audio/audio_file.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/audio/audio_load_context.h"
#include "storage/audio/peak_sidecars.h"
#include "storage/audio/sidecar_path.h"
#include "storage/audio/sidecar_policy.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The GUI headers above reach `third_party/printf/printf.h`, which soft-aliases printf to an implementation this
// harness has no `putchar_` for.
#ifdef printf
#undef printf
#endif

DELUGE_SCENARIO_DECLARE(peak_sidecars);

namespace {

using deluge::scenarios::ScenarioReport;
using deluge::storage::SidecarFile;
using deluge::storage::sidecarPath;

constexpr const char* kLongPath = "SAMPLES/SCLONG.WAV";
constexpr const char* kFullPath = "SAMPLES/SCFULL.WAV";
constexpr const char* kProtPath = "SAMPLES/SCPROT.WAV";
constexpr const char* kTmpPath = "SAMPLES/SCTMP.WAV";
constexpr const char* kPartPath = "SAMPLES/SCPART.WAV";

/// Steps the boot is given before the first load.
constexpr int32_t kSettleSteps = 64;
/// The most steps any wait below is given: many times the one-second write search, and the scan of the longest file.
constexpr int32_t kWaitSteps = 60'000;
/// Steps a "nothing more happens" stage holds for: three of the write search's one-second periods.
constexpr int32_t kHoldSteps = 3'000;

enum class Stage : uint8_t {
	Settle,
	LoadLong,
	AwaitLongWritten,
	DetectPitch,
	AwaitPitchWritten,
	Reload,
	AwaitReloadLooked,
	Rewrite,
	AwaitRewriteLooked,
	AwaitRewriteWritten,
	Corrupt,
	AwaitCorruptLooked,
	AwaitCorruptRemoved,
	LoadFull,
	AwaitFullFailed,
	HoldFull,
	LoadProt,
	AwaitProtFailed,
	HoldProt,
	LoadTmp,
	AwaitTmpLooked,
	AwaitTmpWritten,
	LoadPart,
	AwaitPartLooked,
	AwaitPartFirst,
	HoldPart,
	AwaitPartSecond,
	Report,
	Done,
};

Stage g_stage = Stage::Settle;
int32_t g_steps = 0; ///< steps in the current stage
bool g_stepDone = false;
bool g_summaryPrinted = false;
ScenarioReport g_report{"PS"};
Sample* g_sample = nullptr; ///< the sample the current stages drive
deluge::sample::PitchScan g_scan;
float g_hz = 0;
uint32_t g_firstWrite = 0; ///< complete level-1 entries the partial index's first sidecar held
uint64_t g_firstSize = 0;  ///< SCLONG.WAV's size before the rewrite

void next(Stage s) {
	g_stage = s;
	g_steps = 0;
}

/// Ends the current wait on @p done, or fails it (naming @p what) once it has run kWaitSteps steps. True when it ended
/// either way, so the stage moves on.
bool waited(bool done, const char* what) {
	if (done) {
		return true;
	}
	if (g_steps > kWaitSteps) {
		g_report.fail("%s: not within %d steps", what, kWaitSteps);
		return true;
	}
	return false;
}

Sample* load(const char* path) {
	std::string name = path;
	Error error = Error::NONE;
	deluge::storage::AudioLoadContext context{};
	AudioFile* file = audioFileManager.getAudioFileFromFilename(
	    name, /*mayReadCard=*/true, &error, AudioFileType::SAMPLE, /*makeWaveTableWorkAtAllCosts=*/false,
	    deluge::storage::AsyncLoadMode::Blocking, &context);
	if (file == nullptr || !AudioFile::isSample(file)) {
		g_report.fail("%s did not load (error %d)", path, static_cast<int32_t>(error));
		return nullptr;
	}
	file->addReason();
	return static_cast<Sample*>(file);
}

void unload(Sample*& sample) {
	if (sample != nullptr) {
		sample->removeReason("PSUN");
		(void)audioFileManager.releaseUnleasedAudioFiles();
		sample = nullptr;
	}
}

std::optional<std::vector<uint8_t>> readCard(std::string_view path) {
	auto file = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!file) {
		return std::nullopt;
	}
	const auto size = file->size();
	if (!size) {
		return std::nullopt;
	}
	std::vector<uint8_t> bytes(*size);
	const auto got = file->read(std::as_writable_bytes(std::span{bytes}));
	if (!got || got->size() != bytes.size()) {
		return std::nullopt;
	}
	return bytes;
}

bool writeCard(std::string_view path, std::span<const uint8_t> bytes) {
	auto file = deluge::io::File::open(path, DELUGE_FILE_WRITE_CREATE);
	if (!file) {
		return false;
	}
	const auto put = file->write(std::as_bytes(bytes));
	return put && *put == bytes.size() && file->close();
}

std::string pathOf(const Sample& sample, SidecarFile file) {
	const std::optional<DelugeFileIdentity> id = sample.fileIdentity();
	return id ? std::string{sidecarPath(id->first_cluster, file).data()} : std::string{};
}

bool onCard(std::string_view path) {
	return deluge::io::File::open(path, DELUGE_FILE_READ).has_value();
}

uint32_t entries(const Sample& sample) {
	return deluge_peaks_level1_entries(sample.peaks());
}

bool complete(const Sample& sample) {
	return deluge_peaks_next_unscanned(sample.peaks(), 0) == UINT64_MAX;
}

/// The sidecar on the card for @p sample checks out against its key.
bool sidecarSound(const Sample& sample, std::optional<std::vector<uint8_t>>* bytesOut = nullptr) {
	const std::optional<DelugePeakKey> key = deluge::storage::sidecarKey(sample);
	std::optional<std::vector<uint8_t>> bytes = readCard(pathOf(sample, SidecarFile::PK));
	const bool sound =
	    key && bytes && deluge_peaks_sidecar_check(bytes->data(), static_cast<uint32_t>(bytes->size()), &*key) == 0;
	if (bytesOut != nullptr) {
		*bytesOut = std::move(bytes);
	}
	return sound;
}

/// Completes level-1 entries of @p sample's index from entry 0 until @p want are complete: synthetic peaks, folded as
/// the scan would fold blocks.
void foldUntil(Sample& sample, uint32_t want) {
	for (uint64_t e = 0; e < entries(sample) && deluge_peaks_complete_level1(sample.peaks()) < want; e++) {
		for (uint64_t b = e * 16; b < e * 16 + 16; b++) {
			deluge_peaks_fold_block(sample.peaks(), b, DelugePeak{.min = -100, .max = 100});
		}
	}
}

const deluge::sample::PitchScan::Limits kDefaultLimits{};

bool pitchRemembered(Sample& sample, float hz) {
	const std::optional<float> found = sample.pitchResults().find({.minFreqHz = kDefaultLimits.minFreqHz,
	                                                               .maxFreqHz = kDefaultLimits.maxFreqHz,
	                                                               .primeTest = kDefaultLimits.doPrimeTest});
	return found && *found == hz;
}

void runStep() {
	g_steps++;
	audioFileManager.backgroundWaveformScan();
	switch (g_stage) {
	case Stage::Settle:
		if (g_steps >= kSettleSteps) {
			next(Stage::LoadLong);
		}
		return;

	case Stage::LoadLong:
		audioFileManager.setBackgroundScansEnabled(true);
		g_sample = load(kLongPath);
		next(g_sample != nullptr ? Stage::AwaitLongWritten : Stage::Report);
		return;

	case Stage::AwaitLongWritten:
		if (waited(g_sample->sidecar.written == entries(*g_sample) && !audioFileManager.peakSidecars().busy(),
		           "SCLONG.WAV's complete sidecar")) {
			const std::optional<DelugePeakKey> key = deluge::storage::sidecarKey(*g_sample);
			const bool sound = sidecarSound(*g_sample);
			const bool noTmp = !onCard(pathOf(*g_sample, SidecarFile::TMP));
			printf("PS-WRITE entries=%u written=%u sound=%d tmp_gone=%d path=%s\n", entries(*g_sample),
			       g_sample->sidecar.written, sound ? 1 : 0, noTmp ? 1 : 0, pathOf(*g_sample, SidecarFile::PK).c_str());
			if (!key || !sound || !noTmp) {
				g_report.fail("a complete index must leave a sound sidecar and no .TMP");
			}
			g_scan.arm(*g_sample, kDefaultLimits);
			next(Stage::DetectPitch);
		}
		return;

	case Stage::DetectPitch:
		if (g_scan.tick() == deluge::sample::PitchScan::Progress::Done || g_steps > kWaitSteps) {
			g_hz = g_scan.result();
			next(Stage::AwaitPitchWritten);
		}
		return;

	case Stage::AwaitPitchWritten:
		if (waited(!g_sample->pitchResults().unwritten() && !audioFileManager.peakSidecars().busy(),
		           "SCLONG.WAV's pitch written")) {
			std::optional<std::vector<uint8_t>> bytes;
			const bool sound = sidecarSound(*g_sample, &bytes);
			const std::optional<DelugePeakKey> key = deluge::storage::sidecarKey(*g_sample);
			const uint8_t* ptch = nullptr;
			uint32_t ptchLen = 0;
			const bool hasPitch =
			    sound && key
			    && deluge_peaks_sidecar_section(bytes->data(), static_cast<uint32_t>(bytes->size()), &*key,
			                                    deluge::sample::PitchResults::kTag, &ptch, &ptchLen);
			printf("PS-PITCH-WRITE hz=%.2f section=%d\n", static_cast<double>(g_hz), hasPitch ? 1 : 0);
			if (!hasPitch || g_hz == 0.0f) {
				g_report.fail("a detected pitch must be written to the sidecar's PTCH section");
			}
			next(Stage::Reload);
		}
		return;

	case Stage::Reload:
		unload(g_sample);
		audioFileManager.setBackgroundScansEnabled(false); // only the sidecar may complete the index
		g_sample = load(kLongPath);
		next(g_sample != nullptr ? Stage::AwaitReloadLooked : Stage::Report);
		return;

	case Stage::AwaitReloadLooked:
		if (waited(g_sample->sidecar.looked && !audioFileManager.peakSidecars().busy(), "SCLONG.WAV's lookup")) {
			const bool restored = complete(*g_sample);
			printf("PS-RELOAD complete=%d complete_entries=%u of %u\n", restored ? 1 : 0,
			       deluge_peaks_complete_level1(g_sample->peaks()), entries(*g_sample));
			if (!restored) {
				g_report.fail("a reloaded sample's index must be complete from its sidecar alone");
			}
			const bool remembered = pitchRemembered(*g_sample, g_hz);
			deluge::sample::PitchScan scan;
			scan.arm(*g_sample, kDefaultLimits);
			const bool answered = scan.tick() == deluge::sample::PitchScan::Progress::Done && scan.result() == g_hz;
			printf("PS-PITCH remembered=%d answered=%d hz=%.2f\n", remembered ? 1 : 0, answered ? 1 : 0,
			       static_cast<double>(scan.result()));
			if (!remembered || !answered) {
				g_report.fail("a reloaded sample must answer its detected pitch from its sidecar");
			}
			next(Stage::Rewrite);
		}
		return;

	case Stage::Rewrite: {
		unload(g_sample);
		std::optional<std::vector<uint8_t>> audio = readCard(kLongPath);
		if (!audio) {
			g_report.fail("SCLONG.WAV could not be read back to rewrite it");
			next(Stage::LoadFull);
			return;
		}
		g_firstSize = audio->size();
		audio->resize(audio->size() + 512, 0); // a computer appended to it: same audio, another size
		if (!writeCard(kLongPath, *audio)) {
			g_report.fail("SCLONG.WAV could not be rewritten");
		}
		g_sample = load(kLongPath);
		next(g_sample != nullptr ? Stage::AwaitRewriteLooked : Stage::Report);
		return;
	}

	case Stage::AwaitRewriteLooked:
		if (waited(g_sample->sidecar.looked && !audioFileManager.peakSidecars().busy(),
		           "the rewritten file's lookup")) {
			const bool refused = !complete(*g_sample);
			printf("PS-REWRITE refused=%d\n", refused ? 1 : 0);
			if (!refused) {
				g_report.fail("a sidecar must not be restored once its file has changed");
			}
			audioFileManager.setBackgroundScansEnabled(true);
			next(Stage::AwaitRewriteWritten);
		}
		return;

	case Stage::AwaitRewriteWritten:
		if (waited(g_sample->sidecar.written == entries(*g_sample) && !audioFileManager.peakSidecars().busy(),
		           "the rewritten file's new sidecar")) {
			const bool sound = sidecarSound(*g_sample);
			const std::optional<DelugeFileIdentity> id = g_sample->fileIdentity();
			printf("PS-REWRITE-WRITTEN sound=%d size=%u was=%llu\n", sound ? 1 : 0, id ? id->size : 0,
			       static_cast<unsigned long long>(g_firstSize));
			if (!sound || !id || id->size != g_firstSize + 512) {
				g_report.fail("a rewritten file must get a new sidecar under its new key");
			}
			next(Stage::Corrupt);
		}
		return;

	case Stage::Corrupt: {
		// A sidecar damaged on the card: one header byte flipped, so its header checksum fails.
		const std::string pk = pathOf(*g_sample, SidecarFile::PK);
		unload(g_sample);
		std::optional<std::vector<uint8_t>> bytes = readCard(pk);
		if (!bytes || bytes->size() < 10) {
			g_report.fail("no sidecar at %s to damage", pk.c_str());
			next(Stage::LoadFull);
			return;
		}
		(*bytes)[9] ^= 0x40;
		if (!writeCard(pk, *bytes)) {
			g_report.fail("the sidecar at %s could not be damaged", pk.c_str());
		}
		audioFileManager.setBackgroundScansEnabled(false);
		g_sample = load(kLongPath);
		next(g_sample != nullptr ? Stage::AwaitCorruptLooked : Stage::Report);
		return;
	}

	case Stage::AwaitCorruptLooked:
		if (waited(g_sample->sidecar.looked, "the damaged sidecar's lookup")) {
			const bool refused = !complete(*g_sample);
			printf("PS-CORRUPT refused=%d\n", refused ? 1 : 0);
			if (!refused) {
				g_report.fail("a damaged sidecar must not be restored");
			}
			next(Stage::AwaitCorruptRemoved);
		}
		return;

	case Stage::AwaitCorruptRemoved:
		if (waited(!onCard(pathOf(*g_sample, SidecarFile::PK)), "the damaged sidecar's removal")) {
			printf("PS-CORRUPT-REMOVED gone=%d\n", onCard(pathOf(*g_sample, SidecarFile::PK)) ? 0 : 1);
			audioFileManager.setBackgroundScansEnabled(true);
			unload(g_sample);
			next(Stage::LoadFull);
		}
		return;

	case Stage::LoadFull:
		deluge_file_bytes_set_write_fault(DELUGE_ERR_NO_SPACE);
		g_sample = load(kFullPath);
		next(g_sample != nullptr ? Stage::AwaitFullFailed : Stage::Report);
		return;

	case Stage::AwaitFullFailed:
		if (waited(g_sample->sidecar.failures >= deluge::storage::kMaxSidecarFailures,
		           "two failed writes on a full card")) {
			next(Stage::HoldFull);
		}
		return;

	case Stage::HoldFull:
		if (g_steps >= kHoldSteps) {
			const bool stopped = g_sample->sidecar.failures == deluge::storage::kMaxSidecarFailures;
			const bool none = !onCard(pathOf(*g_sample, SidecarFile::PK));
			printf("PS-FULL failures=%u stopped=%d no_sidecar=%d\n", g_sample->sidecar.failures, stopped ? 1 : 0,
			       none ? 1 : 0);
			if (!stopped || !none) {
				g_report.fail("a full card must be tried twice, then left alone");
			}
			deluge_file_bytes_set_write_fault(DELUGE_OK);
			unload(g_sample);
			next(Stage::LoadProt);
		}
		return;

	case Stage::LoadProt:
		deluge_file_bytes_set_write_fault(DELUGE_ERR_WRITE_PROTECTED);
		g_sample = load(kProtPath);
		next(g_sample != nullptr ? Stage::AwaitProtFailed : Stage::Report);
		return;

	case Stage::AwaitProtFailed:
		if (waited(g_sample->sidecar.failures >= deluge::storage::kMaxSidecarFailures,
		           "two failed writes on a write-protected card")) {
			next(Stage::HoldProt);
		}
		return;

	case Stage::HoldProt:
		if (g_steps >= kHoldSteps) {
			const bool stopped = g_sample->sidecar.failures == deluge::storage::kMaxSidecarFailures;
			const bool none = !onCard(pathOf(*g_sample, SidecarFile::PK));
			printf("PS-PROTECT failures=%u stopped=%d no_sidecar=%d\n", g_sample->sidecar.failures, stopped ? 1 : 0,
			       none ? 1 : 0);
			if (!stopped || !none) {
				g_report.fail("a write-protected card must be tried twice, then left alone");
			}
			deluge_file_bytes_set_write_fault(DELUGE_OK);
			unload(g_sample);
			next(Stage::LoadTmp);
		}
		return;

	case Stage::LoadTmp:
		audioFileManager.setBackgroundScansEnabled(false); // no write until the stray .TMP is in place
		g_sample = load(kTmpPath);
		next(g_sample != nullptr ? Stage::AwaitTmpLooked : Stage::Report);
		return;

	case Stage::AwaitTmpLooked:
		if (waited(g_sample->sidecar.looked && !audioFileManager.peakSidecars().busy(), "SCTMP.WAV's lookup")) {
			const std::string tmp = pathOf(*g_sample, SidecarFile::TMP);
			const std::string shard = tmp.substr(0, tmp.rfind('/'));
			(void)deluge::io::mkdir("DELUGE");
			(void)deluge::io::mkdir("DELUGE/PEAKS");
			(void)deluge::io::mkdir(shard);
			const std::vector<uint8_t> junk(100, 0x5A);
			if (!writeCard(tmp, junk)) {
				g_report.fail("the stray .TMP could not be placed at %s", tmp.c_str());
			}
			audioFileManager.setBackgroundScansEnabled(true);
			next(Stage::AwaitTmpWritten);
		}
		return;

	case Stage::AwaitTmpWritten:
		if (waited(g_sample->sidecar.written == entries(*g_sample) && !audioFileManager.peakSidecars().busy(),
		           "SCTMP.WAV's sidecar over a stray .TMP")) {
			const bool sound = sidecarSound(*g_sample);
			const bool tmpGone = !onCard(pathOf(*g_sample, SidecarFile::TMP));
			printf("PS-TMP sound=%d tmp_gone=%d\n", sound ? 1 : 0, tmpGone ? 1 : 0);
			if (!sound || !tmpGone) {
				g_report.fail("a stray .TMP must be overwritten by its key's next write");
			}
			unload(g_sample);
			next(Stage::LoadPart);
		}
		return;

	case Stage::LoadPart:
		audioFileManager.setBackgroundScansEnabled(false); // the index grows only as this stage folds it
		g_sample = load(kPartPath);
		next(g_sample != nullptr ? Stage::AwaitPartLooked : Stage::Report);
		return;

	case Stage::AwaitPartLooked:
		if (waited(g_sample->sidecar.looked && !audioFileManager.peakSidecars().busy(), "SCPART.WAV's lookup")) {
			foldUntil(*g_sample, (entries(*g_sample) + 3) >> 2); // a quarter
			next(Stage::AwaitPartFirst);
		}
		return;

	case Stage::AwaitPartFirst:
		if (waited(g_sample->sidecar.written != 0 && !audioFileManager.peakSidecars().busy(),
		           "a quarter-complete index's first sidecar")) {
			g_firstWrite = g_sample->sidecar.written;
			foldUntil(*g_sample, g_firstWrite + 1); // short of a quarter's growth
			next(Stage::HoldPart);
		}
		return;

	case Stage::HoldPart:
		if (g_steps >= kHoldSteps) {
			const bool held = g_sample->sidecar.written == g_firstWrite;
			foldUntil(*g_sample, ((g_firstWrite * 5) + 3) >> 2); // a quarter's growth
			printf("PS-PARTIAL-HOLD first=%u held=%d\n", g_firstWrite, held ? 1 : 0);
			if (!held) {
				g_report.fail("a partial index must not be written again before it grows by a quarter");
			}
			next(Stage::AwaitPartSecond);
		}
		return;

	case Stage::AwaitPartSecond:
		if (waited(g_sample->sidecar.written > g_firstWrite && !audioFileManager.peakSidecars().busy(),
		           "a partial index's second sidecar")) {
			const bool sound = sidecarSound(*g_sample);
			printf("PS-PARTIAL first=%u second=%u of %u sound=%d\n", g_firstWrite, g_sample->sidecar.written,
			       entries(*g_sample), sound ? 1 : 0);
			if (!sound || g_firstWrite * 4 < entries(*g_sample)) {
				g_report.fail("a partial index must be written from a quarter, and again after a quarter's growth");
			}
			unload(g_sample);
			next(Stage::Report);
		}
		return;

	case Stage::Report:
		audioFileManager.setBackgroundScansEnabled(true);
		deluge_file_bytes_set_write_fault(DELUGE_OK);
		g_report.printResult();
		g_summaryPrinted = true;
		next(Stage::Done);
		return;

	case Stage::Done:
		return;
	}
}

} // namespace

extern "C" {

void deluge_scenario_peak_sidecars_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Settle;
	g_steps = 0;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_report.reset();
	g_sample = nullptr;
	g_hz = 0;
	g_firstWrite = 0;
	g_firstSize = 0;
}

bool deluge_scenario_peak_sidecars_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_peak_sidecars_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runStep();
	g_stepDone = true;
}

bool deluge_scenario_peak_sidecars_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_peak_sidecars_failures(void) {
	return g_report.failures();
}

} // extern "C"
