#include "scenes/bind/model_reader.h"

#include <variant>

namespace deluge::scenes::bind {

namespace {

/// Which of the device's bend ranges @p path of @p group plays when its owner holds none, or nullopt for another row.
std::optional<int32_t> device_bend_of(project::SettingGroupId group, std::string_view path) {
	if (group == project::SettingGroupId::CLIP_VALUES && path == "bendRange") {
		return 0;
	}
	if ((group == project::SettingGroupId::CLIP_VALUES && path == "bendRangeMPE")
	    || (group == project::SettingGroupId::NOTE_ROW && path == "bendRange")) {
		return 1; // a kit row's bend is per finger
	}
	return std::nullopt;
}

} // namespace

ModelReader::ModelReader(const project::Song& model, project::SettingAddress address, const RuntimeFacts& facts)
    : model_{&model}, address_{address}, facts_{&facts} {
	const auto table = project::table_of(address.group);
	resolved_ = !table.empty() && project::read_setting(model, address, table.front().path).has_value();
}

std::optional<int32_t> ModelReader::read(std::string_view path) const {
	const engine::SettingInfo* row = engine::find_setting(project::table_of(address_.group), path);
	if (row == nullptr || !resolved_) {
		return std::nullopt;
	}
	// A kit row whose sound cannot hold note tails is not in its kit's arp, whatever its record says.
	if (address_.group == project::SettingGroupId::LANE_ARP && path == "arpeggiator/kitArp") {
		const auto* lane = std::get_if<project::LaneAddress>(&address_.owner);
		const project::DocDrum* drum = lane != nullptr ? model_->drum_structure.find(lane->drum_id) : nullptr;
		if (drum != nullptr && drum->drum_type == project::DrumType::SOUND
		    && !facts_->allows_note_tails({project::SlotAddress::Kind::DRUM, lane->drum_id})) {
			return 0;
		}
	}
	if (const std::optional<int32_t> which = device_bend_of(address_.group, path);
	    which && !project::holds_setting(*model_, address_, path)) {
		return facts_->device_bend_range(*which);
	}
	const std::optional<serialization::Value> value = project::read_setting(*model_, address_, path);
	return value ? engine::stored_value(*row, value->str()) : std::nullopt;
}

engine::SettingReader ModelReader::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const ModelReader*>(self)->read(path); },
	        .self = this};
}

} // namespace deluge::scenes::bind
