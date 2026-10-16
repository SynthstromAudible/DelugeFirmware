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

#include "model/project_bridge/arp_bridge.h"

#include "deluge-project/arp_codec.h"
#include "modulation/arpeggiator.h"
#include "storage/storage_manager.h"

#include <utility>

namespace deluge::bridge {

project::ArpRecord arpRecordOf(const ArpeggiatorSettings& settings, bool hostValues) {
	project::ArpRecord record{
	    .mode = static_cast<uint8_t>(util::to_underlying(settings.mode)),
	    .octave_mode = static_cast<uint8_t>(util::to_underlying(settings.octaveMode)),
	    .note_mode = static_cast<uint8_t>(util::to_underlying(settings.noteMode)),
	    .mpe_velocity = static_cast<uint8_t>(util::to_underlying(settings.mpeVelocity)),
	    .num_octaves = settings.numOctaves,
	    .step_repeat = settings.numStepRepeats,
	    .chord_type = settings.chordTypeIndex,
	    .sync_level = static_cast<uint8_t>(engine::index_of(settings.syncLevel)),
	    .sync_type = static_cast<uint8_t>(std::to_underlying(settings.syncType)),
	    .randomizer_lock = settings.randomizerLock,
	    .kit_arp = settings.includeInKitArp,
	};
	if (hostValues) {
		const auto held = [](uint32_t value) { return std::optional<int32_t>{static_cast<int32_t>(value)}; };
		record.gate = settings.gate;
		record.rate = settings.rate;
		record.rhythm = held(settings.rhythm);
		record.sequence_length = held(settings.sequenceLength);
		record.chord_polyphony = held(settings.chordPolyphony);
		record.ratchet_amount = held(settings.ratchetAmount);
		record.note_probability = held(settings.noteProbability);
		record.bass_probability = held(settings.bassProbability);
		record.swap_probability = held(settings.swapProbability);
		record.glide_probability = held(settings.glideProbability);
		record.reverse_probability = held(settings.reverseProbability);
		record.chord_probability = held(settings.chordProbability);
		record.ratchet_probability = held(settings.ratchetProbability);
		record.spread_velocity = held(settings.spreadVelocity);
		record.spread_gate = held(settings.spreadGate);
		record.spread_octave = held(settings.spreadOctave);
	}
	return record;
}

void seedArpRecord(std::optional<project::ArpRecord>& record, const ArpeggiatorSettings& settings, bool hostValues) {
	if (!record) {
		record = arpRecordOf(settings, hostValues);
		return;
	}
	if (hostValues) {
		const project::ArpRecord engine = arpRecordOf(settings, true);
		for (const engine::SettingInfo& row : engine::kArpHostRows) {
			std::optional<int32_t>& value = *project::host_value_at(*record, row.path);
			value = value.has_value() ? value : *project::host_value_at(engine, row.path);
		}
	}
}

void writeArpAttributes(Serializer& writer, const ArpeggiatorSettings& settings, const project::ArpRecord* record,
                        bool hostValues, std::optional<int32_t> tickMagnitude) {
	project::ArpRecord written = record != nullptr ? *record : arpRecordOf(settings, hostValues);
	if (hostValues) {
		const project::ArpRecord engine = arpRecordOf(settings, true);
		for (const engine::SettingInfo& row : engine::kArpHostRows) {
			std::optional<int32_t>& value = *project::host_value_at(written, row.path);
			value = value.has_value() ? value : *project::host_value_at(engine, row.path);
		}
	}
	writer.writeValue(project::encode_settings(written, tickMagnitude));
	settings.writeGeneratedToFile(writer);
	if (hostValues) {
		writer.writeValue(project::encode_host(written));
	}
}

} // namespace deluge::bridge
