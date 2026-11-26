#pragma once

/// @file
/// @brief What a scene asks of the card's files.

#include <string_view>

namespace deluge::ui::scenes::bind {

/// @brief The card's file actions, which the host performs as the legacy browser does.
class CardActions {
public:
	virtual ~CardActions() = default;
	/// @brief Stop the preview's sound, then drop the sample file @p path from memory unless the song uses it: the
	///        sample browser's Shift+SAVE, before it offers to delete the file.
	/// @return False when the song uses the file, which may then not be deleted.
	[[nodiscard]] virtual bool release_sample_file(std::string_view path) = 0;
};

} // namespace deluge::ui::scenes::bind
