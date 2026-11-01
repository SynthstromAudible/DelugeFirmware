/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "definitions_cxx.hpp"
#include "deluge-project/fx_chain.h"
#include "util/misc.h"
#include <algorithm>
#include <cstdint>

namespace deluge::model {

/// @brief Which value a host chain's mod FX knob and which filter its filter knobs edit: in the chain's record while
/// one is bound, else here.
class ChainView {
public:
	/// @return Which value the mod FX knob edits.
	[[nodiscard]] ModFXParam modFXParam() const {
		return bound_ != nullptr ? static_cast<ModFXParam>(std::min<int32_t>(bound_->mod_fx_param, kNumModFXParams - 1))
		                         : param_;
	}

	/// @return Which filter the filter knobs edit.
	[[nodiscard]] FilterType filterType() const {
		return bound_ != nullptr ? static_cast<FilterType>(std::min<int32_t>(bound_->filter_type, kNumFilterTypes - 1))
		                         : type_;
	}

	/// @brief Sets which mod FX value and filter the knobs edit, in the bound record while there is one.
	/// @param param Which mod FX value.
	/// @param type  Which filter.
	void write(ModFXParam param, FilterType type) {
		if (bound_ != nullptr) {
			bound_->mod_fx_param = static_cast<uint8_t>(util::to_underlying(param));
			bound_->filter_type = static_cast<uint8_t>(util::to_underlying(type));
		}
		else {
			param_ = param;
			type_ = type;
		}
	}

	/// @brief Keeps the view in @p chain, which takes the view held now.
	/// @param chain The chain's record, which outlives the binding (unbind() runs before it goes).
	void bind(project::FxChain& chain) {
		chain.mod_fx_param = static_cast<uint8_t>(util::to_underlying(modFXParam()));
		chain.filter_type = static_cast<uint8_t>(util::to_underlying(filterType()));
		bound_ = &chain;
	}

	/// @brief Takes the bound record's view back; nothing when unbound.
	void unbind() {
		if (bound_ == nullptr) {
			return;
		}
		param_ = modFXParam();
		type_ = filterType();
		bound_ = nullptr;
	}

	/// @return The record this view is kept in, or null when it is kept here.
	[[nodiscard]] const project::FxChain* bound() const { return bound_; }

private:
	ModFXParam param_{ModFXParam::FEEDBACK};
	FilterType type_{FilterType::LPF};
	project::FxChain* bound_{nullptr};
};

} // namespace deluge::model
