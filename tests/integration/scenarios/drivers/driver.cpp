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

#include "scenarios/drivers/driver.h"

#include "model/edits/param_address_for.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/waves.h"

#include <bitset>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// playback_handler.cpp's, not in its header.
extern bool currentlyActioningSwungTickOrResettingPlayPos;

namespace deluge::scenarios::driver {

namespace {

bool g_done = false;

// The random streams' static initial values (util/waves.cpp, util/functions.cpp).
constexpr uint32_t kJcongSeed = 380116160;
constexpr uint32_t kZSeed = 362436069;
constexpr uint32_t kWSeed = 521288629;

/// The arguments DRV_ARGS names, after argv[0].
std::vector<std::string> arguments() {
	std::vector<std::string> args;
	const char* all = std::getenv("DRV_ARGS");
	if (all == nullptr || *all == '\0') {
		return args;
	}
	std::string current;
	for (const char* c = all; *c != '\0'; c++) {
		if (*c == '\x1f') {
			args.push_back(current);
			current.clear();
		}
		else {
			current.push_back(*c);
		}
	}
	args.push_back(current);
	return args;
}

/// Holds @p value in @p ref for its lifetime, then puts back what was there.
template <class T>
class Pinned {
public:
	Pinned(T& ref, T value) : ref_(ref), saved_(ref) { ref_ = value; }
	~Pinned() { ref_ = saved_; }
	Pinned(const Pinned&) = delete;
	Pinned& operator=(const Pinned&) = delete;

private:
	T& ref_;
	T saved_;
};

/// Hand @p len bytes of a driver's stream on: to the file @p fileVar names, or to stdout as `prefix` lines.
void deliver(const char* bytes, size_t len, const char* fileVar, const char* prefix) {
	if (const char* path = std::getenv(fileVar); path != nullptr && *path != '\0') {
		if (FILE* f = std::fopen(path, "wb")) {
			std::fwrite(bytes, 1, len, f);
			std::fclose(f);
		}
		return;
	}
	size_t start = 0;
	while (start < len) {
		const char* nl = static_cast<const char*>(std::memchr(bytes + start, '\n', len - start));
		const size_t end = nl == nullptr ? len : static_cast<size_t>(nl - bytes);
		std::printf("%s %.*s\n", prefix, static_cast<int>(end - start), bytes + start);
		start = end + 1;
	}
}

} // namespace

void begin() {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_done = false;
}

bool complete() {
	return g_done;
}

void dispatch(Main main) {
	if (g_done) {
		return;
	}
	std::vector<std::string> args = arguments();
	const char* name = std::getenv("GOLDEN_SCENARIO");
	std::string argv0 = name != nullptr ? name : "driver";
	std::vector<char*> argv;
	argv.push_back(argv0.data());
	for (std::string& arg : args) {
		argv.push_back(arg.data());
	}
	argv.push_back(nullptr);

	// DRV_DIRECT: print straight to the process's own streams, unprefixed, so what a driver printed survives it
	// crashing the harness.
	const bool direct = std::getenv("DRV_DIRECT") != nullptr;
	char* outBytes = nullptr;
	size_t outLen = 0;
	char* errBytes = nullptr;
	size_t errLen = 0;
	FILE* out = direct ? stdout : open_memstream(&outBytes, &outLen);
	FILE* err = direct ? stderr : open_memstream(&errBytes, &errLen);

	int code = 0;
	{
		// What a driver's own process started from, which its baselines were recorded against: no current or
		// preloaded song until it sets one (a load converts some values relative to the current song's when there is
		// one), the random streams at their static seeds (a load fills in what a file leaves out, an arpeggiator's
		// note pattern say, from them), and the settings it never read at zero (a default a file leaves out comes
		// from them: an arpeggiator's sync level from the default magnitude, say).
		Pinned current(currentSong, static_cast<Song*>(nullptr));
		Pinned preloaded(preLoadedSong, static_cast<Song*>(nullptr));
		Pinned reader(activeReader, activeReader);
		Pinned fileReader(activeDeserializer, activeDeserializer);
		Pinned sampleTimer(AudioEngine::audioSampleTimer, AudioEngine::audioSampleTimer);
		Pinned randomJ(jcong, kJcongSeed);
		Pinned randomZ(z, kZSeed);
		Pinned randomW(w, kWSeed);
		Pinned scale(FlashStorage::defaultScale, uint8_t{0});
		Pinned margins(FlashStorage::audioClipRecordMargins, false);
		Pinned velocity(FlashStorage::defaultVelocity, uint8_t{0});
		Pinned magnitude(FlashStorage::defaultMagnitude, int8_t{0});
		Pinned bendRange0(FlashStorage::defaultBendRange[0], uint8_t{0});
		Pinned bendRange1(FlashStorage::defaultBendRange[1], uint8_t{0});
		Pinned sessionLayout(FlashStorage::defaultSessionLayout, SessionLayoutType{});
		Pinned automationClear(FlashStorage::automationClear, false);
		Pinned startupSongMode(FlashStorage::defaultStartupSongMode, StartupSongMode{});
		Pinned swingInterval(FlashStorage::defaultSwingInterval, uint8_t{0});
		Pinned disabledScales(FlashStorage::defaultDisabledPresetScales, std::bitset<NUM_PRESET_SCALES>{});
		Pinned thresholdMode(FlashStorage::defaultThresholdRecordingMode, ThresholdRecordingMode{});
		Pinned cablePolarity(FlashStorage::defaultPatchCablePolarity, Polarity{});
		Pinned recordQuantize(FlashStorage::recordQuantizeLevel, uint8_t{0});
		// The doc() fallback tally counts from zero for a driver, as it did from its process start: a driver that
		// requires no fallback hits means none of its own, not none since boot.
		Pinned fallbackHits(deluge::edits::docFallbackHits, uint32_t{0});
		// Driven playback (param_record_diff's), which the booted app must not find still running afterwards.
		Pinned playbackState(playbackHandler.playbackState, playbackHandler.playbackState);
		Pinned recording(playbackHandler.recording, playbackHandler.recording);
		Pinned countIn(playbackHandler.ticksLeftInCountIn, playbackHandler.ticksLeftInCountIn);
		Pinned swungTick(currentlyActioningSwungTickOrResettingPlayPos, currentlyActioningSwungTickOrResettingPlayPos);

		code = main(static_cast<int>(argv.size()) - 1, argv.data(), Io{out, err});
	}
	// A driver that serialized into memory leaves the shared serializers that way; the app's own saves need them
	// reaching the card again.
	smSerializer.clearMemoryBased();
	smJsonSerializer.clearMemoryBased();

	if (!direct) {
		std::fclose(out);
		std::fclose(err);
		deliver(outBytes, outLen, "DRV_OUT", "DRV-OUT");
		deliver(errBytes, errLen, "DRV_ERR", "DRV-ERR");
		std::free(outBytes);
		std::free(errBytes);
	}

	std::printf("DRV-EXIT code=%d\n", code);
	std::fflush(stdout);
	g_done = true;
}

bool stepDone() {
	return true;
}

int32_t failures() {
	// The exit code is the driver's verdict, and not every non-zero one is a failure (a song a runner skips, say):
	// DRV-EXIT carries it to whoever reads the run.
	return 0;
}

void beginCapture(FileWriter& writer) {
	writer.setMemoryBased();
}

long endCapture(FileWriter& writer, char* buf, uint32_t cap) {
	const int32_t written = writer.bytesWritten();
	long result = -1;
	if (written >= 0 && static_cast<uint32_t>(written) < cap) {
		std::memcpy(buf, writer.getBufferPtr(), static_cast<size_t>(written));
		buf[written] = '\0';
		result = written;
	}
	writer.clearMemoryBased();
	return result;
}

} // namespace deluge::scenarios::driver
