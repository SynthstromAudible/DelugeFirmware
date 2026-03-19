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

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace deluge::model::clip::sequencer {

class SequencerMode;

/**
 * RAII factory and registry for sequencer modes.
 * Manages creation, registration, and lifecycle of sequencer mode instances.
 */
class SequencerModeManager {
public:
	static SequencerModeManager& instance();

	// Registration system for sequencer modes
	template <typename T>
	void registerMode(const std::string& name) {
		static_assert(std::is_base_of_v<SequencerMode, T>, "T must derive from SequencerMode");
		factories_[name] = []() { return std::make_unique<T>(); };
		modeNames_.push_back(name);
	}

	// Factory methods
	std::unique_ptr<SequencerMode> createMode(const std::string& name);
	bool isValidMode(const std::string& name) const;
	const std::vector<std::string>& getAvailableModes() const;

private:
	SequencerModeManager() = default;
	~SequencerModeManager() = default;

	// Non-copyable, non-movable singleton
	SequencerModeManager(const SequencerModeManager&) = delete;
	SequencerModeManager& operator=(const SequencerModeManager&) = delete;
	SequencerModeManager(SequencerModeManager&&) = delete;
	SequencerModeManager& operator=(SequencerModeManager&&) = delete;

	std::map<std::string, std::function<std::unique_ptr<SequencerMode>()>> factories_;
	std::vector<std::string> modeNames_;
};

// Convenient registration macro for sequencer modes
#define REGISTER_SEQUENCER_MODE(ClassName, ModeName)                                                                   \
	namespace {                                                                                                        \
	static auto registered_##ClassName##_mode = []() {                                                                 \
		deluge::model::clip::sequencer::SequencerModeManager::instance().registerMode<ClassName>(ModeName);            \
		return true;                                                                                                   \
	}();                                                                                                               \
	}

} // namespace deluge::model::clip::sequencer
