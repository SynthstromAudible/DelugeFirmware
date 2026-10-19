#pragma once

/// @file
/// @brief A target's value, range, choices, names and applicability, from its descriptor, its record and the name
/// table; and the write request for a menu value.

#include "deluge-project/song.h"
#include "gui/l10n/strings.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/target.h"
#include "scenes/bind/write_sink.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::bind {

/// @brief What a menu page shows of one target, evaluated when the page is drawn.
struct View {
	bool applies{false};          ///< the row's conditions hold in the target's context
	int32_t value{0};             ///< the menu value: a CHOICE row's position among its listed entries
	int32_t least{0};             ///< the menu range
	int32_t greatest{0};          ///< the menu range
	std::optional<int32_t> stale; ///< the position of a stored choice that no longer applies, listed but unwritable
};

/// @brief One entry of a CHOICE row's list, in menu order.
struct ChoiceName {
	l10n::String name{l10n::String::EMPTY_STRING};
	bool unavailable{false}; ///< the stored choice, listed although it no longer applies; shown marked
};

/// @brief What the menu shows of @p target.
/// @param target The target.
/// @param model  The song.
/// @param where  The addresses the target resolves against.
/// @param facts  The host's runtime facts.
/// @return The view, or nullopt when the target does not resolve (no slot, or one that is gone).
[[nodiscard]] std::optional<View> view(const Target& target, const project::Song& model, const Addresses& where,
                                       const RuntimeFacts& facts);

/// @brief A target's list entries in menu order. A CHOICE row's: the choices that apply, plus the stored one, marked,
/// when it no longer applies. A BOOL or INTEGER row a menu shows as a list: the name table's name for each menu value
/// of its range; none where the table names no values.
/// @param target The target.
/// @param model  The song.
/// @param where  The addresses the target resolves against.
/// @param facts  The host's runtime facts.
/// @param out    Where the entries go.
/// @return How many entries were written; 0 for a target that does not resolve or has no choices.
size_t choice_names(const Target& target, const project::Song& model, const Addresses& where, const RuntimeFacts& facts,
                    std::span<ChoiceName> out);

/// @brief The write a menu value @p menu asks for: a CHOICE row's position becomes the descriptor's menu value of the
/// choice listed there.
/// @param target The target.
/// @param model  The song.
/// @param where  The addresses the target resolves against.
/// @param facts  The host's runtime facts.
/// @param menu   The menu value the user chose.
/// @return The request, or nullopt for the stale entry, a value outside the range, or a target that does not resolve.
[[nodiscard]] std::optional<WriteRequest> request_for(const Target& target, const project::Song& model,
                                                      const Addresses& where, const RuntimeFacts& facts, int32_t menu);

} // namespace deluge::scenes::bind
