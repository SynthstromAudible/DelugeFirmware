#include "model/edits/set_chain_view.h"

#include "effects/chain.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"

#include <algorithm>
#include <optional>
#include <span>

namespace deluge::edits {

Error SetChainView::apply(Context& ctx) {
	deluge::project::FxChain* chain = ctx.model.resolveChain(chain_);
	::GlobalEffectable* owner = deluge::bridge::chainOwnerAt(chain_, ctx.song);
	if (chain == nullptr || owner == nullptr || owner->boundChain() != chain || !value_.isScalar()) {
		return Error::UNSPECIFIED;
	}
	// A word is taken only when it names a value.
	const std::string_view word = value_.str();
	const auto index_of = [&](std::span<const std::string_view> words) -> std::optional<size_t> {
		const auto found = std::ranges::find(words, word);
		return found != words.end() ? std::optional{static_cast<size_t>(found - words.begin())} : std::nullopt;
	};
	if (key_ == "modFXCurrentParam") {
		const std::optional<size_t> index = index_of(deluge::effects::kModFxParamWords);
		if (!index) {
			return Error::UNSPECIFIED;
		}
		owner->setView(static_cast<ModFXParam>(*index), owner->filterType());
	}
	else if (key_ == "currentFilterType") {
		const std::optional<size_t> index = index_of(deluge::effects::kFilterTypeWords);
		if (!index) {
			return Error::UNSPECIFIED;
		}
		owner->setView(owner->modFXParam(), static_cast<FilterType>(*index));
	}
	else {
		return Error::UNSPECIFIED;
	}
	return Error::NONE; // the owner plays its record's view, so this wrote the record
}

} // namespace deluge::edits
