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
	resetFavourites();
}

FavouritesManager::~FavouritesManager() {
	if (!currentCategory.empty()) {
		saveFavouriteBank();
	}
}
void FavouritesManager::close() {
	if (unsavedChanges) {
		saveFavouriteBank();
	}
	resetFavourites();
	currentCategory.clear();
	currentFavouriteNumber = std::nullopt;
}

// Reset all 16 favourite slots to their default (no colour/filename), with position = slot index.
void FavouritesManager::resetFavourites() {
	favourites.clear();
	favourites.resize(16);
	for (uint8_t i = 0; i < 16; i++) {
		favourites[i].position = i;
	}
}

void FavouritesManager::setCategory(const std::string& category) {
	// Save the current category's bank before switching - must happen before currentCategory changes, or it'd be
	// saved under the new category's filename.
	if (unsavedChanges && !currentCategory.empty()) {
		saveFavouriteBank();
	}
	currentCategory = category;
	currentBankNumber = 0;
	currentFavouriteNumber = std::nullopt;
	loadFavouritesBank();
}

std::string FavouritesManager::getFilenameForSave() const {
	return "SETTINGS/FAVOURITES/" + currentCategory + "_Bank" + std::to_string(currentBankNumber) + ".xml";
}

void FavouritesManager::loadFavouritesBank() {
	resetFavourites();
	std::string filePath = getFilenameForSave();
	FilePointer fileToLoad{};
	bool fileExists = StorageManager::fileExists(filePath.c_str(), &fileToLoad);
	if (!fileExists) {
		// Create an empty bank file and keep the in-memory defaults.
		saveFavouriteBank();
		return;
	}
	std::string path;
	path = filePath.c_str();
	Error error = StorageManager::loadFavouriteFile(&fileToLoad, &path);
	if (error != Error::NONE) {
		resetFavourites();
	}
}

Error FavouritesManager::loadFavouritesFromFile(Deserializer& reader) {
	reader.match('{');
	char const* tagName;
	std::string fileName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "favourite")) {
			// Bound the index read from the file: favourites is sized to 16, so a corrupt/out-of-range
			// position must not index past the end (was an unchecked favourites[i] OOB write).
			int32_t position = -1;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "position")) {
					position = reader.readTagOrAttributeValueInt();
					if (position >= 0 && position < (int32_t)favourites.size()) {
						favourites[position].position = position;
					}
					else {
						position = -1;
					}
				}
				else if (!strcmp(tagName, "colour")) {
					int32_t colour = reader.readTagOrAttributeValueInt();
					if (position >= 0) {
						favourites[position].colour = static_cast<uint8_t>(colour);
					}
				}
				else if (!strcmp(tagName, "instrumentPresetFolder")) {
					reader.readTagOrAttributeValueString(fileName);
					if (position >= 0) {
						favourites[position].filename = fileName.c_str();
					}
				}
			}
		}
	}
	return Error::NONE;
}

void FavouritesManager::saveFavouriteBank() const {
	if (currentCategory.empty()) {
		return;
	}

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
	if (position >= 16 || favourites.size() <= position)
		return true;
	return !favourites[position].colour.has_value();
}

std::array<std::optional<uint8_t>, 16> FavouritesManager::getFavouriteColours() const {
	std::array<std::optional<uint8_t>, 16> colours{};
	for (uint8_t i = 0; i < 16 && i < favourites.size(); i++) {
		colours[i] = favourites[i].colour;
	}
	return colours; // Copy elision makes this efficient
}

void FavouritesManager::changeColour(uint8_t position, int32_t offset) {
	if (position < 16 && favourites.size() > position && favourites[position].colour.has_value()) {
		favourites[position].colour = ((favourites[position].colour.value() + offset) % 16 + 16) % 16;
		unsavedChanges = true;
		return;
	}
	return;
}

const std::string& FavouritesManager::getFavoriteFilename(uint8_t position) {
	static const std::string emptyString = ""; // Safe default value
	currentFavouriteNumber = position;
	if (position >= 16 || favourites.size() <= position || !favourites[position].colour.has_value()) {
		return emptyString; // Return a reference to an empty string instead of nullptr
	}
	return favourites[position].filename;
}
