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
#include "gui/menu_item/toggle.h"
#include "model/clip/instrument_clip.h"
#include "model/sequencing/lanes_engine.h"

namespace deluge::gui::menu_item::lanes {

class IntervalMin final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 0; }
	[[nodiscard]] int32_t getMinValue() const override { return -48; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			this->setValue(clip->lanesEngine->intervalMin);
		}
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			clip->lanesEngine->intervalMin = this->getValue();
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}
};

class IntervalMax final : public Integer {
public:
	using Integer::Integer;

	[[nodiscard]] int32_t getMaxValue() const override { return 48; }
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			this->setValue(clip->lanesEngine->intervalMax);
		}
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			clip->lanesEngine->intervalMax = this->getValue();
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}
};

class IntervalScale final : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			this->setValue(clip->lanesEngine->intervalScaleAware);
		}
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (clip->lanesEngine) {
			clip->lanesEngine->intervalScaleAware = this->getValue();
		}
	}

	MenuPermission checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
	                                             ::MultiRange** currentRange) override {
		return MenuPermission::YES;
	}
};

} // namespace deluge::gui::menu_item::lanes
