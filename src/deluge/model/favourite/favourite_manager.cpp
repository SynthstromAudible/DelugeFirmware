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

#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "storage/existence_policy.h"
#include "storage/owner.h"

#include <cstdint>
#include <span>
#include <string.h>

FavouritesManager favouritesManager{};

namespace {

using deluge::favourites::BankState;
using deluge::favourites::FavouritesIntent;
using deluge::storage::bytes::SettingsOutcome;

/// Write a save's snapshot. Legal only where blocking is: the storage owner.
bool writeSnapshot(const std::string& path, const std::string& bytes) {
	auto created = StorageManager::createFile(path.c_str(), true);
	if (!created.has_value()) {
		return false;
	}
	auto written =
	    created->write(std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
	const bool whole = written.has_value() && *written == bytes.size();
	const bool closed = created->close().has_value();
	return whole && closed;
}

} // namespace

FavouritesManager::FavouritesManager() {
	resetFavourites();
}

std::vector<FavouritesManager::Favorite> FavouritesManager::emptyBank() {
	std::vector<Favorite> bank(kNumFavourites);
	for (uint8_t i = 0; i < kNumFavourites; i++) {
		bank[i].position = i;
	}
	return bank;
}

// Reset all favourite slots to their default (no colour/filename), with position = slot index.
void FavouritesManager::resetFavourites() {
	favourites = emptyBank();
}

std::string FavouritesManager::bankPath(const std::string& category, uint8_t bank) {
	return "SETTINGS/FAVOURITES/" + category + "_Bank" + std::to_string(bank) + ".xml";
}

std::string FavouritesManager::getFilenameForSave() const {
	return bankPath(currentCategory, currentBankNumber);
}

void FavouritesManager::setCategory(const std::string& category) {
	// Save the outgoing bank before the category changes, or it would be saved under the new name.
	if (unsavedChanges && bankState_ == BankState::Loaded) {
		queueSaveOfCurrentBank();
	}
	currentCategory = category;
	currentBankNumber = 0;
	requestLoad();
}

void FavouritesManager::selectFavouritesBank(uint8_t bankNumber) {
	if (bankNumber > 15) {
		return;
	}
	if (unsavedChanges && bankState_ == BankState::Loaded) {
		queueSaveOfCurrentBank();
	}
	currentBankNumber = bankNumber;
	requestLoad();
}

void FavouritesManager::close() {
	if (unsavedChanges && bankState_ == BankState::Loaded) {
		queueSaveOfCurrentBank();
	}
	generation_++; // any load still coming belongs to a bank nobody is looking at
	resetFavourites();
	currentCategory.clear();
	currentFavouriteNumber = std::nullopt;
	unsavedChanges = false;
	bankState_ = BankState::Loaded;
}

void FavouritesManager::requestLoad() {
	generation_++;
	resetFavourites();
	currentFavouriteNumber = std::nullopt;
	unsavedChanges = false;
	bankState_ = BankState::Loading;
	deluge::favourites::enqueueIntent(waiting_, FavouritesIntent{.kind = FavouritesIntent::Kind::Load,
	                                                             .path = getFilenameForSave(),
	                                                             .generation = generation_});
}

std::string FavouritesManager::serialiseBank() const {
	XMLSerializer writer;
	writer.setMemoryBased();
	// The owner arm used to get this head from createXMLFile. Both arms now write this same snapshot, head
	// included, so the two legs' files are byte-identical.
	writer.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
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
	return std::string(writer.getBufferPtr(), static_cast<size_t>(writer.bytesWritten()));
}

void FavouritesManager::queueSaveOfCurrentBank() {
	if (currentCategory.empty()) {
		return;
	}
	deluge::favourites::enqueueIntent(
	    waiting_,
	    FavouritesIntent{.kind = FavouritesIntent::Kind::Save, .path = getFilenameForSave(), .bytes = serialiseBank()});
	unsavedChanges = false;
}

Error FavouritesManager::loadFavouritesFromFile(Deserializer& reader, std::vector<Favorite>& into) {
	reader.match('{');
	char const* tagName;
	std::string fileName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "favourite")) {
			// Bound the index read from the file: a corrupt/out-of-range position must not index past the end.
			int32_t position = -1;
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "position")) {
					position = reader.readTagOrAttributeValueInt();
					if (position >= 0 && position < (int32_t)into.size()) {
						into[position].position = position;
					}
					else {
						position = -1;
					}
				}
				else if (!strcmp(tagName, "colour")) {
					int32_t colour = reader.readTagOrAttributeValueInt();
					if (position >= 0) {
						into[position].colour = static_cast<uint8_t>(colour);
					}
				}
				else if (!strcmp(tagName, "instrumentPresetFolder")) {
					reader.readTagOrAttributeValueString(fileName);
					if (position >= 0) {
						into[position].filename = fileName.c_str();
					}
				}
			}
		}
	}
	return Error::NONE;
}

