#include "gui/ui/shell_host/shell_host.h"
#include "model/favourite/favourite_manager.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace deluge::gui::shell_host {
namespace {

/// @brief The favourites the legacy browsers show, through `favouritesManager`.
class HostFavourites final : public deluge::ui::scenes::bind::Favourites {
public:
	void set_category(std::string_view category) override { favouritesManager.setCategory(std::string{category}); }
	void close() override { favouritesManager.close(); }
	[[nodiscard]] deluge::ui::scenes::bind::FavouritesBank bank_state() const override {
		switch (favouritesManager.bankState()) {
		case deluge::favourites::BankState::Loading:
			return deluge::ui::scenes::bind::FavouritesBank::Loading;
		case deluge::favourites::BankState::Loaded:
			return deluge::ui::scenes::bind::FavouritesBank::Loaded;
		case deluge::favourites::BankState::Unavailable:
			break;
		}
		return deluge::ui::scenes::bind::FavouritesBank::Unavailable;
	}
	[[nodiscard]] std::array<std::optional<uint8_t>, deluge::ui::scenes::bind::kFavouriteSlots>
	colours() const override {
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

deluge::ui::scenes::bind::Favourites& favourites() {
	static HostFavourites favourites;
	return favourites;
}

} // namespace deluge::gui::shell_host
