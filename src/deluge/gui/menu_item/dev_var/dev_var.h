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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "storage/storage_manager.h"

namespace deluge::gui::menu_item::dev_var {

class AMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarA; }
	void writeCurrentValue() override { storageManager.devVarA = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 512; }
};

class BMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarB; }
	void writeCurrentValue() override { storageManager.devVarB = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 512; }
};

class CMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarC; }
	void writeCurrentValue() override { storageManager.devVarC = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 1024; }
};

class DMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarD; }
	void writeCurrentValue() override { storageManager.devVarD = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 1024; }
};

class EMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarE; }
	void writeCurrentValue() override { storageManager.devVarE = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 1024; }
};

class FMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarF; }
	void writeCurrentValue() override { storageManager.devVarF = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 1024; }
};

class GMenu final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->value_ = storageManager.devVarG; }
	void writeCurrentValue() override { storageManager.devVarG = this->value_; }
	[[nodiscard]] int getMaxValue() const override { return 1024; }
	[[nodiscard]] int getMinValue() const override { return -1024; }
};
} // namespace deluge::gui::menu_item::dev_var
