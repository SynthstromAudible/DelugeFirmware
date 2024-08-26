/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "model/favourite/favourite_manager.h"
#include "storage/storage_manager.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class FavouritesManager {
public:
	struct Favorite {
		int position;
		std::optional<uint8_t> colour;
		std::string filename;
	};

public:
	FavouritesManager();
	~FavouritesManager();

	void setCategory(const std::string& category);
	void selectFavouritesBank(uint8_t bankNumber);
	void setFavorite(uint8_t position, uint8_t colour, const std::string& filename);
	void unsetFavorite(uint8_t position);
	bool isEmpty(uint8_t position) const;
	Error loadFavouritesFromFile(Deserializer& reader);
	void close();
	std::array<std::optional<uint8_t>, 16> getFavouriteColours() const;
	void changeColour(uint8_t position, int32_t offset);
	const std::string& getFavoriteFilename(uint8_t position);
	static constexpr uint8_t favouriteDefaultColor = 4;

	uint8_t currentBankNumber;
	std::optional<uint8_t> currentFavouriteNumber;

private:
	void loadFavouritesBank();
	void saveFavouriteBank() const;

	std::string getFilenameForSave() const;
	mutable bool unsavedChanges = false;

	std::string currentCategory;
	std::vector<Favorite> favourites;
};

extern FavouritesManager favouritesManager;
