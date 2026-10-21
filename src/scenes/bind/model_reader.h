#pragma once

/// @file
/// @brief A setting group's records, read by an owner's address.

#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "engine-api/types/setting.h"
#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief Reads one owner's settings of a model group (the song's, a track's, a clip's, a lane's, a note row's): each
/// row's stored value as project::read_setting gives it, whether or not the row applies. A bend range the clip or kit
/// row does not hold reads as the one its expression set plays; a kit row that cannot hold note tails reads as out of
/// its kit's arp; a choice whose word the row does not name reads as the row's default.
class ModelReader {
public:
	/// @param model   The song.
	/// @param address The group and owner.
	/// @param facts   The host's runtime facts, which give the bend ranges played and a kit row's note tails.
	ModelReader(const project::Song& model, project::SettingAddress address, const RuntimeFacts& facts);

	/// @brief Whether the song holds the owner.
	[[nodiscard]] bool resolved() const { return resolved_; }
	/// @brief The stored value of the row at @p path.
	/// @param path A path of the group's table.
	/// @return The value, or nullopt for a path the table does not hold or an owner that does not resolve.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;
	/// @brief This reader as the engine's SettingReader; valid while this object lives.
	[[nodiscard]] engine::SettingReader reader() const;

private:
	const project::Song* model_;
	project::SettingAddress address_;
	const RuntimeFacts* facts_;
	bool resolved_{false};
};

} // namespace deluge::scenes::bind
