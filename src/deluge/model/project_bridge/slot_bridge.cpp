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

#include "model/project_bridge/slot_bridge.h"

#include "deluge-project/structure_codec.h"
#include "engine-api/types/engine_id.h"
#include "model/drum/drum.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/value_serializer.h"

#include <optional>
#include <string>

namespace deluge::bridge {

void fillSlot(::Sound& sound, deluge::project::InstrumentSlot& slot, bool isDrum) {
	if (std::optional<deluge::serialization::Value> element = sound.takeLoadedElement()) {
		slot = isDrum ? deluge::project::slot_of_drum(*element) : deluge::project::slot_of_output(*element);
	}
	else {
		slot = deluge::project::InstrumentSlot{.engine = std::string(deluge::engine::kDelugeInstrument.name)};
	}
}

namespace {

/// The element an output's write produced: the one item of the list it was written into, unboxed.
const deluge::serialization::Value* writtenElement(const deluge::serialization::Value& items) {
	if (!items.isArray() || items.items().size() != 1) {
		return nullptr;
	}
	const deluge::serialization::Value& boxed = items.items().front();
	if (!boxed.isMap() || boxed.entries().size() != 1) {
		return nullptr;
	}
	return &boxed.entries().front().second;
}

} // namespace

void refreshSlots(::Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->docOutput_ == nullptr || (output->type != OutputType::SYNTH && output->type != OutputType::KIT)) {
			continue;
		}
		ValueSerializer writer(ValueSerializer::Writes::ITEMS);
		{
			ActiveWriterScope scope(writer);
			output->writeToFile(nullptr, &song);
		}
		std::optional<deluge::serialization::Value> written = writer.finish();
		const deluge::serialization::Value* element = written ? writtenElement(*written) : nullptr;
		if (element == nullptr) {
			continue;
		}
		if (output->type == OutputType::SYNTH) {
			output->docOutput_->instrument = deluge::project::slot_of_output(*element);
			continue;
		}
		const deluge::serialization::Value* sources = element->find("soundSources");
		if (sources == nullptr) {
			continue;
		}
		for (const deluge::serialization::NamedChild& drum : deluge::serialization::children(*sources)) {
			const std::optional<int32_t> id = deluge::serialization::int_of(drum.node->find("id"));
			deluge::project::DocDrum* record =
			    id ? song.project.drum_structure.find(static_cast<uint32_t>(*id)) : nullptr;
			if (record != nullptr && record->drum_type == DrumType::SOUND) {
				record->instrument = deluge::project::slot_of_drum(*drum.node);
			}
		}
	}
}

} // namespace deluge::bridge
