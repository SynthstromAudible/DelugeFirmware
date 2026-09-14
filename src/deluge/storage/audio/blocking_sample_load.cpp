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

#include "storage/audio/blocking_sample_load.h"

#include "hid/display/display.h"
#include "model/clip/audio_clip.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_holder.h"
#include "util/functions.h"

namespace deluge::storage {

BlockingSampleLoad::BlockingSampleLoad(bool mayReadCard, AudioLoadContext* context)
    : mayReadCard_{mayReadCard}, context_{context} {
}

Walk BlockingSampleLoad::stopIfAborted() const {
	return (mayReadCard_ && shouldAbortLoading()) ? Walk::Stop : Walk::Continue;
}

Walk BlockingSampleLoad::rangeCheckpoint(int32_t rangeIndex) {
	AudioEngine::logAction("Source::loadAllSamples");
	if (!(rangeIndex & 3)) { // 3 works, 7 occasionally drops voices - for multisampled synths
		AudioEngine::routineWithClusterLoading();
	}
	return stopIfAborted();
}

Walk BlockingSampleLoad::drumCheckpoint() {
	return stopIfAborted();
}

void BlockingSampleLoad::clipCheckpoint() {
	// If not reading files, high chance that we'll be searching through memory a lot and not reading the card
	// (which would call the audio routine), so we'd better call the audio routine here.
	if (!mayReadCard_ && !(clipsWithoutReading_++ & 7)) { // 31 bad. 15 seems to pass. 7 to be safe
		AudioEngine::logAction("Song::loadAllSamples");
		AudioEngine::routineWithClusterLoading();
	}
}

void BlockingSampleLoad::beginInstrumentScope(std::string_view dirPath, std::string_view name, ThingType thing,
                                              ScopeRule rule) {
	scopeDepth_++;
	const bool ruleAllows = (rule == ScopeRule::Always) || mayReadCard_;
	if (ownFolderDepth_ < 0 && ruleAllows && AudioLoadContext::isNone(context_)) {
		ownFolder_ = AudioLoadContext::forInstrument(dirPath, name, thing);
		contextOutsideOwnFolder_ = context_;
		context_ = &ownFolder_;
		ownFolderDepth_ = scopeDepth_;
	}
}

void BlockingSampleLoad::endInstrumentScope() {
	if (ownFolderDepth_ == scopeDepth_) {
		context_ = contextOutsideOwnFolder_;
		ownFolder_ = {};
		ownFolderDepth_ = -1;
	}
	scopeDepth_--;
}

void BlockingSampleLoad::demand(const SampleDemand& demand) {
	Error error = Error::NONE;
	// Bound only once it is fully set up, so the audio routine never sounds a file that is still being built.
	if (AudioFile* file = demand.holder->acquire(mayReadCard_, &error, demand.makeWaveTableWorkAtAllCosts,
	                                             AsyncLoadMode::Blocking, context_)) {
		demand.holder->setAudioFile(file, demand.reversed, /*manuallySelected=*/false, demand.clusterLoadInstruction);
	}
	if (demand.clipToName != nullptr) {
		demand.clipToName->name = demand.holder->filePath.c_str();
		if (error != Error::NONE) {
			display->displayError(error);
		}
	}
}

} // namespace deluge::storage
