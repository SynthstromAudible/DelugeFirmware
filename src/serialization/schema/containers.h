#pragma once

/// @file
/// @brief The optional, list and variant ops for standard containers.

#include "serialization/schema/descriptor.h"

#include <array>
#include <cstddef>
#include <list>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace deluge::serialization::schema {
namespace detail {

/// @brief The OptionalOps of an optional member type.
template <typename T>
struct OptionalTraits;

template <typename V>
struct OptionalTraits<std::optional<V>> {
	using O = std::optional<V>;
	static bool has_value(const void* member) { return static_cast<const O*>(member)->has_value(); }
	static const void* value(const void* member) { return &**static_cast<const O*>(member); }
	static void* emplace(void* member) { return &static_cast<O*>(member)->emplace(); }
	static void reset(void* member) { static_cast<O*>(member)->reset(); }
	static constexpr OptionalOps kOps{.has_value = &has_value, .value = &value, .emplace = &emplace, .reset = &reset};
};

/// A vector or a list: items appended at the end.
template <typename C>
struct SequenceTraits {
	static void for_each(const void* list, FunctionRef<void(const void*)> visit) {
		for (const auto& item : *static_cast<const C*>(list)) {
			visit(&item);
		}
	}
	static void clear(void* list) { static_cast<C*>(list)->clear(); }
	static Appended append(void* list, std::size_t /*index*/, std::size_t /*alternative*/,
	                       FunctionRef<void(void*)> fill) {
		fill(&static_cast<C*>(list)->emplace_back());
		return Appended::YES;
	}
	static constexpr ListOps kOps{.for_each = &for_each, .clear = &clear, .append = &append};
};

/// @brief The ListOps of a list member type: a vector, a list, a fixed-size array or a map.
template <typename T>
struct ListTraits;

template <typename V, typename A>
struct ListTraits<std::vector<V, A>> : SequenceTraits<std::vector<V, A>> {};

template <typename V, typename A>
struct ListTraits<std::list<V, A>> : SequenceTraits<std::list<V, A>> {};

/// A fixed-size array: a document's items fill it from the front; those past its end are dropped.
template <typename V, std::size_t N>
struct ListTraits<std::array<V, N>> {
	using C = std::array<V, N>;
	static void for_each(const void* list, FunctionRef<void(const void*)> visit) {
		for (const V& item : *static_cast<const C*>(list)) {
			visit(&item);
		}
	}
	static void clear(void* list) { static_cast<C*>(list)->fill(V{}); }
	static Appended append(void* list, std::size_t index, std::size_t /*alternative*/, FunctionRef<void(void*)> fill) {
		if (index >= N) {
			return Appended::FULL;
		}
		V& item = (*static_cast<C*>(list))[index];
		item = V{};
		fill(&item);
		return Appended::YES;
	}
	static constexpr ListOps kOps{.for_each = &for_each, .clear = &clear, .append = &append, .capacity = N};
};

/// A map: each item is a (key, value) pair, described as a record over std::pair<K, V>; two items with one key are a
/// duplicate.
template <typename K, typename V, typename Compare, typename A>
struct ListTraits<std::map<K, V, Compare, A>> {
	using C = std::map<K, V, Compare, A>;
	using Item = std::pair<K, V>;
	static void for_each(const void* list, FunctionRef<void(const void*)> visit) {
		for (const auto& [key, value] : *static_cast<const C*>(list)) {
			const Item item{key, value};
			visit(&item);
		}
	}
	static void clear(void* list) { static_cast<C*>(list)->clear(); }
	static Appended append(void* list, std::size_t /*index*/, std::size_t /*alternative*/,
	                       FunctionRef<void(void*)> fill) {
		Item item{};
		fill(&item);
		const bool inserted = static_cast<C*>(list)->try_emplace(std::move(item.first), std::move(item.second)).second;
		return inserted ? Appended::YES : Appended::DUPLICATE_ID;
	}
	static constexpr ListOps kOps{.for_each = &for_each, .clear = &clear, .append = &append};
};

/// @brief The index, alternative and emplace functions of a std::variant member type.
template <typename T>
struct VariantTraits;

template <typename... Ts>
struct VariantTraits<std::variant<Ts...>> {
	using V = std::variant<Ts...>;
	static std::size_t index(const void* member) { return static_cast<const V*>(member)->index(); }
	static const void* get(const void* member) {
		return std::visit([](const auto& held) -> const void* { return &held; }, *static_cast<const V*>(member));
	}
	static void* emplace(void* member, std::size_t index) {
		return emplace_at(*static_cast<V*>(member), index, std::index_sequence_for<Ts...>{});
	}

private:
	template <std::size_t... I>
	static void* emplace_at(V& variant, std::size_t index, std::index_sequence<I...> /*indices*/) {
		void* made = nullptr;
		(void)((index == I && (made = &variant.template emplace<I>(), true)) || ...);
		return made;
	}
};

} // namespace detail

/// @brief The ops of an optional member of type @p T (a std::optional).
template <typename T>
inline constexpr const OptionalOps* kOptional = &detail::OptionalTraits<T>::kOps;

/// @brief The ops of a list member of type @p T (a std::vector, std::list, std::array or std::map).
template <typename T>
inline constexpr const ListOps* kList = &detail::ListTraits<T>::kOps;

/// @brief The variant descriptor of a std::variant member.
/// @tparam V The std::variant type.
/// @param alternatives One per alternative of @p V, in its order.
/// @return The descriptor.
template <typename V>
[[nodiscard]] constexpr VariantDesc variant_of(std::span<const Alternative> alternatives) {
	using Traits = detail::VariantTraits<V>;
	return {.alternatives = alternatives, .index = &Traits::index, .get = &Traits::get, .emplace = &Traits::emplace};
}

} // namespace deluge::serialization::schema
