/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/selection.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::sample::browser_preview {
class Mode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(FlashStorage::sampleBrowserPreviewMode); }
	void writeCurrentValue() override { FlashStorage::sampleBrowserPreviewMode = this->getValue(); }
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {
		    l10n::getView(l10n::String::STRING_FOR_DISABLED),
		    l10n::getView(l10n::String::STRING_FOR_CONDITIONAL),
		    l10n::getView(l10n::String::STRING_FOR_ENABLED),
		};
	}
};
} // namespace deluge::gui::menu_item::sample::browser_preview
