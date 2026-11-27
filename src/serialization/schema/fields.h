#pragma once

/// @file
/// @brief The typed builders that make descriptors: a field from a member pointer or a projection, a record from its
/// fields.
///
/// A projection is a captureless lambda taking the described object by reference (const or not, so write it with
/// `auto&`) and returning its member by reference: `[](auto& view) -> auto& { return view.entry->id; }`.

#include "serialization/schema/containers.h"
#include "serialization/schema/descriptor.h"
#include "serialization/schema/scalars.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deluge::serialization::schema {
namespace detail {

template <auto Member>
struct MemberOf;
template <typename R, typename T, T R::* Member>
struct MemberOf<Member> {
	using Record = R;
	using Type = T;
};

template <typename View, auto Project>
using Projected = std::remove_cvref_t<decltype(Project(std::declval<View&>()))>;

} // namespace detail

/// The type of the member @p Member points to.
template <auto Member>
using MemberType = detail::MemberOf<Member>::Type;

/// @brief The access of a member, from a pointer to it.
template <auto Member>
inline constexpr Access kMemberAccess{
    .get = [](const void* owner) -> const void* {
	    return &(static_cast<const detail::MemberOf<Member>::Record*>(owner)->*Member);
    },
    .get_mut = [](void* owner) -> void* { return &(static_cast<detail::MemberOf<Member>::Record*>(owner)->*Member); }};

/// @brief The access of a member a projection names, from a view of type @p View (or @p ConstView when only read).
template <typename ConstView, typename View, auto Project>
inline constexpr Access kProjectedAccess{
    .get = [](const void* owner) -> const void* {
	    return std::addressof(Project(*static_cast<const ConstView*>(owner)));
    },
    .get_mut = [](void* owner) -> void* { return std::addressof(Project(*static_cast<View*>(owner))); }};

/// @brief The access of a member a projection names, on a record of type @p R.
template <typename R, auto Project>
inline constexpr Access kProjected = kProjectedAccess<R, R, Project>;

/// @brief The access of the object itself: a list item's or an optional's value field.
inline constexpr Access kSelf{.get = [](const void* owner) { return owner; },
                              .get_mut = [](void* owner) { return owner; }};

/// @brief A scalar field.
/// @param key  The field's key.
/// @param rule Its range or words; narrowed to what the member's type holds.
template <auto Member>
[[nodiscard]] constexpr FieldDesc scalar(std::string_view key, ScalarRule rule = {}) {
	using T = MemberType<Member>;
	return {.key = key,
	        .access = kMemberAccess<Member>,
	        .shape = ScalarShape{.codec = &kScalar<T>, .rule = narrowed<T>(rule)}};
}

/// @brief A scalar field a projection names.
template <typename ConstView, typename View, auto Project>
[[nodiscard]] constexpr FieldDesc scalar_at(std::string_view key, ScalarRule rule = {}) {
	using T = detail::Projected<View, Project>;
	return {.key = key,
	        .access = kProjectedAccess<ConstView, View, Project>,
	        .shape = ScalarShape{.codec = &kScalar<T>, .rule = narrowed<T>(rule)}};
}

/// @brief A scalar field with its own codec (a member type the built-in codecs do not cover).
template <auto Member>
[[nodiscard]] constexpr FieldDesc scalar_with(std::string_view key, const ScalarCodec& codec, ScalarRule rule) {
	return {.key = key, .access = kMemberAccess<Member>, .shape = ScalarShape{.codec = &codec, .rule = rule}};
}

/// @brief A scalar field a projection names, with its own codec.
template <typename ConstView, typename View, auto Project>
[[nodiscard]] constexpr FieldDesc scalar_with_at(std::string_view key, const ScalarCodec& codec, ScalarRule rule) {
	return {.key = key,
	        .access = kProjectedAccess<ConstView, View, Project>,
	        .shape = ScalarShape{.codec = &codec, .rule = rule}};
}

/// @brief A nested record field.
template <auto Member>
[[nodiscard]] constexpr FieldDesc record(std::string_view key, const RecordDesc& desc) {
	return {.key = key, .access = kMemberAccess<Member>, .shape = RecordShape{.record = &desc}};
}

/// @brief A nested record field a projection names.
template <typename ConstView, typename View, auto Project>
[[nodiscard]] constexpr FieldDesc record_at(std::string_view key, const RecordDesc& desc) {
	return {.key = key, .access = kProjectedAccess<ConstView, View, Project>, .shape = RecordShape{.record = &desc}};
}

/// @brief An optional field: @p value describes the held value (a value_… field).
template <auto Member>
[[nodiscard]] constexpr FieldDesc optional(std::string_view key, const FieldDesc& value) {
	return {.key = key,
	        .access = kMemberAccess<Member>,
	        .shape = OptionalShape{.ops = kOptional<MemberType<Member>>, .value = &value}};
}

/// @brief An optional field a projection names.
template <typename ConstView, typename View, auto Project>
[[nodiscard]] constexpr FieldDesc optional_at(std::string_view key, const FieldDesc& value) {
	return {.key = key,
	        .access = kProjectedAccess<ConstView, View, Project>,
	        .shape = OptionalShape{.ops = kOptional<detail::Projected<View, Project>>, .value = &value}};
}

