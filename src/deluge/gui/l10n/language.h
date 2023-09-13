#pragma once
#include "gui/l10n/strings.h"
#include "util/misc.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deluge::l10n {

class Language;
constexpr size_t kMaxNumLanguages = 4;
extern Language const* chosenLanguage;

class Language {
	using entry_type = std::pair<l10n::String, std::string_view>;

public:
	using map_type = std::array<std::optional<std::string_view>, kNumStrings>;

	Language(std::string name, Language const* fallback = nullptr) : name_(std::move(name)) {
		std::copy(fallback->map_.cbegin(), fallback->map_.cend(), this->map_.begin());
	};

	/** @brief Builder-style constructor for creating localization language maps (compile-time only) */
	consteval Language(char const* name, std::initializer_list<entry_type> stringmaps,
	                   const Language* fallback = nullptr)
	    : name_(name), fallback_(fallback) {

		// Replace with stringmap values for valid entries
		for (auto [string_id, s] : stringmaps) {
			map_.at(util::to_underlying(string_id)) = s;
		}
	}

	[[nodiscard]] constexpr map_type::value_type get(String entry) const { return map_.at(util::to_underlying(entry)); }

	constexpr Language& add(String entry, map_type::value_type value) {
		size_t idx = util::to_underlying(entry);
		map_[idx] = value;
		return *this;
	}

	[[nodiscard]] constexpr std::string_view name() const { return name_; }

	[[nodiscard]] constexpr bool hasFallback() const { return fallback_ != nullptr; }
	[[nodiscard]] constexpr const Language& fallback() const { return *fallback_; }

private:
	std::string name_;
	map_type map_{};
	const Language* fallback_ = nullptr;
};

} // namespace deluge::l10n

namespace deluge::l10n::built_in {
extern deluge::l10n::Language english;
extern deluge::l10n::Language seven_segment;
} // namespace deluge::l10n::built_in
