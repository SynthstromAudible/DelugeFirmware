#pragma once

/// @file
/// @brief The browser's favourites bank, as a scene reads and edits it: what the host's favourites manager holds.

#include "ui/grid/favourites.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::scenes::services {

/// @brief Whether the bank on show can be read and edited yet.
enum class FavouritesBank : uint8_t {
	Loading,     ///< its read has been asked for and has not answered: nothing may be done with it
	Loaded,      ///< what is in memory is what the card holds
	Unavailable, ///< the read failed: nothing may be edited, and a press says so
};

/// @brief One category's favourites (the sample browser's is "SAMPLES"), sixteen to a bank, sixteen banks.
class Favourites {
public:
	virtual ~Favourites() = default;
	/// @brief Show @p category's bank 0 (`FavouritesManager::setCategory`).
	virtual void set_category(std::string_view category) = 0;
	/// @brief Leave the bank, saving unsaved colour changes (`FavouritesManager::close`).
	virtual void close() = 0;
	/// @brief Whether the bank on show has loaded.
	[[nodiscard]] virtual FavouritesBank bank_state() const = 0;
	/// @brief Each slot's colour (0-15), or nothing for an empty slot.
	[[nodiscard]] virtual std::array<std::optional<uint8_t>, ui::grid::kFavourites> colours() const = 0;
	/// @brief The slot last recalled, set or unset, which flashes and which SHIFT + vertical recolours.
	[[nodiscard]] virtual std::optional<uint8_t> current_favourite() const = 0;
	/// @brief The bank on show.
	[[nodiscard]] virtual uint8_t current_bank() const = 0;
	/// @brief Whether slot @p position holds nothing.
	[[nodiscard]] virtual bool is_empty(uint8_t position) const = 0;
	/// @brief Make @p position current and answer its file's path, empty for an empty slot
	///        (`FavouritesManager::getFavoriteFilename`).
	virtual std::string recall(uint8_t position) = 0;
	/// @brief Set @p position to @p path in the default colour.
	virtual void set(uint8_t position, std::string_view path) = 0;
	/// @brief Empty @p position.
	virtual void unset(uint8_t position) = 0;
	/// @brief Show bank @p bank.
	virtual void select_bank(uint8_t bank) = 0;
	/// @brief Move @p position's colour by @p offset, wrapping.
	virtual void change_colour(uint8_t position, int32_t offset) = 0;
};

} // namespace deluge::scenes::services