/// @brief A list field over a standard container.
/// @param element Describes an item (a value_… field).
/// @param id_key  The key of the item field that is unique across the list; empty for none.
template <auto Member>
[[nodiscard]] constexpr FieldDesc list(std::string_view key, const FieldDesc& element, std::string_view id_key = {}) {
	return {.key = key,
	        .access = kMemberAccess<Member>,
	        .shape = ListShape{.ops = kList<MemberType<Member>>, .element = &element, .id_key = id_key}};
}

/// @brief A list field with its own ops: a list built over something other than one container.
[[nodiscard]] constexpr FieldDesc list_with(std::string_view key, Access access, const ListOps& ops,
                                            const FieldDesc& element, std::string_view id_key = {}) {
	return {.key = key, .access = access, .shape = ListShape{.ops = &ops, .element = &element, .id_key = id_key}};
}

/// @brief A field of several kinds.
template <auto Member>
[[nodiscard]] constexpr FieldDesc variant(std::string_view key, const VariantDesc& desc) {
	return {.key = key, .access = kMemberAccess<Member>, .shape = VariantShape{.variant = &desc}};
}

/// @brief A plugin node field.
template <auto Member>
[[nodiscard]] constexpr FieldDesc plugin(std::string_view key, const PluginOps& ops) {
	return {.key = key, .access = kMemberAccess<Member>, .shape = PluginShape{.ops = &ops}};
}

/// @brief A plugin node field whose state is found through @p access.
[[nodiscard]] constexpr FieldDesc plugin_with(std::string_view key, Access access, const PluginOps& ops) {
	return {.key = key, .access = access, .shape = PluginShape{.ops = &ops}};
}

/// @brief A list item or optional value that is a scalar of type @p T.
template <typename T>
[[nodiscard]] constexpr FieldDesc value_scalar(ScalarRule rule = {}) {
	return {.key = {}, .access = kSelf, .shape = ScalarShape{.codec = &kScalar<T>, .rule = narrowed<T>(rule)}};
}

/// @brief A list item or optional value that is a scalar with its own codec.
[[nodiscard]] constexpr FieldDesc value_with(const ScalarCodec& codec, ScalarRule rule) {
	return {.key = {}, .access = kSelf, .shape = ScalarShape{.codec = &codec, .rule = rule}};
}

/// @brief A list item or optional value that is a record.
[[nodiscard]] constexpr FieldDesc value_record(const RecordDesc& desc) {
	return {.key = {}, .access = kSelf, .shape = RecordShape{.record = &desc}};
}

/// @brief A list item or optional value of several kinds.
[[nodiscard]] constexpr FieldDesc value_variant(const VariantDesc& desc) {
	return {.key = {}, .access = kSelf, .shape = VariantShape{.variant = &desc}};
}

/// @brief A list item or optional value that is a plugin node.
[[nodiscard]] constexpr FieldDesc value_plugin(const PluginOps& ops) {
	return {.key = {}, .access = kSelf, .shape = PluginShape{.ops = &ops}};
}

/// @brief A record that keeps unknown keys in its member @p ExtrasMember.
template <auto ExtrasMember>
[[nodiscard]] constexpr RecordDesc open_record(std::string_view name, std::span<const FieldDesc> fields) {
	return {.name = name,
	        .fields = fields,
	        .extras = [](const void* record) -> const Extras* {
		        return static_cast<const Extras*>(kMemberAccess<ExtrasMember>.get(record));
	        },
	        .extras_mut = [](void* record) -> Extras* {
		        return static_cast<Extras*>(kMemberAccess<ExtrasMember>.get_mut(record));
	        }};
}

/// @brief A record over a view that keeps unknown keys in the Extras a projection names.
template <typename ConstView, typename View, auto Project>
[[nodiscard]] constexpr RecordDesc open_record_at(std::string_view name, std::span<const FieldDesc> fields) {
	return {.name = name,
	        .fields = fields,
	        .extras = [](const void* record) -> const Extras* {
		        return static_cast<const Extras*>(kProjectedAccess<ConstView, View, Project>.get(record));
	        },
	        .extras_mut = [](void* record) -> Extras* {
		        return static_cast<Extras*>(kProjectedAccess<ConstView, View, Project>.get_mut(record));
	        }};
}

/// @brief A record that drops unknown keys (an address or identity type).
[[nodiscard]] constexpr RecordDesc closed_record(std::string_view name, std::span<const FieldDesc> fields) {
	return {.name = name, .fields = fields, .extras = nullptr, .extras_mut = nullptr};
}

/// @brief The fields of @p parts, one after another.
template <std::size_t... N>
[[nodiscard]] consteval std::array<FieldDesc, (N + ...)> join(const std::array<FieldDesc, N>&... parts) {
	std::array<FieldDesc, (N + ...)> joined{};
	std::size_t at = 0;
	((std::ranges::copy(parts, joined.begin() + at), at += N), ...);
	return joined;
}

/// @brief Whether no two of @p fields share a key.
[[nodiscard]] consteval bool keys_unique(std::span<const FieldDesc> fields) {
	for (std::size_t i = 0; i < fields.size(); ++i) {
		for (std::size_t j = i + 1; j < fields.size(); ++j) {
			if (fields[i].key == fields[j].key) {
				return false;
			}
		}
	}
	return true;
}

} // namespace deluge::serialization::schema
