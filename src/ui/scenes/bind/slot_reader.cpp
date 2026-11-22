#include "ui/scenes/bind/slot_reader.h"

#include "instruments/classic/settings.h"

namespace deluge::ui::scenes::bind {

SlotReader::SlotReader(const project::Song& model, project::SlotAddress slot, const SoundFacts& sound) {
	if (model.resolveSlot(slot) != nullptr) {
		state_ = sound.sound_state(slot);
	}
}

std::optional<int32_t> SlotReader::read(std::string_view path) const {
	if (state_ == nullptr || plugin::find_setting(instruments::classic::kSettings, path) == nullptr) {
		return std::nullopt;
	}
	return instruments::classic::held_setting(*state_, path);
}

plugin::SettingReader SlotReader::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const SlotReader*>(self)->read(path); },
	        .self = this};
}

} // namespace deluge::ui::scenes::bind
