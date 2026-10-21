#include "scenes/bind/chain_reader.h"

#include "engines/effects/settings.h"

namespace deluge::scenes::bind {

ChainReader::ChainReader(const project::Song& model, project::ChainAddress chain, const RuntimeFacts& facts) {
	if (model.resolveChain(chain) != nullptr) {
		effects_ = facts.chain_effects(chain);
	}
}

std::optional<int32_t> ChainReader::read(std::string_view path) const {
	return effects_ != nullptr ? effects::held_effect_setting(*effects_, path) : std::nullopt;
}

engine::SettingReader ChainReader::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const ChainReader*>(self)->read(path); },
	        .self = this};
}

} // namespace deluge::scenes::bind
