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

#include "gui/ui/reversion.h"

#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "storage/audio/sample_load_walk.h"

namespace deluge::gui {

Reversion reversion{};

bool Reversion::request(Direction direction) {
	if (phase_ != Phase::Idle) {
		// Undo and redo are steps, not destinations: the one already out is the one the user asked for first, and
		// dropping either would lose an edit.
		refused_++;
		return false;
	}
	direction_ = direction;
	wanted_.clear();
	nextWanted_ = 0;
	phase_ = Phase::Collect;
	routine();
	return true;
}

void Reversion::routine() {
	while (step()) {}
}

bool Reversion::step() {
	switch (phase_) {
	case Phase::Idle:
		return false;
	case Phase::Collect:
		return stepCollect();
	case Phase::Samples:
		return stepSamples();
	case Phase::Revert:
		return stepRevert();
	}
	return false;
}

bool Reversion::stepCollect() {
	// The action this command would revert, and every file its consequences will want. Copied out as values: the
	// list belongs to the model, and the reversion rewrites it as it runs.
	const TimeType time = (direction_ == Direction::Undo) ? BEFORE : AFTER;
	for (const Action* action = actionLogger.firstAction[time]; action != nullptr; action = nullptr) {
		for (Consequence* consequence = action->firstConsequence; consequence != nullptr;
		     consequence = consequence->next) {
			if (const std::string* path = consequence->filePathNeededForRevert(); path != nullptr && !path->empty()) {
				wanted_.push_back(*path);
			}
		}
	}
	phase_ = wanted_.empty() ? Phase::Revert : Phase::Samples;
	return true;
}

bool Reversion::stepSamples() {
	if (load_.pending()) {
		const std::optional<deluge::storage::LatestSampleLoad<int32_t>::Outcome> outcome = load_.tick();
		if (!outcome.has_value()) {
			return false; // still loading
		}
		// A file that will not load is not fatal: revert() shows the error and carries on with an empty holder,
		// which is what it did when the load happened inside it.
		nextWanted_++;
	}
	if (nextWanted_ >= wanted_.size()) {
		phase_ = Phase::Revert;
		return true;
	}
	// One at a time, into a holder of our own: binding the clip's holder here would move the model before the
	// reversion runs, and the reversion has to be the thing that moves it.
	held_.setAudioFile(nullptr);
	held_.filePath = wanted_[nextWanted_];
	held_.audioFileType = AudioFileType::SAMPLE;
	preloads_++;
	load_.request({.holder = &held_}, nullptr, 0);
	return false;
}

bool Reversion::stepRevert() {
	// Card-free: every file the consequences want is in memory, so `AudioFileHolder::loadFile()` inside them binds
	// what is already there.
	if (direction_ == Direction::Undo) {
		actionLogger.undo();
	}
	else {
		actionLogger.redo();
	}
	reversions_++;
	releaseHeld();
	wanted_.clear();
	nextWanted_ = 0;
	phase_ = Phase::Idle;
	return false;
}

void Reversion::releaseHeld() {
	// The reversion has taken what it needed; this reference was only ever keeping the file in memory for it.
	held_.setAudioFile(nullptr);
	held_.filePath.clear();
}

} // namespace deluge::gui
