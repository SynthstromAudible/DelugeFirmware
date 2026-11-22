#pragma once

/// @file
/// @brief A target's value, range, choices, names and applicability, from its descriptor, its record and the name
/// table; and the write request for a menu value.

#include "gui/l10n/strings.h"
#include "project/song.h"
#include "scenes/bind/ports/ports.h"
#include "scenes/bind/ports/write_sink.h"
#include "scenes/bind/target.h"

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
	/// The setting's short spelling of the choice (`SettingNames::short_choices`), a horizontal column's text; empty
	/// when the setting has none.
	l10n::String short_name{l10n::String::EMPTY_STRING};
	bool unavailable{false}; ///< the stored choice, listed although it no longer applies; shown marked
};

/// @brief The target @p target names at @p where: itself, or for a source-indexed target, the same target on the row
/// whose path is its pattern with Addresses::source_index in place of the `*`.
/// @param target The target.
/// @param where  The addresses the target resolves against.
/// @return The target, or nullopt for a source-indexed target where @p where names no output, whose table has no row
///         at that index, or whose path has no `*`.
[[nodiscard]] std::optional<Target> concrete(const Target& target, const Addresses& where);

/// @brief What the menu shows of @p target.
/// @param target The target.
/// @param model  The song; null reads DEVICE targets alone, which no song holds.
/// @param where  The addresses the target resolves against.
/// @param ports  The host's ports: the audio (plugged inputs), sound (loaded sources, typed state), track (effects) and
///               controls (live param values) facts, which must all be set for a song's target; a DEVICE target reads
///               the device port alone, and resolves to nothing where it is null.
/// @return The view, or nullopt when the target does not resolve (no slot, or one that is gone).
[[nodiscard]] std::optional<View> view(const Target& target, const project::Song* model, const Addresses& where,
                                       const Ports& ports);

/// @brief A target's list entries in menu order. A CHOICE row's: the choices that apply, plus the stored one, marked,
/// when it no longer applies. A BOOL or INTEGER row a menu shows as a list: the name table's name for each menu value
/// of its range; none where the table names no values.
/// @param target The target.
/// @param model  The song; null reads DEVICE targets alone, which no song holds.
/// @param where  The addresses the target resolves against.
/// @param ports  The host's ports: the audio (plugged inputs), sound (loaded sources, typed state), track (effects) and
///               controls (live param values) facts, which must all be set for a song's target; a DEVICE target reads
///               the device port alone, and resolves to nothing where it is null.
/// @param out    Where the entries go.
/// @return How many entries were written; 0 for a target that does not resolve or has no choices.
size_t choice_names(const Target& target, const project::Song* model, const Addresses& where, const Ports& ports,
                    std::span<ChoiceName> out);

/// @brief The write a menu value @p menu asks for: a CHOICE row's position becomes the descriptor's menu value of the
/// choice listed there.
/// @param target The target.
/// @param model  The song; null reads DEVICE targets alone, which no song holds.
/// @param where  The addresses the target resolves against.
/// @param ports  The host's ports: the audio (plugged inputs), sound (loaded sources, typed state), track (effects) and
///               controls (live param values) facts, which must all be set for a song's target; a DEVICE target reads
///               the device port alone, and resolves to nothing where it is null.
/// @param menu   The menu value the user chose.
/// @return The request, or nullopt for the stale entry, a value outside the range, or a target that does not resolve.
[[nodiscard]] std::optional<WriteRequest> request_for(const Target& target, const project::Song* model,
                                                      const Addresses& where, const Ports& ports, int32_t menu);

} // namespace deluge::scenes::bind
