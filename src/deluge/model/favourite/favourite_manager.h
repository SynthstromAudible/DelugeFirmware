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
#include "model/favourite/favourites_policy.h"
#include "storage/byte_channel.h"
#include "storage/storage_manager.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

// Number of favourite slots per bank (one pad row). NB: this is *not* the colour-palette size or the
// bank count, which are independently 16 - don't reuse kNumFavourites for those.
constexpr size_t kNumFavourites = 16;

/// @brief The favourites bank on screen, and every card operation it needs, in order.
///
/// Nothing here touches the card from the caller's context. An entry point changes the bank in memory and
/// QUEUES what the card must do; @ref serviceRoutine issues one operation at a time -- through the byte
/// channel, or on the storage owner when the channel refuses -- and applies what it produced on the
/// interaction tier.
class FavouritesManager {
public:
	struct Favorite {
		int position{};
		std::optional<uint8_t> colour{};
		std::string filename{};
	};

	FavouritesManager();

	/// @brief Show @p category's bank 0, saving the outgoing bank first if it has unsaved changes.
	void setCategory(const std::string& category);
	/// @brief Show bank @p bankNumber, saving the outgoing bank first if it has unsaved changes.
	void selectFavouritesBank(uint8_t bankNumber);
	/// @brief Set a slot and queue a save. Refused unless the bank is `Loaded`.
	void setFavorite(uint8_t position, uint8_t colour, const std::string& filename);
	/// @brief Clear a slot and queue a save. Refused unless the bank is `Loaded`.
	void unsetFavorite(uint8_t position);
	bool isEmpty(uint8_t position) const;
	/// @brief Read a bank document into @p into, which must already hold kNumFavourites slots.
	static Error loadFavouritesFromFile(Deserializer& reader, std::vector<Favorite>& into);
	/// @brief Leave the bank: queue a save of unsaved colour changes, then forget the category.
	void close();
	std::array<std::optional<uint8_t>, kNumFavourites> getFavouriteColours() const;
	/// @brief Change a slot's colour in memory. Refused unless the bank is `Loaded`; saved by the next
	///        bank change or @ref close.
	void changeColour(uint8_t position, int32_t offset);
	const std::string& getFavoriteFilename(uint8_t position);
	static constexpr uint8_t favouriteDefaultColor = 4;

	/// @brief The file holding @p category's bank @p bank.
	[[nodiscard]] static std::string bankPath(const std::string& category, uint8_t bank);

	/// @brief Apply a finished operation's result, then issue the next. Called from the "settings io" task.
	void serviceRoutine();

	/// @name What the favourites-channel gate reads
	/// @{
	[[nodiscard]] deluge::favourites::BankState bankState() const { return bankState_; }
	/// @brief Nothing waiting, nothing in flight, nothing waiting to be applied.
	[[nodiscard]] bool idle() const { return !inFlight_.active && waiting_.empty(); }
	[[nodiscard]] int32_t intentsIssued() const { return intentsIssued_; }
	[[nodiscard]] int32_t channelCompletions() const { return channelCompletions_; }
	[[nodiscard]] int32_t ownerCompletions() const { return ownerCompletions_.load(std::memory_order_acquire); }
	/// @}

	uint8_t currentBankNumber{};
	std::optional<uint8_t> currentFavouriteNumber{};

private:
	/// The one operation issued and not yet applied.
	///
	/// The owner arm fills `outcome` and `scratch` on the storage owner and only THEN sets `resultReady`;
	/// the interaction tier reads nothing but `resultReady` until it is set, and the owner touches nothing
	/// after setting it.
	struct InFlight {
		deluge::favourites::FavouritesIntent intent;
		bool active = false;
		std::atomic<bool> resultReady{false};
		deluge::storage::bytes::SettingsOutcome outcome = deluge::storage::bytes::SettingsOutcome::Unreadable;
		std::vector<Favorite> scratch;
	};

	[[nodiscard]] static std::vector<Favorite> emptyBank();
	void resetFavourites();
	[[nodiscard]] std::string getFilenameForSave() const;
	[[nodiscard]] std::string serialiseBank() const;
	void queueSaveOfCurrentBank();
	void requestLoad();
	void issueNext();
	void applyResult();

	static void parseBankBytes(void* ctx, char* bytes, uint32_t len);
	static void channelDone(void* ctx, deluge::storage::bytes::SettingsOutcome outcome);
	static void runIntentOnOwner(void* ctx);
	static void loadOnOwner(InFlight& inFlight);

	bool unsavedChanges = false;
	std::string currentCategory{};
	std::vector<Favorite> favourites{};

	deluge::favourites::BankState bankState_ = deluge::favourites::BankState::Loaded;
	uint32_t generation_ = 0;
	std::deque<deluge::favourites::FavouritesIntent> waiting_;
	InFlight inFlight_;
	int32_t intentsIssued_ = 0;
	int32_t channelCompletions_ = 0;
	std::atomic<int32_t> ownerCompletions_{0};
};

extern FavouritesManager favouritesManager;
