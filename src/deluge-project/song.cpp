/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/song.h"

namespace deluge::project {

Song::Song() = default;

namespace {
/// Resolve a MIDI-CC lane within a tree-resident manager by IDENTITY (S4c). The address's id is the CC
/// number (the MIDIParamVector keys by cc); scan the manager's per-clip CC store for it. Identity, not index: a
/// newly-learned CC reorders the engine vector, so only cc is stable.
DocAutoParam* midiParamInManager(DocParamManager* mgr, uint32_t cc) {
	if (mgr == nullptr) {
		return nullptr;
	}
	for (DocMidiParam& entry : mgr->midi_params) {
		if (entry.cc == cc) {
			return &entry.param;
		}
	}
	return nullptr;
}

/// Resolve @p address's lane within a tree-resident manager: engine params (cable depths included) and expression by
/// id, a MIDI CC by its number. nullptr when the manager or lane is not tree-resident.
DocAutoParam* laneInManager(DocParamManager* mgr, const ParamAddress& address) {
	if (mgr == nullptr) {
		return nullptr;
	}
	switch (address.space) {
	case ParamSpace::ENGINE:
		return mgr->findEngineParam(address.id);
	case ParamSpace::EXPRESSION:
		return mgr->expression ? mgr->expression->find(address.id) : nullptr;
	case ParamSpace::MIDI_CC:
		return midiParamInManager(mgr, address.id.value);
	}
	return nullptr;
}
} // namespace

DocAutoParam* Song::resolveParam(const ParamAddress& address) {
	switch (address.owner.kind) {
	case OwnerId::Kind::SONG:
		return laneInManager(&master_params, address);
	case OwnerId::Kind::CLIP:
		return laneInManager(clip_params.find(address.owner.id), address);
	case OwnerId::Kind::INSTRUMENT:
	case OwnerId::Kind::DRUM:
		return laneInManager(backed_params.find(address.owner, address.clip_id), address);
	}
	return nullptr;
}

InstrumentSlot* Song::resolveSlot(SlotAddress address) {
	std::optional<InstrumentSlot>* slot = nullptr;
	if (address.kind == SlotAddress::Kind::OUTPUT) {
		if (DocOutput* output = output_structure.find(address.id)) {
			slot = &output->instrument;
		}
	}
	else if (DocDrum* drum = drum_structure.find(address.id)) {
		slot = &drum->instrument;
	}
	return slot != nullptr && slot->has_value() ? &**slot : nullptr;
}

std::vector<ControllerMapping>* Song::resolveControls(ControlsAddress address) {
	switch (address.kind) {
	case ControlsAddress::Kind::OUTPUT: {
		DocOutput* output = output_structure.find(address.id);
		return output != nullptr ? &output->controls : nullptr;
	}
	case ControlsAddress::Kind::DRUM: {
		DocDrum* drum = drum_structure.find(address.id);
		return drum != nullptr ? &drum->controls : nullptr;
	}
	case ControlsAddress::Kind::SONG:
		break;
	}
	return &controls;
}

FxChain* Song::resolveChain(ChainAddress address) {
	if (address.kind == ChainAddress::Kind::SONG) {
		return &master_effects;
	}
	DocOutput* output = output_structure.find(address.id);
	return output != nullptr && output->effects ? &*output->effects : nullptr;
}

// M3 owner-1 (clip structure): a clip's structural record is tree-resident by its durable id (clip_structure),
// always set at construction (durable-id Phase 0), so there is no transient session-keyed store.
DocClip* Song::resolveClip(const ClipAddress& address) {
	return clip_structure.find(address.clip_id);
}

DocClip& Song::getOrCreateDocClip(const ClipAddress& address) {
	return clip_structure.getOrCreate(address.clip_id);
}

void Song::removeDocClip(const ClipAddress& address) {
	clip_structure.remove(address.clip_id);
}

// M3 owner-4 (audio-clip descriptors): the AudioClip's descriptor record is tree-resident by its durable id, in a
// store SEPARATE from clip_structure (DocClip is shared with InstrumentClip; DocAudioClip is AudioClip-only).
DocAudioClip& Song::getOrCreateDocAudioClip(const ClipAddress& address) {
	return audio_clip_structure.getOrCreate(address.clip_id);
}

void Song::removeDocAudioClip(const ClipAddress& address) {
	audio_clip_structure.remove(address.clip_id);
}

} // namespace deluge::project
