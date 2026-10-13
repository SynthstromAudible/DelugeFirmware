#include "model/edits/set_chain_view.h"

#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "util/functions.h"

#include <cstring>

namespace deluge::edits {

Error SetChainView::apply(Context& ctx) {
	deluge::project::FxChain* chain = ctx.model.resolveChain(chain_);
	::GlobalEffectable* owner = deluge::bridge::chainOwnerAt(chain_, ctx.song);
	if (chain == nullptr || owner == nullptr || !value_.isScalar()) {
		return Error::UNSPECIFIED;
	}
	const char* word = value_.str().c_str();
	deluge::model::HostChainState next = owner->chainState();
	// A word is taken only when it names a value: each parse falls back on an unknown word, which would not round-trip.
	if (key_ == "modFXCurrentParam") {
		const ModFXParam param = stringToModFXParam(word);
		if (std::strcmp(modFXParamToString(param), word) != 0) {
			return Error::UNSPECIFIED;
		}
		next.mod_fx_param = param;
	}
	else if (key_ == "currentFilterType") {
		const FilterType type = stringToFilterType(word);
		if (std::strcmp(filterTypeToString(type), word) != 0) {
			return Error::UNSPECIFIED;
		}
		next.filter_type = type;
	}
	else {
		return Error::UNSPECIFIED;
	}
	owner->setChainState(next);
	chain->view = deluge::bridge::withLeaf(chain->view, key_, value_);
	return Error::NONE;
}

} // namespace deluge::edits
