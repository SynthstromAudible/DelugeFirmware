#include "scenes/bind/page_sources.h"

#include "effects/host_chain/pages.h"
#include "instruments/classic/pages.h"
#include "project/fx_chain.h"

#include <algorithm>
#include <array>

namespace deluge::scenes::bind {

std::span<const plugin::RemoteControls> app_publishers() {
	static const std::array<plugin::RemoteControls, 2> kRecords{instruments::classic::kRemoteControls,
	                                                            host_chain::kRemoteControls};
	return kRecords;
}

controls::ChainView chain_view(const project::FxChain* chain, const controls::CompView& comp, bool song,
                               uint32_t section) {
	// A chain that does not resolve has a new chain's view.
	const uint8_t mod_fx_param = chain != nullptr ? chain->mod_fx_param : project::FxChain{}.mod_fx_param;
	const uint8_t filter_type = chain != nullptr ? chain->filter_type : project::FxChain{}.filter_type;
	// The compressor settings' pages follow the reverb page, one each.
	const uint32_t comp_page = 1 + (comp.param % host_chain::kCompPages);
	controls::ChainView out{.current = 0, .group_entry = {0, comp_page}};
	if (section == host_chain::kModFxSection) {
		out.current = mod_fx_param;
	}
	else if (section == host_chain::kFilterSection) {
		out.current = std::min<uint32_t>(filter_type, host_chain::kEqPage);
	}
	else if (section == host_chain::kCompressorSection && song && comp.editing) {
		out.current = comp_page;
	}
	return out;
}

controls::PageSources app_turn_sources(const controls::Board& board) {
	return {.publishers = app_publishers(), .alt_delay = false, .board = board, .chain_view = &chain_view};
}

controls::PageSources app_page_sources(const controls::Facts& facts, const controls::Board& board) {
	controls::PageSources out = app_turn_sources(board);
	out.alt_delay = facts.alt_delay();
	return out;
}

} // namespace deluge::scenes::bind
