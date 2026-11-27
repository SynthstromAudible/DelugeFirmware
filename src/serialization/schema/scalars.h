#pragma once

/// @file
/// @brief The scalar codecs for built-in member types: whole numbers and enums (as numbers, or as words), bools, text
/// and reals.

#include "serialization/schema/descriptor.h"

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace deluge::serialization::schema {
namespace detail {

template <typename T>
using Stored = std::conditional_t<std::is_enum_v<T>, std::underlying_type<T>, std::type_identity<T>>::type;

template <typename T>
[[nodiscard]] constexpr int64_t to_int64(T value) {
	const auto stored = static_cast<Stored<T>>(value);
	if constexpr (std::is_unsigned_v<Stored<T>> && sizeof(Stored<T>) == sizeof(int64_t)) {
		return static_cast<int64_t>(std::min<Stored<T>>(stored, std::numeric_limits<int64_t>::max()));
	}
	else {
		return static_cast<int64_t>(stored);
	}
}

template <typename T>
void write_integer(const void* member, const ScalarRule& rule, Writer& out) {
	const int64_t stored = to_int64(*static_cast<const T*>(member));
	if (rule.words.empty()) {
		out.integer(stored);
		return;
	}
	const bool named = stored >= 0 && std::cmp_less(stored, rule.words.size());
	out.text(named ? rule.words[static_cast<std::size_t>(stored)] : rule.words.front());
}

template <typename T>
std::expected<void, LoadError::Kind> read_integer(void* member, const ScalarRule& rule, Reader& in,
                                                  WalkContext& context) {
	int64_t stored = 0;
	if (!rule.words.empty()) {
		const std::optional<std::string> word = in.text();
		if (!word) {
			return std::unexpected(LoadError::Kind::WRONG_TYPE);
		}
		const auto named = std::ranges::find(rule.words, *word);
		if (named == rule.words.end()) {
			context.note(LoadNote::Kind::UNKNOWN_WORD);
		}
		else {
			stored = named - rule.words.begin();
		}
	}
	else {
		const std::optional<int64_t> number = in.integer();
		if (!number) {
			return std::unexpected(LoadError::Kind::WRONG_TYPE);
		}
		stored = std::clamp(*number, rule.min, rule.max);
		if (stored != *number) {
			context.note(LoadNote::Kind::CLAMPED);
		}
	}
	*static_cast<T*>(member) = static_cast<T>(static_cast<Stored<T>>(stored));
	return {};
}

inline void write_bool(const void* member, const ScalarRule& /*rule*/, Writer& out) {
	out.boolean(*static_cast<const bool*>(member));
}
inline std::expected<void, LoadError::Kind> read_bool(void* member, const ScalarRule& /*rule*/, Reader& in,
                                                      WalkContext& /*context*/) {
	const std::optional<bool> value = in.boolean();
	if (!value) {
		return std::unexpected(LoadError::Kind::WRONG_TYPE);
	}
	*static_cast<bool*>(member) = *value;
	return {};
}

inline void write_text(const void* member, const ScalarRule& /*rule*/, Writer& out) {
	out.text(*static_cast<const std::string*>(member));
}
inline std::expected<void, LoadError::Kind> read_text(void* member, const ScalarRule& /*rule*/, Reader& in,
                                                      WalkContext& /*context*/) {
	std::optional<std::string> value = in.text();
	if (!value) {
		return std::unexpected(LoadError::Kind::WRONG_TYPE);
	}
	*static_cast<std::string*>(member) = std::move(*value);
	return {};
}

template <typename T>
void write_real(const void* member, const ScalarRule& /*rule*/, Writer& out) {
	out.real(static_cast<double>(*static_cast<const T*>(member)));
}
template <typename T>
std::expected<void, LoadError::Kind> read_real(void* member, const ScalarRule& /*rule*/, Reader& in,
                                               WalkContext& /*context*/) {
	const std::optional<double> value = in.real();
	if (!value) {
		return std::unexpected(LoadError::Kind::WRONG_TYPE);
	}
	*static_cast<T*>(member) = static_cast<T>(*value);
	return {};
}

template <typename T>
consteval ScalarCodec scalar_codec() {
	if constexpr (std::is_same_v<T, bool>) {
		return {.form = "bool", .write = &write_bool, .read = &read_bool};
	}
	else if constexpr (std::is_same_v<T, std::string>) {
		return {.form = "text", .write = &write_text, .read = &read_text};
	}
	else if constexpr (std::is_floating_point_v<T>) {
		return {.form = "real", .write = &write_real<T>, .read = &read_real<T>};
	}
	else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
		return {.form = "integer", .write = &write_integer<T>, .read = &read_integer<T>};
	}
	else {
		static_assert(false, "no built-in scalar codec for this member type: give its field a ScalarCodec");
	}
}

} // namespace detail

/// @brief The codec for a member of type @p T.
/// @tparam T A bool, std::string, floating-point, integral or enum type.
template <typename T>
inline constexpr ScalarCodec kScalar = detail::scalar_codec<T>();

/// @brief @p rule narrowed to what a member of type @p T holds: a choice to its words' indices, a whole number or an
/// enum to its type's range as well as the rule's.
/// @tparam T The member's type.
/// @param rule The field's rule.
/// @return The rule the field records and applies.
template <typename T>
[[nodiscard]] constexpr ScalarRule narrowed(ScalarRule rule) {
	if (!rule.words.empty()) {
		rule.min = 0;
		rule.max = static_cast<int64_t>(rule.words.size()) - 1;
		return rule;
	}
	if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
		using S = detail::Stored<T>;
		rule.min = std::max(rule.min, detail::to_int64(std::numeric_limits<S>::min()));
		rule.max = std::min(rule.max, detail::to_int64(std::numeric_limits<S>::max()));
	}
	return rule;
}

} // namespace deluge::serialization::schema
