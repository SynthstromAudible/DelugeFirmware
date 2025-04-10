/*
 * Copyright (c) 2024 Sean Ditny
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
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::record {

class LoopCommand final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		OverDubType type = FlashStorage::defaultLoopRecordingCommand == GlobalMIDICommand::LOOP
		                       ? OverDubType::Normal
		                       : OverDubType::ContinuousLayering;
		this->setValue(util::to_underlying(type));
	}
	void writeCurrentValue() override {
		FlashStorage::defaultLoopRecordingCommand = this->getValue<OverDubType>() == OverDubType::Normal
		                                                ? GlobalMIDICommand::LOOP
		                                                : GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING;
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {
		    l10n::getView(l10n::String::STRING_FOR_LOOP),
		    l10n::getView(l10n::String::STRING_FOR_LAYERING_LOOP),
		};
	}
};
} // namespace deluge::gui::menu_item::record
