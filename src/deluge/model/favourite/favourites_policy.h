#pragma once

#include "storage/byte_channel.h"

#include <cstdint>
#include <deque>
#include <string>

namespace deluge::favourites {

/// @brief Whether the bank on screen may be recalled from and edited.
enum class BankState : uint8_t {
	Loading,     ///< its read has been asked for and has not answered
	Loaded,      ///< read, or known absent: what is in memory is what the card holds
	Unavailable, ///< the read failed: what is in memory is NOT what the card holds
};

/// @brief One card operation the favourites bank wants done, in the order it was wanted.
struct FavouritesIntent {
	enum class Kind : uint8_t { Load, Save };
	Kind kind = Kind::Load;
	std::string path;
	/// Save only: the WHOLE bank, serialised when the edit was made -- never re-read from live state.
	std::string bytes;
	/// Load only: the bank generation it was asked for. See @ref loadIsCurrent.
	uint32_t generation = 0;
};

/// @brief The state a bank is in once its load has ended.
/// @param outcome How the load ended.
/// @return `Loaded` for a parsed or genuinely absent bank; `Unavailable` for anything else, including a
///         write outcome delivered to a load, which is a contract breach refusing edits cannot make worse.
[[nodiscard]] BankState stateAfterLoad(storage::bytes::SettingsOutcome outcome);

/// @brief Whether a bank in @p state may be edited. Saving a bank that is not `Loaded` would replace the
///        favourites still on the card with whatever happens to be in memory.
[[nodiscard]] bool editAllowed(BankState state);

/// @brief Whether a load's answer still belongs to the bank on screen.
/// @param requested The generation the load was asked for.
/// @param current   The generation now.
[[nodiscard]] bool loadIsCurrent(uint32_t requested, uint32_t current);

/// @brief Queue @p intent behind what is already WAITING. The operation in flight is never in @p waiting.
///
/// A save of a bank that already has a save waiting replaces that save's bytes in place: each carries the
/// whole bank, so only the newest needs to land. A load removes any waiting load and goes to the BACK, so
/// it always reads after every save waiting ahead of it.
void enqueueIntent(std::deque<FavouritesIntent>& waiting, FavouritesIntent intent);

} // namespace deluge::favourites
