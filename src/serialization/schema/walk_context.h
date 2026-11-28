#pragma once

/// @file
/// @brief What a walk carries: where it is, what a decode did, and the caller's own data.

#include "serialization/schema/report.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::serialization::schema {

/// @brief What a walk carries: the path to the value it is at, the report a decode builds, and a pointer to the
/// caller's own data, which plugin nodes read (the codecs they decode through, for one).
class WalkContext {
public:
	/// @brief A context at the document's root.
	/// @param user The caller's data, handed to plugin nodes as it is; null for none.
	explicit WalkContext(const void* user = nullptr) : user_{user} {}

	/// @brief The caller's data.
	/// @return The pointer given at construction.
	[[nodiscard]] const void* user() const { return user_; }
	/// @brief Step into the value at @p key.
	/// @param key The key.
	void enter(std::string_view key) { path_.emplace_back(key); }
	/// @brief Step into a list's item @p index.
	/// @param index The item's index.
	void enter(std::size_t index) { path_.push_back(std::to_string(index)); }
	/// @brief Step back out of the value entered last.
	void leave() { path_.pop_back(); }
	/// @brief Where the walk is.
	/// @return The keys and indices entered, joined by `/`; empty at the root.
	[[nodiscard]] std::string path() const;
	/// @brief Note that the decode did something the document did not say, here.
	/// @param kind What it did.
	void note(LoadNote::Kind kind) { report_.notes.push_back({.kind = kind, .path = path()}); }
	/// @brief An error here.
	/// @param kind What is wrong.
	/// @return The error, with this path.
	[[nodiscard]] LoadError error(LoadError::Kind kind) const { return {.kind = kind, .path = path()}; }
	/// @brief The report built so far.
	/// @return The report.
	[[nodiscard]] LoadReport take_report() && { return std::move(report_); }

private:
	const void* user_;
	std::vector<std::string> path_;
	LoadReport report_;
};

} // namespace deluge::serialization::schema
