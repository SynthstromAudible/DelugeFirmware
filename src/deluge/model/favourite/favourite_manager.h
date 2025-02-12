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
#include <string>
#include <vector>

class FavouritesManager {
public:
	struct Favorite {
		int position;
		int colour;
		std::string filename;

		Favorite() {
			position = -1;
			colour = -1;
			filename = "";
		}; // Default constructor

		Favorite(int pos, int col, const std::string& file) {
			position = pos;
			colour = col;
			filename = file;
		}; // Constructor with parameters
	};

public:
	FavouritesManager();
	~FavouritesManager();

	void setCategory(const std::string& category);
	void selectFavouritesBank(uint8_t bankNumber);
	void setFavorite(uint8_t position, int8_t colour, const std::string& filename);
	void unsetFavorite(uint8_t position);
	bool isEmtpy(uint8_t position) const;
	Error loadFavouritesFromFile(Deserializer& reader);
	void close();
	std::array<int8_t, 16> getFavouriteColours() const;
	int8_t changeColour(uint8_t position, int32_t offset);
	const std::string& getFavoriteFilename(uint8_t position) const;
	static constexpr int8_t favouriteDefaultColor = 4;
	static constexpr int8_t favouriteEmtpyColor = -1;

private:
	std::string currentCategory;
	int currentBankNumber;
	std::vector<Favorite> favourites;

	void loadSubcategory(int subcategoryIndex);
	void saveFavouriteBank() const;
	std::string getFilenameForSave() const;
	mutable bool unsavedChanges = false;
};

extern FavouritesManager favouritesManager;
