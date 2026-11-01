#include "scenes/bind/model_reader.h"

#include <variant>

namespace deluge::scenes::bind {

namespace {

/// Which of its expression set's bend ranges @p path of @p group is, or nullopt for another row: a clip's main and
/// MPE ranges, and a kit row's own, which is its set's per-finger range.
std::optional<int32_t> played_bend_of(project::SettingGroupId group, std::string_view path) {
	if (group == project::SettingGroupId::CLIP_VALUES && path == "bendRange") {
		return 0;
	}
	if ((group == project::SettingGroupId::CLIP_VALUES && path == "bendRangeMPE")
	    || (group == project::SettingGroupId::NOTE_ROW && path == "bendRange")) {
		return 1;
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
	const plugin::SettingInfo* row = plugin::find_setting(project::table_of(address_.group), path);
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
	if (const std::optional<int32_t> which = played_bend_of(address_.group, path);
	    which && !project::holds_setting(*model_, address_, path)) {
		return facts_->played_bend_range(address_, *which);
	}
	const std::optional<serialization::Value> value = project::read_setting(*model_, address_, path);
	if (!value) {
		return std::nullopt;
	}
	const std::optional<int32_t> stored = plugin::stored_value(*row, value->str());
	// A choice a file gave a word the row has no name for (a MIDI track's Y axis on a CC other than 1 or 74) shows as
	// the row's default, as legacy's menus do, so it can be chosen again.
	return stored || row->kind != plugin::SettingKind::CHOICE ? stored : std::optional<int32_t>{row->default_value};
}

plugin::SettingReader ModelReader::reader() const {
	return {.read = [](const void* self,
	                   std::string_view path) { return static_cast<const ModelReader*>(self)->read(path); },
	        .self = this};
}

} // namespace deluge::scenes::bind
