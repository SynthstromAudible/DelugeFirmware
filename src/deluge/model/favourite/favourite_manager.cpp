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
#include "io/debug/log.h"
#include <cstdint>
#include <string.h>

FavouritesManager favouritesManager{};

FavouritesManager::FavouritesManager() {
}

FavouritesManager::~FavouritesManager() {
	saveFavouriteBank();
}
void FavouritesManager::close() {
	if (unsavedChanges) {
		saveFavouriteBank();
	}
	favourites.clear();
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

void FavouritesManager::loadSubcategory(int subcategoryIndex) {
	D_PRINTLN("Loading subcategory %d", subcategoryIndex);
	favourites.clear();
	favourites.resize(16);
	std::string filePath = getFilenameForSave();
	FilePointer fileToLoad;
	bool fileExists = StorageManager::fileExists(filePath.c_str(), &fileToLoad);
	if (!fileExists) {
		D_PRINTLN("No file found for subcategory %d", subcategoryIndex);
		return;
	}
	D_PRINTLN("File to load %s", filePath.c_str());
	String path;
	path.set(filePath.c_str());
	D_PRINTLN("SM load");
	Error error = StorageManager::loadFavouriteFile(&fileToLoad, &path);
	D_PRINTLN("Error loading favourites: %i", error);
}

Error FavouritesManager::loadFavouritesFromFile(Deserializer& reader) {
	reader.match('{');
	char const* tagName;
	uint8_t i = 0;
	String fileName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		D_PRINTLN("Reading tag %s", tagName);
		if (!strcmp(tagName, "favourite")) {
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "position")) {
					favourites[i].position = reader.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, "colour")) {
					favourites[i].colour = reader.readTagOrAttributeValueInt();
				}
				else if (!strcmp(tagName, "instrumentPresetFolder")) {
					reader.readTagOrAttributeValueString(&fileName);
					favourites[i].filename = fileName.get();
				}
			}
			i++;
		}
	}
	return Error::NONE;
}

void FavouritesManager::saveFavouriteBank() const {

	std::string filePath = getFilenameForSave();
	Error error = StorageManager::createXMLFile(filePath.c_str(), smSerializer, true, true);
	D_PRINTLN("Saving Error  %i", error);
	if (error != Error::FILE_ALREADY_EXISTS && error != Error::NONE) {
		D_PRINTLN("Error saving favourites: %s", error);
		return;
	}

	D_PRINTLN("Saving subcategory %d", currentBankNumber);
	if (favourites.empty()) {
		D_PRINTLN("No favourites to save");
		return;
	}

	Serializer& writer = GetSerializer();

	char buffer[9];
	writer.writeArrayStart("favourites");
	for (const auto& fav : favourites) {
		D_PRINTLN("Saving favourite at position %d Colour: %i val: %s", fav.position, fav.colour, fav.filename.c_str());
		if (fav.colour != -1) {
			writer.writeOpeningTagBeginning("favourite");
			writer.writeAttribute("position", fav.position);
			writer.writeAttribute("colour", fav.colour);
			writer.writeAttribute("instrumentPresetFolder", fav.filename.c_str());
			writer.closeTag();
		}
	}
	unsavedChanges = false;
	writer.writeArrayEnding("favourites");
	error = writer.closeFileAfterWriting();
	D_PRINTLN("Error saving favourites: %s", error);
	return;
}

void FavouritesManager::selectFavouritesBank(uint8_t bankNumber) {
	D_PRINTLN("Selecting subcategory %d", bankNumber);
	if (bankNumber < 0 || bankNumber > 15)
		return;
	if (unsavedChanges) {
		saveFavouriteBank();
	}
	currentBankNumber = bankNumber;
	loadSubcategory(currentBankNumber);
}

void FavouritesManager::setFavorite(uint8_t position, int8_t color, const std::string& filename) {
	D_PRINTLN("Setting favorite at position %d to %s", position, filename.c_str());
	if (position < 0 || position >= 16)
		return;
	if (favourites.size() <= position) {
		favourites.resize(16);
	}
	favourites[position] = Favorite(position, color, filename);
	D_PRINTLN("Stored favorite at position %d val: %s", position, favourites[position].filename.c_str());
	saveFavouriteBank();
}

void FavouritesManager::unsetFavorite(uint8_t position) {
	if (position < 0 || position >= 16)
		return;
	D_PRINTLN("Unsetting favorite at position %i", position);
	favourites[position] = Favorite(position, -1, "");
	saveFavouriteBank();
}

bool FavouritesManager::isEmtpy(uint8_t position) const {
	if (position < 0 || position >= 16)
		return true;
	return favourites[position].colour == -1;
}

std::array<int8_t, 16> FavouritesManager::getFavouriteColours() const {
	std::array<int8_t, 16> colours;
	for (uint8_t i = 0; i < 16; i++) {
		colours[i] = favourites[i].colour;
	}
	return colours; // Copy elision makes this efficient
}

int8_t FavouritesManager::changeColour(uint8_t position, int32_t offset) {
	D_PRINTLN("Changing color at position %d by %d", position, offset);
	if (position >= 0 && position < 16) {
		favourites[position].colour = ((favourites[position].colour + offset) % 16 + 16) % 16;
		D_PRINTLN("Color at position %d is now %d", position, favourites[position].colour);
		unsavedChanges = true;
		return favourites[position].colour;
	}
	return -1;
}

const std::string& FavouritesManager::getFavoriteFilename(uint8_t position) const {
	static const std::string emptyString = ""; // Safe default value

	if (position < 0 || position >= 16 || favourites.size() != 16 || favourites[position].colour == -1) {
		return emptyString; // Return a reference to an empty string instead of nullptr
	}

	D_PRINTLN("Getting favorite at position %d val: %s color: %i", position, favourites[position].filename,
	          favourites[position].colour);

	return favourites[position].filename;
}