void FavouritesManager::setFavorite(uint8_t position, uint8_t colour, const std::string& filename) {
	if (position >= kNumFavourites || !deluge::favourites::editAllowed(bankState_)) {
		return;
	}
	currentFavouriteNumber = position;
	favourites[position] = Favorite(position, static_cast<uint8_t>(colour), filename);
	queueSaveOfCurrentBank();
}

void FavouritesManager::unsetFavorite(uint8_t position) {
	if (position >= kNumFavourites || !deluge::favourites::editAllowed(bankState_)) {
		return;
	}
	currentFavouriteNumber = position;
	favourites[position] = Favorite(position, std::nullopt, "");
	queueSaveOfCurrentBank();
}

bool FavouritesManager::isEmpty(uint8_t position) const {
	if (position >= kNumFavourites || favourites.size() <= position) {
		return true;
	}
	return !favourites[position].colour.has_value();
}

std::array<std::optional<uint8_t>, kNumFavourites> FavouritesManager::getFavouriteColours() const {
	std::array<std::optional<uint8_t>, kNumFavourites> colours{};
	for (uint8_t i = 0; i < kNumFavourites && i < favourites.size(); i++) {
		colours[i] = favourites[i].colour;
	}
	return colours;
}

void FavouritesManager::changeColour(uint8_t position, int32_t offset) {
	if (!deluge::favourites::editAllowed(bankState_)) {
		return;
	}
	if (position < kNumFavourites && favourites.size() > position && favourites[position].colour.has_value()) {
		// NB: 16 here is the colour-palette size (wrapping the colour value 0-15), not kNumFavourites.
		favourites[position].colour = ((favourites[position].colour.value() + offset) % 16 + 16) % 16;
		unsavedChanges = true;
	}
}

const std::string& FavouritesManager::getFavoriteFilename(uint8_t position) {
	static const std::string emptyString = "";
	currentFavouriteNumber = position;
	if (position >= kNumFavourites || favourites.size() <= position || !favourites[position].colour.has_value()) {
		return emptyString;
	}
	return favourites[position].filename;
}

void FavouritesManager::serviceRoutine() {
	if (inFlight_.active && inFlight_.resultReady.load(std::memory_order_acquire)) {
		applyResult();
	}
	if (!inFlight_.active && !waiting_.empty()) {
		issueNext();
	}
}

void FavouritesManager::issueNext() {
	namespace bytes = deluge::storage::bytes;
	inFlight_.intent = std::move(waiting_.front());
	waiting_.pop_front();
	inFlight_.active = true;
	inFlight_.resultReady.store(false, std::memory_order_release);
	inFlight_.outcome = SettingsOutcome::Unreadable;
	inFlight_.scratch.clear();
	intentsIssued_++;

	const FavouritesIntent& intent = inFlight_.intent;
	if (bytes::usingByteChannel()) {
		const bool taken =
		    intent.kind == FavouritesIntent::Kind::Load
		        ? bytes::submitRead(intent.path.c_str(), nullptr, &parseBankBytes, nullptr, this, &channelDone)
		        : bytes::submitWrite(intent.path.c_str(), intent.bytes.data(),
		                             static_cast<uint32_t>(intent.bytes.size()), &channelDone, this);
		if (taken) {
			return;
		}
		// Not taken: too large for the channel (a busy channel queues instead). The owner arm does the same work.
	}

	// Dispatched here rather than through runOnStorageOwner(), which silently drops an operation when the owner
	// queue is full -- and a dropped operation would leave this bank in flight forever.
	if (deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started()) {
		runIntentOnOwner(this);
		return;
	}
	if (!deluge::storage::Owner::run(&runIntentOnOwner, this)) {
		inFlight_.outcome =
		    intent.kind == FavouritesIntent::Kind::Load ? SettingsOutcome::Unreadable : SettingsOutcome::WriteFailed;
		inFlight_.resultReady.store(true, std::memory_order_release);
	}
}

