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

// preset_parse — reading a preset document: a JSON synth preset reads into a kit row as an XML one does, and a preset
// that cannot be read fails to load with an error, leaving nothing behind (the instrument it began is deleted whole).

#include "memory/general_memory_allocator.h"
#include "model/global_effectable/global_effectable.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "storage/storage_manager.h"
#include "storage/tree_document.h"

#include <cstdio>
#include <new>
#include <span>
#include <string>
#include <string_view>

namespace {

using deluge::scenarios::driver::Io;

/// Where this driver's prints go: what its process wrote to stdout and stderr.
Io io{};
int failures = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		failures++;
		std::fprintf(io.err, "PRESET-PARSE FAIL %s\n", what);
	}
}

/// Loads @p text as a preset of @p type and checks it fails with nothing returned.
void expectFailure(std::string_view text, bool isJson, OutputType type, const char* what) {
	char const* firstTag = nullptr;
	char const* altTag = nullptr;
	StorageManager::instrumentDocumentTags(type, &firstTag, &altTag);
	deluge::storage::TreeDocument document;
	const Error opened =
	    document.open(std::span{reinterpret_cast<const uint8_t*>(text.data()), text.size()}, isJson, firstTag, altTag);
	check(opened == Error::NONE, what);
	Instrument* instrument = nullptr;
	std::string name = "BROKEN";
	std::string dir = type == OutputType::SYNTH ? "SYNTHS" : "KITS";
	const Error error =
	    StorageManager::parseInstrumentDocument(document, currentSong, nullptr, type, false, &instrument, &name, &dir);
	document.release();
	check(error != Error::NONE && instrument == nullptr, what);
}

/// A song for the preset to be loaded into, as a preset is always loaded into one.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	return song;
}

int preset_parse_main(int /*argc*/, char** /*argv*/, Io driverIo) {
	io = driverIo;
	currentSong = freshSong(); // the driver step puts the booted song back afterwards
	expectFailure(R"(<?xml version="1.0" encoding="UTF-8"?><sound mode="subtractive"></sound>)", false,
	              OutputType::SYNTH, "a synth preset without params");
	expectFailure(R"({"firmwareVersion": "c1.3.0", "sound": {"mode": "subtractive"}})", true, OutputType::SYNTH,
	              "a JSON synth preset without params");
	{
		constexpr std::string_view kJsonSynth = R"({"firmwareVersion": "c1.3.0", "sound": {"mode": "subtractive",
			"defaultParams": {"volume": "0x50000000"}}})";
		deluge::storage::TreeDocument document;
		check(document.open(std::span{reinterpret_cast<const uint8_t*>(kJsonSynth.data()), kJsonSynth.size()}, true,
		                    "sound", "synth")
		          == Error::NONE,
		      "a JSON synth preset opens for a kit row");
		SoundDrum* drum = nullptr;
		const Error error = StorageManager::parseSynthDrumDocument(document, currentSong, nullptr, &drum);
		document.release();
		check(error == Error::NONE && drum != nullptr, "a JSON synth preset reads into a kit row");
		check(drum != nullptr && currentSong->live.param_backup.getPreferablyWithClip(drum, nullptr) != nullptr,
		      "a JSON synth preset read into a kit row has its params");
	}
	{
		// An unstamped synth with its filter open and half resonance: no old-file rule changes it (the upgrader
		// converts files that old), so it reads -- and saves -- as its file says.
		constexpr std::string_view kOldSynth =
		    R"(<?xml version="1.0" encoding="UTF-8"?><sound mode="subtractive"><defaultParams lpfFrequency="0x7FFFFFFF" )"
		    R"(lpfResonance="0x00000000" volume="0x40000000"/></sound>)";
		deluge::storage::TreeDocument document;
		check(document.open(std::span{reinterpret_cast<const uint8_t*>(kOldSynth.data()), kOldSynth.size()}, false,
		                    "sound", "synth")
		          == Error::NONE,
		      "an old synth preset opens");
		Instrument* instrument = nullptr;
		std::string name = "OLD";
		std::string dir = "SYNTHS";
		const Error error = StorageManager::parseInstrumentDocument(document, currentSong, nullptr, OutputType::SYNTH,
		                                                            false, &instrument, &name, &dir);
		document.release();
		check(error == Error::NONE && instrument != nullptr, "an old synth preset loads");
		if (instrument != nullptr) {
			auto* sound = static_cast<SoundInstrument*>(instrument);
			ParamManager* manager = currentSong->live.param_backup.getPreferablyWithClip(sound, nullptr);
			PatchedParamSet* patched = manager != nullptr ? manager->getPatchedParamSet() : nullptr;
			check(patched != nullptr
			          && patched->docForParam(deluge::modulation::params::LOCAL_LPF_RESONANCE)->current_value == 0,
			      "an unstamped synth's resonance reads as its file says");
		}
	}
	if (failures == 0) {
		std::fprintf(io.out, "preset_parse: OK\n");
	}
	return failures ? 1 : 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(preset_parse, preset_parse_main)
