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

#include "io/debug/log.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param_node.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include <array>

void AutoParam::writeToFile(Serializer& writer, bool writeAutomation, const int32_t* valueForOverride) {
	std::array<char, 9> buffer_storage{};
	char* buffer = buffer_storage.data();

	writer.write("0x");

	int32_t value_now = ((valueForOverride != nullptr) && isAutomated()) ? *valueForOverride : currentValue;

	intToHex(value_now, buffer);
	writer.write(buffer);

	if (writeAutomation) {

		for (int32_t i = 0; i < nodes.getNumElements(); i++) {
			ParamNode* this_node = nodes.getElement(i);
			intToHex(this_node->value, buffer);
			writer.write(buffer);

			uint32_t pos = this_node->pos;
			if (this_node->interpolated) {
				pos |= ((uint32_t)1 << 31);
			}
			intToHex(pos, buffer);
			writer.write(buffer);
		}
	}
}

// Returns error code.
// If you're gonna call this, you probably need to tell the ParamSet that this Param has automation now, if it does.
// Or, to make things easier, you should just call the ParamSet instead, if possible.
Error AutoParam::readFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {

	// Must first delete any automation because sometimes, due to that annoying support I have to do for late-2016
	// files, we'll be overwriting a cloned ParamManager, which might have had automation.
	deleteAutomationBasicForSetup();

	if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
		return Error::NONE;
	}

	char const* first_chars = reader.readNextCharsOfTagOrAttributeValue(2);
	if (first_chars == nullptr) {
		return Error::NONE;
	}

	// If a decimal, then read the rest of the digits
	if (first_chars[0] != '0' || first_chars[1] != 'x') {
		std::array<char, 12> buffer_storage{};
		char* buffer = buffer_storage.data();
		buffer[0] = first_chars[0];
		buffer[1] = first_chars[1];

		for (int32_t i = 2; i < 12 && ((buffer[i] = reader.readNextCharOfTagOrAttributeValue()) != 0); i++) {}
		buffer[11] = 0;
		currentValue = stringToInt(buffer);
		return Error::NONE;
	}

	// Or, normal case - hex and automation...

	// First, read the current value
	char const* hex_chars = reader.readNextCharsOfTagOrAttributeValue(8);
	if (hex_chars == nullptr) {
		return Error::NONE;
	}
	currentValue = static_cast<int32_t>(hexToIntFixedLength(hex_chars, 8));

	return readAutomationFromFile(reader, readAutomationUpToPos);
}

Error AutoParam::readAutomationFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {
	const char* hex_chars = nullptr;

	// And now read in the automation
	int32_t num_elements_to_allocate_for = 0;

	if (readAutomationUpToPos == 0) {
		return Error::NONE;
	}
	{

		int32_t prev_pos = -1;

		while (true) {

			// Every time we've reached the end of a cluster...
			if (num_elements_to_allocate_for <= 0) {

				// See how many more chars before the end of the cluster. If there are any...
				uint32_t chars_remaining = reader.getNumCharsRemainingInValueBeforeEndOfCluster();
				if (chars_remaining != 0u) {

					// Allocate space for the right number of notes, and remember how long it'll be before we need to do
					// this check again
					num_elements_to_allocate_for = static_cast<int32_t>(((chars_remaining - 1) / 16) + 1);
					nodes.ensureEnoughSpaceAllocated(
					    num_elements_to_allocate_for); // If it returns false... oh well. We'll fail later
				}
			}

			hex_chars = reader.readNextCharsOfTagOrAttributeValue(16);
			if (hex_chars == nullptr) {
				return Error::NONE;
			}
			auto value = static_cast<int32_t>(hexToIntFixedLength(hex_chars, 8));
			auto pos = static_cast<int32_t>(hexToIntFixedLength(&hex_chars[8], 8));

			bool interpolated = (pos & ((uint32_t)1 << 31)) != 0u;
			if (interpolated) {
				pos &= ~((uint32_t)1 << 31);
			}

			// Ensure there isn't some problem where nodes are out of order...
			if (pos <= prev_pos) {
				// Firmware logging uses a printf-compatible interface.
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
				D_PRINTLN("Automation nodes out of order");
				continue;
			}

			// If we've reached the end of our allowed timeline length for automation...
			if (pos >= readAutomationUpToPos) {

				// If there's a node actually right on the end-point - well, firmware <= 3.1.5 sometimes put one there
				// when it should have been at pos 0. So, reinterpret that data to make it right.
				if (pos == readAutomationUpToPos) {
					return readLegacyAutomationEndpoint(value, interpolated);
				}
				break;
			}

			prev_pos = pos;

			int32_t node_i = nodes.insertAtKey(pos, true);
			if (node_i == -1) {
				return Error::INSUFFICIENT_RAM;
			}
			ParamNode* node = nodes.getElement(node_i);
			node->value = value;
			node->interpolated = interpolated;

			num_elements_to_allocate_for--;
		}
	}

	return Error::NONE;
}

Error AutoParam::readLegacyAutomationEndpoint(int32_t value, bool interpolated) {
	auto* first_node = nodes.getElement(0);
	if (first_node != nullptr && first_node->pos == 0) {
		return Error::NONE;
	}
	Error error = nodes.insertAtIndex(0);
	if (error != Error::NONE) {
		return error;
	}
	first_node = nodes.getElement(0);
	first_node->pos = 0;
	first_node->value = value;
	first_node->interpolated = interpolated;
	return Error::NONE;
}
