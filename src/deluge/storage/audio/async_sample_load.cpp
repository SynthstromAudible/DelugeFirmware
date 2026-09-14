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

#include "storage/audio/async_sample_load.h"

#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "model/drum/drum.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/audio/audio_file_holder.h"
#include "util/functions.h"

#include <algorithm>
#include <utility>

namespace deluge::storage {

namespace {

/// A std::visit visitor made of lambdas.
template <typename... Fs>
struct Overloaded : Fs... {
	using Fs::operator()...;
};

} // namespace

AsyncSampleLoad::AsyncSampleLoad(Root root, AudioLoadContext* context)
    : root_{std::move(root)}, callerContext_{context}, context_{context} {
}

AsyncSampleLoad AsyncSampleLoad::forSong(Song& song, AudioLoadContext* context) {
	return AsyncSampleLoad{SongRoot{&song}, context};
}

AsyncSampleLoad AsyncSampleLoad::forCrucialSong(Song& song, AudioLoadContext* context) {
	return AsyncSampleLoad{CrucialSongRoot{&song}, context};
}

AsyncSampleLoad AsyncSampleLoad::forOutput(Output& output, AudioLoadContext* context) {
	return AsyncSampleLoad{&output, context};
}

AsyncSampleLoad AsyncSampleLoad::forDrum(Drum& drum, AudioLoadContext* context) {
	return AsyncSampleLoad{&drum, context};
}

AsyncSampleLoad AsyncSampleLoad::forAudioClip(AudioClip& clip, AudioLoadContext* context) {
	return AsyncSampleLoad{&clip, context};
}

AsyncSampleLoad AsyncSampleLoad::forHolder(const SampleDemand& demand, AudioLoadContext* context) {
	return AsyncSampleLoad{demand, context};
}

AsyncSampleLoad::Progress AsyncSampleLoad::tick() {
	if (progress_.state != State::Loading) {
		return progress_;
	}

	progress_.ticks++;
	progress_.bound = 0;
	progress_.emptyHolders = 0;
	progress_.inFlight = 0;
	samplesInFlight_ = 0;
	aborted_ = false;
	context_ = callerContext_;
	scopeDepth_ = 0;
	ownFolderDepth_ = -1;

	SampleLoadVisitor& visitor = *this;
	std::visit(Overloaded{
	               [&visitor](const SongRoot& root) { root.song->walkSampleDemands(visitor); },
	               [&visitor](const CrucialSongRoot& root) { root.song->walkCrucialSampleDemands(visitor); },
	               [&visitor](Output* output) { (void)output->walkSampleDemands(visitor); },
	               [&visitor](Drum* drum) { (void)drum->walkSampleDemands(visitor); },
	               [&visitor](AudioClip* clip) { (void)clip->walkSampleDemands(visitor); },
	               [&visitor](const SampleDemand& demand) { visitor.demand(demand); },
	           },
	           root_);

	samplesSettled_ = (samplesInFlight_ == 0);
	if (aborted_) {
		progress_.state = State::Failed;
		progress_.reason = Reason::Aborted;
	}
	else if (progress_.inFlight == 0) {
		progress_.state = State::Loaded;
	}
	return progress_;
}

Walk AsyncSampleLoad::stopIfAborted() {
	// Every lookup this load makes may read the card, so the abort is always asked, as the blocking driver asks it
	// when reading.
	if (aborted_ || shouldAbortLoading()) {
		aborted_ = true;
		return Walk::Stop;
	}
	return Walk::Continue;
}

Walk AsyncSampleLoad::rangeCheckpoint(int32_t /*rangeIndex*/) {
	// No audio routine: nothing this load does holds the calling tier long enough to need one.
	return stopIfAborted();
}

Walk AsyncSampleLoad::drumCheckpoint() {
	return stopIfAborted();
}

void AsyncSampleLoad::clipCheckpoint() {
}

void AsyncSampleLoad::beginInstrumentScope(std::string_view dirPath, std::string_view name, ThingType thing,
                                           ScopeRule /*rule*/) {
	scopeDepth_++;
	// This load always reads the card, so a scope opens under either rule whenever no context is in effect.
	if (ownFolderDepth_ < 0 && AudioLoadContext::isNone(context_)) {
		if (!ownFolderMade_ || ownFolder_.alternateDir != AudioLoadContext::alternateDirFor(dirPath, name)
		    || ownFolder_.thing != thing) {
			ownFolder_ = AudioLoadContext::forInstrument(dirPath, name, thing);
			ownFolderMade_ = true;
		}
		contextOutsideOwnFolder_ = context_;
		context_ = &ownFolder_;
		ownFolderDepth_ = scopeDepth_;
	}
}

void AsyncSampleLoad::endInstrumentScope() {
	if (ownFolderDepth_ == scopeDepth_) {
		context_ = contextOutsideOwnFolder_;
		ownFolderDepth_ = -1;
	}
	scopeDepth_--;
}

void AsyncSampleLoad::demand(const SampleDemand& demand) {
	if (aborted_) {
		// A song walks its audio clips with no checkpoint between them: nothing more is looked up after an abort.
		return;
	}

	AudioFileHolder& holder = *demand.holder;
	if (holder.audioFile != nullptr) {
		progress_.bound++;
		return;
	}
	if (holder.filePath.empty()) {
		return;
	}
	if (std::ranges::find(settledEmpty_, &holder) != settledEmpty_.end()) {
		progress_.emptyHolders++;
		return;
	}

	const bool waveTable = (holder.audioFileType == AudioFileType::WAVETABLE);
	if (waveTable && !samplesSettled_) {
		progress_.inFlight++;
		return;
	}

	Error error = Error::NONE;
	// Bound only once it is fully set up, so the audio routine never sounds a file that is still being built.
	AudioFile* file = holder.acquire(/*mayActuallyReadFile=*/true, &error, demand.makeWaveTableWorkAtAllCosts,
	                                 waveTable ? AsyncLoadMode::Blocking : AsyncLoadMode::AsyncOnly, context_);
	if (waveTable) {
		progress_.wavetableReads++;
	}
	if (error == Error::LOAD_IN_PROGRESS) {
		progress_.inFlight++;
		samplesInFlight_++;
		return;
	}

	if (file != nullptr) {
		holder.setAudioFile(file, demand.reversed, /*manuallySelected=*/false, demand.clusterLoadInstruction);
		progress_.bound++;
	}
	else {
		settledEmpty_.push_back(&holder);
		progress_.emptyHolders++;
	}
	if (demand.clipToName != nullptr) {
		demand.clipToName->name = holder.filePath;
		if (error != Error::NONE) {
			display->displayError(error);
		}
	}
}

} // namespace deluge::storage
