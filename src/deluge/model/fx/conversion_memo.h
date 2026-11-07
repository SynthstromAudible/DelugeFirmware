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

#include <optional>

namespace deluge::model {

/// @brief One conversion's last input and what it gave: the same input again gives that back without converting.
/// @tparam In The input, comparable.
/// @tparam Out What the conversion gives.
template <typename In, typename Out>
class ConversionMemo {
public:
	/// @brief @p convert of @p in, worked out only when @p in differs from the last call's.
	template <typename Convert>
	const Out& operator()(const In& in, Convert&& convert) {
		if (!in_.has_value() || !(*in_ == in)) {
			out_ = convert(in);
			in_ = in;
		}
		return out_;
	}

private:
	std::optional<In> in_;
	Out out_{};
};

} // namespace deluge::model
