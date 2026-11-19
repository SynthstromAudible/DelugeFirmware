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

// Host cost of a sound's settings Edits, run as the edit_cost scenario: a new synth in a fresh song takes each Edit
// below, alternating between two values, and the median and 95th-percentile time of one Edit is printed. A
// measurement, not a check: it always returns 0.
//
// Gate edit-cost:
//     nothing: it times SetEngineSetting on a new synth for a re-voicing scalar (unison detune), a retuning scalar
//     (oscillator transpose), an oscillator type switch and an engine switch, and prints one line per Edit

#include "definitions_cxx.hpp"
#include "memory/general_memory_allocator.h"
#include "model/edits/edit.h"
#include "model/edits/set_engine_setting.h"
#include "model/fx/chain_params.h"
#include "model/instrument/instrument.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "processing/sound/sound_instrument.h"
#include "scenarios/drivers/driver.h"
#include "serialization/value.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using deluge::scenarios::driver::Io;
using deluge::serialization::Value;

/// How many times each Edit is timed.
constexpr size_t kRepeats = 200;

/// One Edit to time: a path and the two values it alternates between.
struct Timed {
	const char* path;
	Value a;
	Value b;
};

/// A song for the synth to live in.
Song* freshSong() {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	deluge::model::initGlobalParams(&song->paramManager);
	return song;
}

/// The median and 95th percentile of @p micros, in µs.
std::pair<double, double> summary(std::vector<double> micros) {
	std::ranges::sort(micros);
	return {micros[micros.size() / 2], micros[(micros.size() * 95) / 100]};
}

int edit_cost_main(int /*argc*/, char** /*argv*/, Io io) {
	Song* song = freshSong();
	currentSong = song;
	ParamManagerForTimeline paramManager;
	Instrument* synth = StorageManager::createNewInstrument(OutputType::SYNTH, &paramManager);
	if (synth == nullptr) {
		std::fprintf(io.err, "edit_cost: no new synth\n");
		return 0;
	}
	song->live.param_backup.backUp(*synth, nullptr, &paramManager, true);
	song->addOutput(synth);
	const std::optional<deluge::project::SlotAddress> address =
	    deluge::bridge::slotAddressOf(*static_cast<SoundInstrument*>(synth));
	if (!address) {
		std::fprintf(io.err, "edit_cost: the synth has no slot\n");
		return 0;
	}
	deluge::edits::Context ctx{*song, song->project};
	const std::array<Timed, 4> edits{{
	    {"unison/detune", Value::makeNumber(10), Value::makeNumber(20)},
	    {"osc1/transpose", Value::makeNumber(3), Value::makeNumber(4)},
	    {"osc1/type", Value::makeScalar("saw"), Value::makeScalar("square")},
	    {"mode", Value::makeScalar("fm"), Value::makeScalar("subtractive")},
	}};
	for (const Timed& timed : edits) {
		std::vector<double> micros;
		micros.reserve(kRepeats);
		for (size_t i = 0; i < kRepeats; i++) {
			deluge::edits::SetEngineSetting edit{*address, timed.path, i % 2 == 0 ? timed.a : timed.b};
			const auto start = std::chrono::steady_clock::now();
			(void)deluge::edits::apply(ctx, edit);
			const auto stop = std::chrono::steady_clock::now();
			micros.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
		}
		const auto [median, p95] = summary(std::move(micros));
		std::fprintf(io.out, "edit %s median_us %.1f p95_us %.1f\n", timed.path, median, p95);
	}
	return 0;
}

} // namespace

DELUGE_DRIVER_SCENARIO(edit_cost, edit_cost_main)
