#include "gui/ui/shell_action_dispatch.h"
#include "gui/ui/shell_host/shell_host.h"
#include "model/favourite/favourite_manager.h"
#include "model/sample/last_file_path_loaded.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "ui/scenes/menu/bind_host.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::ui::scenes::menu {

namespace {

/// @brief The favourites the legacy browsers show, through `favouritesManager`.
class HostFavourites final : public services::Favourites {
public:
	void set_category(std::string_view category) override { favouritesManager.setCategory(std::string{category}); }
	void close() override { favouritesManager.close(); }
	[[nodiscard]] services::FavouritesBank bank_state() const override {
		switch (favouritesManager.bankState()) {
		case deluge::favourites::BankState::Loading:
			return services::FavouritesBank::Loading;
		case deluge::favourites::BankState::Loaded:
			return services::FavouritesBank::Loaded;
		case deluge::favourites::BankState::Unavailable:
			break;
		}
		return services::FavouritesBank::Unavailable;
	}
	[[nodiscard]] std::array<std::optional<uint8_t>, deluge::ui::widgets::grid::kFavourites> colours() const override {
		return favouritesManager.getFavouriteColours();
	}
	[[nodiscard]] std::optional<uint8_t> current_favourite() const override {
		return favouritesManager.currentFavouriteNumber;
	}
	[[nodiscard]] uint8_t current_bank() const override { return favouritesManager.currentBankNumber; }
	[[nodiscard]] bool is_empty(uint8_t position) const override { return favouritesManager.isEmpty(position); }
	std::string recall(uint8_t position) override { return favouritesManager.getFavoriteFilename(position); }
	void set(uint8_t position, std::string_view path) override {
		favouritesManager.setFavorite(position, FavouritesManager::favouriteDefaultColor, std::string{path});
	}
	void unset(uint8_t position) override { favouritesManager.unsetFavorite(position); }
	void select_bank(uint8_t bank) override { favouritesManager.selectFavouritesBank(bank); }
	void change_colour(uint8_t position, int32_t offset) override { favouritesManager.changeColour(position, offset); }
};

} // namespace

void host_settings_reset() {
	gui::ui::reapplyLiveSettingsAfterReset();
}

std::string host_last_sample_path() {
	return deluge::model::lastFilePathLoaded();
}

services::Favourites& host_favourites() {
	static HostFavourites favourites;
	return favourites;
}

bool host_release_sample_file(std::string_view path) {
	AudioEngine::stopAnyPreviewing();
	return audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(std::string{path}.c_str());
}

} // namespace deluge::ui::scenes::menu
