#pragma once
#include "gui/l10n/strings.h"
#include "util/container/static_vector.hpp"
#include "util/misc.h"
#include <array>
#include <string>
#include <utility>

namespace deluge::l10n {

class Language;
constexpr size_t kMaxNumLanguages = 4;
extern static_vector<Language const*, kMaxNumLanguages> languages;
extern Language const* chosenLanguage;

class Language {
public:
	using map_type = std::array<std::string_view, kNumStrings>;

	Language(std::string name, Language const* fallback = languages[0]) : name_(std::move(name)) {
		std::copy(fallback->map_.cbegin(), fallback->map_.cend(), this->map_.begin());
	};

	/** @brief Builder-style constructor for creating localization language maps (compile-time only) */
	consteval Language(char const* name, std::initializer_list<std::pair<l10n::String, const char*>> stringmaps,
	                   const Language* fallback = nullptr)
	    : name_(name) {

		if (fallback != nullptr) { // copy the string pointers from the fallback
			std::copy(fallback->map_.cbegin(), fallback->map_.cend(), this->map_.begin());
		}
		else { // If we have no languages yet...
			   // default all values to empty string
			std::fill(map_.begin(), map_.end(), "");
		}

		// Replace with stringmap values for valid entries
		for (auto [string_id, s] : stringmaps) {
			map_.at(util::to_underlying(string_id)) = s;
		}
	}

	[[nodiscard]] constexpr map_type::value_type get(String entry) const {
		return map_.at(util::to_underlying(entry));
	}

	constexpr Language& add(String entry, map_type::value_type value) {
		size_t idx = util::to_underlying(entry);
		map_[idx] = value;
		return *this;
	}

	constexpr std::string_view name() { return name_; }

private:
	std::string name_;
	map_type map_{};
};
} // namespace deluge::l10n
