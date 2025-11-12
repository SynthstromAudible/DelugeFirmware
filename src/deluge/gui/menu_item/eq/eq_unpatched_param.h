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
#include "gui/menu_item/unpatched_param.h"

namespace deluge::gui::menu_item::eq {

class EqUnpatchedParam : public UnpatchedParam {
public:
	EqUnpatchedParam(l10n::String name, l10n::String columnLabel, int32_t newP)
	    : UnpatchedParam(name, newP), column_label_{columnLabel} {}
	EqUnpatchedParam(l10n::String name, int32_t newP) : UnpatchedParam(name, newP), column_label_{name} {}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		UnpatchedParam::configureRenderingOptions(options);
		options.label = l10n::getView(column_label_);
	}

private:
	l10n::String column_label_;
};
} // namespace deluge::gui::menu_item::eq
