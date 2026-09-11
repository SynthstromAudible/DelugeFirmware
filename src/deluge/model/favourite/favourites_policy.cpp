#include "model/favourite/favourites_policy.h"

#include <utility>

namespace deluge::favourites {

using storage::bytes::SettingsOutcome;

BankState stateAfterLoad(SettingsOutcome outcome) {
	switch (outcome) {
	case SettingsOutcome::Parsed:
	case SettingsOutcome::Absent:
		return BankState::Loaded;
	case SettingsOutcome::Unreadable:
	case SettingsOutcome::Written:
	case SettingsOutcome::WriteFailed:
		return BankState::Unavailable;
	}
	return BankState::Unavailable;
}

bool editAllowed(BankState state) {
	return state == BankState::Loaded;
}

bool loadIsCurrent(uint32_t requested, uint32_t current) {
	return requested == current;
}

void enqueueIntent(std::deque<FavouritesIntent>& waiting, FavouritesIntent intent) {
	if (intent.kind == FavouritesIntent::Kind::Save) {
		for (FavouritesIntent& queued : waiting) {
			if (queued.kind == FavouritesIntent::Kind::Save && queued.path == intent.path) {
				queued.bytes = std::move(intent.bytes);
				return;
			}
		}
		waiting.push_back(std::move(intent));
		return;
	}
	std::erase_if(waiting, [](const FavouritesIntent& queued) { return queued.kind == FavouritesIntent::Kind::Load; });
	waiting.push_back(std::move(intent));
}

} // namespace deluge::favourites
