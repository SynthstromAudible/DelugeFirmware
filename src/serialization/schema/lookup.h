#pragma once

/// @file
/// @brief A record's member found by its key: the one table that names a record's fields also answers a setting path.

#include "serialization/schema/containers.h"
#include "serialization/schema/descriptor.h"
#include "serialization/schema/scalars.h"

#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

namespace deluge::serialization::schema {
namespace detail {

template <typename T>
inline constexpr bool kIsOptional = false;
template <typename V>
inline constexpr bool kIsOptional<std::optional<V>> = true;

template <typename T>
[[nodiscard]] constexpr bool holds(const FieldDesc& field) {
	if constexpr (kIsOptional<T>) {
		const auto* shape = std::get_if<OptionalShape>(&field.shape);
		return shape != nullptr && shape->ops == kOptional<T>;
	}
	else {
		const auto* shape = std::get_if<ScalarShape>(&field.shape);
		return shape != nullptr && shape->type == &kTypeTag<T>;
	}
}

} // namespace detail

/// @brief The member of @p record at @p key, if it is a @p T.
/// @tparam T A scalar member type, or a std::optional of one.
/// @return The member; nullptr when no field has @p key, or its member is of another type.
template <typename T, typename R>
[[nodiscard]] T* member_at(Schema<R> schema, R& record, std::string_view key) {
	for (const FieldDesc& field : schema.desc->fields) {
		if (field.key == key && detail::holds<T>(field)) {
			return static_cast<T*>(field.access.get_mut(&record));
		}
	}
	return nullptr;
}

/// @brief The member of @p record at @p key, if it is a @p T.
template <typename T, typename R>
[[nodiscard]] const T* member_at(Schema<R> schema, const R& record, std::string_view key) {
	for (const FieldDesc& field : schema.desc->fields) {
		if (field.key == key && detail::holds<T>(field)) {
			return static_cast<const T*>(field.access.get(&record));
		}
	}
	return nullptr;
}

} // namespace deluge::serialization::schema
