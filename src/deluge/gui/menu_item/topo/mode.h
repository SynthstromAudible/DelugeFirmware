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

#pragma once
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "model/clip/instrument_clip.h"
#include "model/generative/topo/topo_engine.h"

namespace deluge::gui::menu_item::topo {

/// Topographic vs. Euclidean engine mode for the current kit clip.
class Mode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		bool euclidean = clip && clip->topoDrumsSettings.mode == deluge::model::topo::TopoMode::Euclidean;
		setValue(euclidean ? 1 : 0);
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (!clip) {
			return;
		}
		clip->topoDrumsSettings.mode =
		    getValue() == 0 ? deluge::model::topo::TopoMode::Drums : deluge::model::topo::TopoMode::Euclidean;
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_TOPOGRAPHIC), //<
		    l10n::getView(STRING_FOR_EUCLIDEAN),   //<
		};
	}
};

} // namespace deluge::gui::menu_item::topo