void FavouritesManager::parseBankBytes(void* ctx, char* bytes, uint32_t len) {
	auto* self = static_cast<FavouritesManager*>(ctx);
	self->inFlight_.scratch = emptyBank();
	// activeDeserializer goes back on EVERY path -- see MidiFollow::parseDefaultsBytes. A memory parse does no
	// card I/O, so no owner operation can be suspended mid-parse underneath this.
	FileDeserializer* previous = activeDeserializer;
	if (StorageManager::openDelugeBuffer(std::span{reinterpret_cast<uint8_t*>(bytes), len}, false, "favourites")
	    == Error::NONE) {
		(void)loadFavouritesFromFile(*activeDeserializer, self->inFlight_.scratch);
	}
	// A document that will not open leaves the scratch empty and the outcome Parsed: today's "reset to empty
	// and carry on" for a corrupt bank, preserved. The owner arm matches it for FILE_CORRUPTED.
	activeDeserializer = previous;
}

void FavouritesManager::channelDone(void* ctx, SettingsOutcome outcome) {
	auto* self = static_cast<FavouritesManager*>(ctx);
	self->inFlight_.outcome = outcome;
	self->channelCompletions_++;
	self->inFlight_.resultReady.store(true, std::memory_order_release);
}

void FavouritesManager::runIntentOnOwner(void* ctx) {
	deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Favourites);
	auto* self = static_cast<FavouritesManager*>(ctx);
	InFlight& inFlight = self->inFlight_;
	if (inFlight.intent.kind == FavouritesIntent::Kind::Load) {
		loadOnOwner(inFlight);
	}
	else {
		inFlight.outcome = writeSnapshot(inFlight.intent.path, inFlight.intent.bytes) ? SettingsOutcome::Written
		                                                                              : SettingsOutcome::WriteFailed;
	}
	self->ownerCompletions_.fetch_add(1, std::memory_order_acq_rel);
	inFlight.resultReady.store(true, std::memory_order_release);
}

void FavouritesManager::loadOnOwner(InFlight& inFlight) {
	const char* path = inFlight.intent.path.c_str();
	// Unknown is never absent: see the spec's data loss path 1.
	switch (deluge::storage::presence_of(StorageManager::fileExists(path))) {
	case deluge::storage::Presence::Absent:
		inFlight.outcome = SettingsOutcome::Absent;
		return;
	case deluge::storage::Presence::Undeterminable:
		inFlight.outcome = SettingsOutcome::Unreadable;
		return;
	case deluge::storage::Presence::Present:
		break;
	}
	inFlight.scratch = emptyBank();
	Error error = StorageManager::openFavouriteFile(path);
	if (error == Error::FILE_CORRUPTED) {
		inFlight.outcome = SettingsOutcome::Parsed; // corrupt: reset to empty and carry on, as before
		return;
	}
	if (error != Error::NONE) {
		inFlight.outcome = SettingsOutcome::Unreadable;
		return;
	}
	(void)loadFavouritesFromFile(*activeDeserializer, inFlight.scratch);
	const bool closed = activeDeserializer->closeWriter();
	StorageManager::releaseDocumentStaging();
	if (!closed) {
		inFlight.scratch = emptyBank();
	}
	inFlight.outcome = closed ? SettingsOutcome::Parsed : SettingsOutcome::Unreadable;
}

void FavouritesManager::applyResult() {
	const FavouritesIntent& intent = inFlight_.intent;
	const SettingsOutcome outcome = inFlight_.outcome;
	if (intent.kind == FavouritesIntent::Kind::Load) {
		if (deluge::favourites::loadIsCurrent(intent.generation, generation_)) {
			bankState_ = deluge::favourites::stateAfterLoad(outcome);
			if (outcome == SettingsOutcome::Parsed) {
				favourites = std::move(inFlight_.scratch);
			}
			else {
				resetFavourites();
			}
			getCurrentUI()->favouritesBankApplied();
		}
		// else: an answer for a bank the user has since left. Discarded.
	}
	else if (outcome == SettingsOutcome::WriteFailed) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_SD_CARD_ERROR));
		// Retried by the next save or close of this bank, if it is still the one on screen.
		if (!currentCategory.empty() && bankState_ == BankState::Loaded && intent.path == getFilenameForSave()) {
			unsavedChanges = true;
		}
	}
	inFlight_.active = false;
}
