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

#include "model/favourite/favourite_manager.h"
#include <cstdint>
#include <string.h>

FavouritesManager favouritesManager{};

FavouritesManager::FavouritesManager() {
	favourites.resize(16);
}

FavouritesManager::~FavouritesManager() {
	saveFavouriteBank();
}
void FavouritesManager::close() {
	if (unsavedChanges) {
		saveFavouriteBank();
	}
	favourites.clear();
	// Dealoccating all memory
	std::vector<Favorite>().swap(favourites);
	return;
}

void FavouritesManager::setCategory(const std::string& category) {
	currentCategory = category;
	favourites.clear();
	favourites.resize(16);
	selectFavouritesBank(0);
}

std::string FavouritesManager::getFilenameForSave() const {
	return "SETTINGS/FAVOURITES/" + currentCategory + "_Bank" + std::to_string(currentBankNumber) + ".xml";
}

void FavouritesManager::loadFavouritesBank() {
	favourites.clear();
	favourites.resize(16);
	std::string filePath = getFilenameForSave();
	FilePointer fileToLoad;
	bool fileExists = StorageManager::fileExists(filePath.c_str(), &fileToLoad);
	if (!fileExists) {
		// create emtpy File
		saveFavouriteBank();
	}
	String path;
	path.set(filePath.c_str());
	Error error = StorageManager::loadFavouriteFile(&fileToLoad, &path);
}

Error FavouritesManager::loadFavouritesFromFile(Deserializer& reader) {
	reader.match('{');
	char const* tagName;
	uint8_t i = 0;
	String fileName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "favourite")) {
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "position")) {
					i = reader.readTagOrAttributeValueInt();
					favourites[i].position = i;
				}
				else if (!strcmp(tagName, "colour")) {
					favourites[i].colour = reader.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, "instrumentPresetFolder")) {
					reader.readTagOrAttributeValueString(&fileName);
					favourites[i].filename = fileName.get();
				}
			}
		}
	}
	return Error::NONE;
}

void FavouritesManager::saveFavouriteBank() const {

	std::string filePath = getFilenameForSave();
	Error error = StorageManager::createXMLFile(filePath.c_str(), smSerializer, true, true);
	if (error != Error::FILE_ALREADY_EXISTS && error != Error::NONE) {
		return;
	}

	if (favourites.empty()) {
		return;
	}

	Serializer& writer = GetSerializer();

	char buffer[9];
	writer.writeArrayStart("favourites");
	for (const auto& fav : favourites) {
		if (fav.colour.has_value()) {
			writer.writeOpeningTagBeginning("favourite");
			writer.writeAttribute("position", fav.position);
			writer.writeAttribute("colour", fav.colour.value());
			writer.writeAttribute("instrumentPresetFolder", fav.filename.c_str());
			writer.closeTag();
		}
	}
	writer.writeArrayEnding("favourites");
	error = writer.closeFileAfterWriting();
	unsavedChanges = false;
	return;
}

void FavouritesManager::selectFavouritesBank(uint8_t bankNumber) {
	if (bankNumber > 15)
		return;
	if (unsavedChanges) {
		saveFavouriteBank();
	}
	currentBankNumber = bankNumber;
	currentFavouriteNumber = std::nullopt;
	loadFavouritesBank();
}

void FavouritesManager::setFavorite(uint8_t position, uint8_t colour, const std::string& filename) {
	if (position >= 16)
		return;
	if (favourites.size() <= position) {
		favourites.resize(16);
	}
	currentFavouriteNumber = position;
	favourites[position] = Favorite(position, static_cast<uint8_t>(colour), filename);
	saveFavouriteBank();
}

void FavouritesManager::unsetFavorite(uint8_t position) {
	if (position >= 16)
		return;
	currentFavouriteNumber = position;
	favourites[position] = Favorite(position, std::nullopt, "");
	saveFavouriteBank();
}

bool FavouritesManager::isEmpty(uint8_t position) const {
	if (position >= 16)
		return true;
	return !favourites[position].colour.has_value();
}

std::array<std::optional<uint8_t>, 16> FavouritesManager::getFavouriteColours() const {
	std::array<std::optional<uint8_t>, 16> colours;
	for (uint8_t i = 0; i < 16; i++) {
		colours[i] = favourites[i].colour;
	}
	return colours; // Copy elision makes this efficient
}

void FavouritesManager::changeColour(uint8_t position, int32_t offset) {
	if (position < 16 && favourites[position].colour.has_value()) {
		favourites[position].colour = ((favourites[position].colour.value() + offset) % 16 + 16) % 16;
		unsavedChanges = true;
		return;
	}
	return;
}

const std::string& FavouritesManager::getFavoriteFilename(uint8_t position) {
	static const std::string emptyString = ""; // Safe default value
	currentFavouriteNumber = position;
	if (position < 0 || position >= 16 || favourites.size() != 16 || !favourites[position].colour.has_value()) {
		return emptyString; // Return a reference to an empty string instead of nullptr
	}
	return favourites[position].filename;
}
