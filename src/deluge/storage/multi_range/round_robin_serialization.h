/*
 * Copyright (c) 2024-2026 Synthstrom Audible Limited
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

#include "storage/multi_range/multisample_range.h"
#include <cstring>

/// Reads one end of a slot's velocity band, returning `defaultValue` for anything outside 1-127.
///
/// Read into a full int before validating: assigning straight to uint8_t truncates first, so a
/// hand-edited 300 would wrap to a perfectly legal-looking 44 and no later check could tell. Same
/// trap readChokeGroupFromFile() guards against, same shape of guard.
///
/// Out of range can only come from a hand-edited or corrupted file, so fall back to that end's own
/// default - 1 for min, 127 for max, which is exactly what a slot carrying no velocity attributes
/// at all already gets. Untrusted data then behaves like legacy data, and the slot stays reachable.
/// Clamping instead would fail closed: a mangled max of -1 would clamp to 1, leaving a band of 1-1
/// that nothing can play.
///
/// Deliberately does NOT reconcile min against max. The two attributes arrive separately, in
/// whatever order the file lists them - read min=100 while max still holds its 127 default, then
/// read max=60, and any "keep them ordered" rule would drag one to meet the other and corrupt a
/// file that was written perfectly correctly. A backwards pair already fails safe: it matches no
/// velocity, and resolveVelocitySlotIndex() falls back to slot 1.
///
/// Templated on the deserializer for the same host-testability reason as readRoundRobinAlternates()
/// below - see tests/unit/round_robin_parser_tests.cpp.
template <typename DeserializerT>
uint8_t readVelocityRangeFromFile(DeserializerT& reader, char const* tagName, uint8_t defaultValue) {
	int32_t value = reader.readTagOrAttributeValueInt();
	reader.exitTag(tagName);
	if (value < MultisampleRange::kDefaultVelocityMin || value > MultisampleRange::kDefaultVelocityMax) {
		return defaultValue;
	}
	return (uint8_t)value;
}

/// Reads a "roundRobinAlternates" array into the given range. Deliberately does NOT touch rrMode -
/// that's stored in its own sibling tag which may appear before this one in the file.
///
/// Templated on the deserializer and range types so the parsing logic can be unit-tested on the
/// host with lightweight fakes (see tests/unit/round_robin_parser_tests.cpp). Firmware code
/// instantiates it with Deserializer and MultisampleRange.
template <typename DeserializerT, typename RangeT>
Error readRoundRobinAlternates(DeserializerT& reader, RangeT* range) {
	char const* tagName;

	range->rrCount = 0;
	range->rrIndex = 0;

	reader.match('[');
	while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "alternate")) {
			decltype(range->ensureAlternateHolder(0)) alternateHolder = nullptr;
			if (range->rrCount < kMaxRoundRobinAlternates) {
				alternateHolder = range->ensureAlternateHolder(range->rrCount);
				if (!alternateHolder) {
					return Error::INSUFFICIENT_RAM;
				}
				alternateHolder->startPos = 0;
				alternateHolder->endPos = 0;
				alternateHolder->loopStartPos = 0;
				alternateHolder->loopEndPos = 0;
				alternateHolder->transpose = 0;
				alternateHolder->setCents(0);
				alternateHolder->volume = kVariantVolumeUnity;
			}

			reader.match('{');
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (alternateHolder == nullptr) {
					// Past the slot cap - skip the whole field like any unknown tag.
					reader.exitTag(tagName);
				}
				else if (!strcmp(tagName, "fileName")) {
					reader.readTagOrAttributeValueString(&alternateHolder->filePath);
					reader.exitTag("fileName");
				}
				else if (!strcmp(tagName, "transpose")) {
					alternateHolder->transpose = reader.readTagOrAttributeValueInt();
					reader.exitTag("transpose");
				}
				else if (!strcmp(tagName, "cents")) {
					alternateHolder->setCents(reader.readTagOrAttributeValueInt());
					reader.exitTag("cents");
				}
				else if (!strcmp(tagName, "velocityRangeMin")) {
					uint8_t slotIndex = range->rrCount + 1;
					uint8_t min = readVelocityRangeFromFile(reader, tagName, MultisampleRange::kDefaultVelocityMin);
					range->setVelocityRange(slotIndex, min, range->getVelocityRangeMax(slotIndex));
				}
				else if (!strcmp(tagName, "velocityRangeMax")) {
					uint8_t slotIndex = range->rrCount + 1;
					uint8_t max = readVelocityRangeFromFile(reader, tagName, MultisampleRange::kDefaultVelocityMax);
					range->setVelocityRange(slotIndex, range->getVelocityRangeMin(slotIndex), max);
				}
				else if (!strcmp(tagName, "variantVolume")) {
					alternateHolder->volume = reader.readTagOrAttributeValueInt();
					reader.exitTag("variantVolume");
				}
				else if (!strcmp(tagName, "zone")) {
					reader.match('{');
					while (*(tagName = reader.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "startSamplePos")) {
							alternateHolder->startPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("startSamplePos");
						}
						else if (!strcmp(tagName, "endSamplePos")) {
							alternateHolder->endPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("endSamplePos");
						}
						else if (!strcmp(tagName, "startLoopPos")) {
							alternateHolder->loopStartPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("startLoopPos");
						}
						else if (!strcmp(tagName, "endLoopPos")) {
							alternateHolder->loopEndPos = reader.readTagOrAttributeValueInt();
							reader.exitTag("endLoopPos");
						}
						else {
							reader.exitTag(tagName);
						}
					}
					reader.exitTag("zone", true);
				}
				else {
					reader.exitTag(tagName);
				}
			}
			reader.exitTag("alternate", true);

			if (alternateHolder != nullptr) {
				range->rrCount++;
			}
		}
		else {
			reader.exitTag();
		}
	}

	reader.exitTag();
	reader.match(']');
	return Error::NONE;
}
