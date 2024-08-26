/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/patch/patch_cable.h"
#include "definitions_cxx.hpp"

#include <storage/flash_storage.h>

Polarity stringToPolarity(const std::string_view string) {
	if (string == "unipolar") {
		return Polarity::UNIPOLAR;
	}
	if (string == "bipolar") {
		return Polarity::BIPOLAR;
	}
	return Polarity::BIPOLAR; // Default to bipolar
}
std::string_view polarityToString(const Polarity polarity) {
	switch (polarity) {
	case Polarity::UNIPOLAR:
		return "unipolar";
	case Polarity::BIPOLAR:
		return "bipolar";
	default:
		return "bipolar";
	}
}
std::string_view polarityToStringShort(const Polarity polarity) {
	switch (polarity) {
	case Polarity::UNIPOLAR:
		return "UPLR";
	case Polarity::BIPOLAR:
		return "BPLR";
	default:
		return "BPLR";
	}
}

bool PatchCable::hasPolarity(PatchSource source) {
	if (source == PatchSource::Y || source == PatchSource::X) {
		// these can't be converted so they ignore the actual setting
		return false;
	}
	return true;
}

Polarity PatchCable::getDefaultPolarity(PatchSource source) {
	if (source == PatchSource::AFTERTOUCH) {
		// Aftertouch is stored unipolar, using bipolar here causes near zero volume with the default patch to level
		return Polarity::UNIPOLAR;
	}
	if (source == PatchSource::Y || source == PatchSource::X || source == PatchSource::SIDECHAIN) {
		// mod wheel is stored unipolar but MPE Y is bipolar, so stuck using bipolar
		return Polarity::BIPOLAR;
	}
	return FlashStorage::defaultPatchCablePolarity; // Use the default polarity from flash storage
}

void PatchCable::setDefaultPolarity() {
	polarity = getDefaultPolarity(from);
}

void PatchCable::setup(PatchSource newFrom, uint8_t newTo, int32_t newAmount) {
	from = newFrom;
	destinationParamDescriptor.setToHaveParamOnly(newTo);
	initAmount(newAmount);
	setDefaultPolarity();
}

bool PatchCable::isActive() {
	return param.containsSomething(0);
}

void PatchCable::initAmount(int32_t value) {
	param.nodes.empty();
	param.currentValue = value;
}

void PatchCable::makeUnusable() {
	destinationParamDescriptor.setToNull();
}
