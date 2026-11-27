#pragma once

/// @file
/// @brief The keys a document held that a record's schema does not know, kept to be written back.

#include "serialization/value.h"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::serialization::schema {

/// @brief The keys a document held that a record's schema does not know, in the order read, kept so that saving never
/// drops what the firmware does not understand. A pointer wide, and empty unless a document held such keys.
///
/// Identities (addresses, targets, MIDI commands) hold none: their schemas describe them as closed records, so an
/// equality that identifies one never compares kept keys.
class Extras {
public:
	using Entry = std::pair<std::string, Value>;

	Extras() = default;
	Extras(const Extras& other) : entries_{copy_of(other)} {}
	Extras& operator=(const Extras& other) {
		if (this != &other) {
			entries_ = copy_of(other);
		}
		return *this;
	}
	Extras(Extras&&) noexcept = default;
	Extras& operator=(Extras&&) noexcept = default;
	~Extras() = default;

	/// @brief Whether nothing is kept.
	/// @return True when no unknown key was read.
	[[nodiscard]] bool empty() const { return entries_ == nullptr; }
	/// @brief The kept entries, in the order read.
	/// @return The entries.
	[[nodiscard]] std::span<const Entry> entries() const {
		return entries_ != nullptr ? std::span<const Entry>{*entries_} : std::span<const Entry>{};
	}
	/// @brief Whether an entry is kept at @p key.
	/// @param key The key.
	/// @return True when one is.
	[[nodiscard]] bool holds(std::string_view key) const {
		return std::ranges::any_of(entries(), [&](const Entry& entry) { return entry.first == key; });
	}
	/// @brief Keep @p value at @p key, in place of an entry already kept there.
	/// @param key   The key.
	/// @param value The value, as the document held it.
	void keep(std::string key, Value value) {
		if (entries_ == nullptr) {
			entries_ = std::make_unique<std::vector<Entry>>();
		}
		const auto held = std::ranges::find(*entries_, key, &Entry::first);
		if (held != entries_->end()) {
			held->second = std::move(value);
			return;
		}
		entries_->emplace_back(std::move(key), std::move(value));
	}

	/// @brief Equal when the same entries are kept in the same order.
	friend bool operator==(const Extras& a, const Extras& b) { return std::ranges::equal(a.entries(), b.entries()); }

private:
	[[nodiscard]] static std::unique_ptr<std::vector<Entry>> copy_of(const Extras& other) {
		return other.entries_ != nullptr ? std::make_unique<std::vector<Entry>>(*other.entries_) : nullptr;
	}

	std::unique_ptr<std::vector<Entry>> entries_;
};

} // namespace deluge::serialization::schema
