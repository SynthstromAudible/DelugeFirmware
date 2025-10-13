/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/clip/sequencer/sequencer_mode.h"

namespace deluge::model::clip::sequencer {

SequencerModeManager& SequencerModeManager::instance() {
	static SequencerModeManager instance;
	return instance;
}

std::unique_ptr<SequencerMode> SequencerModeManager::createMode(const std::string& name) {
	auto it = factories_.find(name);
	if (it != factories_.end()) {
		return it->second();
	}
	return nullptr;
}

bool SequencerModeManager::isValidMode(const std::string& name) const {
	return factories_.find(name) != factories_.end();
}

const std::vector<std::string>& SequencerModeManager::getAvailableModes() const {
	return modeNames_;
}

} // namespace deluge::model::clip::sequencer
