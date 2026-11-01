#include "scenes/bind/slot_reader.h"

#include "instruments/deluge/settings.h"

namespace deluge::scenes::bind {

SlotReader::SlotReader(const project::Song& model, project::SlotAddress slot, const RuntimeFacts& facts) {
	if (model.resolveSlot(slot) != nullptr) {
		state_ = facts.sound_state(slot);
	}
}

std::optional<int32_t> SlotReader::read(std::string_view path) const {
	if (state_ == nullptr || plugin::find_setting(instrument::kSettings, path) == nullptr) {
		return std::nullopt;
	}
	return instrument::held_setting(*state_, path);
}

plugin::SettingReader SlotReader::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const SlotReader*>(self)->read(path); },
	        .self = this};
}

} // namespace deluge::scenes::bind
