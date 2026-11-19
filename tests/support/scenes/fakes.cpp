#include "fakes.h"

namespace deluge::scenes::bind::test {

uint8_t FakeSoundFacts::loaded_sources(project::SlotAddress slot) const {
	asked.push_back(slot);
	const auto found = loaded.find(slot.id);
	return found != loaded.end() ? found->second : 0;
}

const instrument::SoundState* FakeSoundFacts::sound_state(project::SlotAddress slot) const {
	const auto found = states.find(slot.id);
	return found != states.end() ? &found->second : nullptr;
}

bool FakeSoundFacts::kit_takes_whole_folder(project::SlotAddress slot) const {
	const auto found = whole_folder.find(slot.id);
	return found != whole_folder.end() && found->second;
}

std::optional<uint16_t> FakeSoundFacts::default_range(project::SlotAddress slot, uint8_t source) const {
	const auto found = default_ranges.find({slot.id, source});
	return found != default_ranges.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint64_t> FakeSoundFacts::range_identity(project::SlotAddress slot, uint8_t source,
                                                       uint16_t range) const {
	const auto found = identities.find({slot.id, source, range});
	return found != identities.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint16_t> FakeSoundFacts::take_range(const TakeTarget& /*target*/) const {
	return take;
}

CableList FakeSoundFacts::cables(const project::ParamAddress& /*owner*/) const {
	CableList list;
	for (const CableFact& cable : cable_list) {
		if (list.count < list.items.size()) {
			list.items[list.count++] = cable;
		}
	}
	return list;
}

} // namespace deluge::scenes::bind::test
