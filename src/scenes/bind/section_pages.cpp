#include "scenes/bind/section_pages.h"

#include "effects/global_effectable/pages.h"
#include "instruments/deluge/pages.h"

#include <array>

namespace deluge::scenes::bind {

std::span<const plugin::RemoteControls> app_publishers() {
	static const std::array<plugin::RemoteControls, 2> kRecords{instrument::kRemoteControls,
	                                                            global_effectable::kRemoteControls};
	return kRecords;
}

controls::PageSources app_page_sources(const controls::Facts& facts, const controls::Board& board) {
	return {.publishers = app_publishers(), .alt_delay = facts.alt_delay(), .board = board};
}

} // namespace deluge::scenes::bind
