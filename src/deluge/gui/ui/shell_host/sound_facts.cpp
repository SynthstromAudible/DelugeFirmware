#include "definitions_cxx.hpp"
#include "gui/ui/browser/dx_cartridge_bank.h"
#include "gui/ui/shell_host/shell_host.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/DX7Cartridge.h"
#include "storage/multi_range/multisample_range.h"

#include <algorithm>

namespace deluge::gui::shell_host {
namespace {

/// Whether @p clip plays the sound @p slot names: a synth clip its own track, a kit clip one of its kit's sound rows. A
/// replaced synth kept hibernating under its id is not one: no clip plays it.
bool clipPlaysSlot(const ::Clip& clip, project::SlotAddress slot) {
	using Kind = project::SlotAddress::Kind;
	if (clip.output == nullptr) {
		return false;
	}
	if (slot.kind == Kind::OUTPUT) {
		return clip.output->type == OutputType::SYNTH && clip.output->id() == slot.id;
	}
	if (clip.output->type != OutputType::KIT) {
		return false;
	}
	for (const Drum* drum = static_cast<const Kit*>(clip.output)->firstDrum; drum != nullptr; drum = drum->next) {
		if (drum->type == DrumType::SOUND && drum->id() == slot.id) {
			return true;
		}
	}
	return false;
}

/// The sound at @p slot in the current song, or null.
::Sound* sound_at(project::SlotAddress slot) {
	return currentSong != nullptr ? deluge::bridge::soundAt(*currentSong, slot) : nullptr;
}

static_assert(kMaxNumPatchCables <= scenes::bind::kMaxCables);

/// The sound facts, answered from the running app: each sound is found by its slot in the current song.
class HostSoundFacts final : public scenes::bind::SoundFacts {
public:
	[[nodiscard]] uint8_t loaded_sources(project::SlotAddress slot) const override {
		::Sound* sound = sound_at(slot);
		uint8_t loaded = 0;
		if (sound != nullptr) {
			for (uint8_t s = 0; s < kNumSources; ++s) {
				if (sound->sourceResources[s].hasAtLeastOneAudioFileLoaded()) {
					loaded |= static_cast<uint8_t>(1u << s);
				}
			}
		}
		return loaded;
	}

	[[nodiscard]] const deluge::instruments::classic::SoundState*
	sound_state(project::SlotAddress slot) const override {
		::Sound* sound = sound_at(slot);
		return sound != nullptr ? &sound->state() : nullptr;
	}

	[[nodiscard]] bool kit_takes_whole_folder(project::SlotAddress slot) const override {
		::Clip* clip = currentSong != nullptr ? currentSong->getCurrentClip() : nullptr;
		if (slot.kind != project::SlotAddress::Kind::DRUM || clip == nullptr || clip->type != ClipType::INSTRUMENT
		    || clip->output == nullptr || clip->output->type != OutputType::KIT) {
			return false;
		}
		const auto& rows = static_cast<::InstrumentClip*>(clip)->noteRows;
		const ::Drum* drum = currentSong->drumForId(slot.id);
		const ::Kit* kit = static_cast<const ::Kit*>(clip->output);
		return drum != nullptr && !rows.empty() && rows[0].drum == drum && kit->firstDrum == drum
		       && drum->next == nullptr;
	}

	[[nodiscard]] std::optional<uint16_t> default_range(project::SlotAddress slot, uint8_t source) const override {
		const ::Sound* sound = sound_at(slot);
		if (sound == nullptr || source >= kNumSources) {
			return std::nullopt;
		}
		const int32_t i = sound->sourceResources[source].defaultRangeI;
		if (i < 0 || i >= std::ssize(sound->sourceResources[source].ranges)) {
			return std::nullopt;
		}
		return static_cast<uint16_t>(i);
	}

	[[nodiscard]] std::optional<uint64_t> range_identity(project::SlotAddress slot, uint8_t source,
	                                                     uint16_t range) const override {
		const ::Sound* sound = sound_at(slot);
		if (sound == nullptr || source >= kNumSources || range >= std::ssize(sound->sourceResources[source].ranges)) {
			return std::nullopt;
		}
		return sound->sourceResources[source].ranges.getElement(range)->identity();
	}

	[[nodiscard]] std::optional<uint16_t> take_range(const scenes::bind::TakeTarget& target) const override {
		// By identity: the clip and what it plays, then the range wherever it now sits.
		// Range identities are run-unique and never saved, so a range in another song cannot match.
		if (currentSong == nullptr || !target.clip || !target.range || target.source >= kNumSources) {
			return std::nullopt;
		}
		::Clip* clip = currentSong->clipForId(target.clip->clip_id);
		if (clip == nullptr || !clipPlaysSlot(*clip, target.slot)) {
			return std::nullopt;
		}
		const ::Sound* sound = deluge::bridge::soundAt(*currentSong, target.slot);
		if (sound == nullptr) {
			return std::nullopt;
		}
		const MultiRangeArray& ranges = sound->sourceResources[target.source].ranges;
		for (int32_t i = 0; i < std::ssize(ranges); i++) {
			if (ranges.getElement(i)->identity() == *target.range) {
				return static_cast<uint16_t>(i);
			}
		}
		return std::nullopt;
	}

	[[nodiscard]] std::optional<scenes::bind::DxCartridgeNames> dx_cartridge() const override {
		DX7Cartridge* bank = deluge::gui::dxCartridgeBank();
		if (bank == nullptr) {
			return std::nullopt;
		}
		scenes::bind::DxCartridgeNames out;
		char names[32][11];
		bank->getProgramNames(names);
		out.count = bank->numPatches();
		for (int32_t i = 0; i < out.count; i++) {
			std::copy_n(names[i], 11, out.names[i].begin());
		}
		return out;
	}

	[[nodiscard]] scenes::bind::CableList cables(const project::ParamAddress& owner) const override {
		scenes::bind::CableList list;
		if (currentSong == nullptr) {
			return list;
		}
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		PatchCableSet* set = deluge::bridge::cableSetAt(*currentSong, owner, memory);
		if (set == nullptr) {
			return list;
		}
		for (int32_t i = 0; i < set->numPatchCables && list.count < list.items.size(); ++i) {
			const PatchCable& cable = set->patchCables[i];
			list.items[list.count++] = {.depth = deluge::bridge::cable_depth(cable.from, cable.destination),
			                            .polarity =
			                                static_cast<deluge::instruments::classic::Polarity>(cable.polarity)};
		}
		return list;
	}
};

} // namespace

const scenes::bind::SoundFacts& sound_facts() {
	static const HostSoundFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
