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

class Source;

/// @brief What an oscillator-type change did to a source's ranges, as `Source::setOscType` reports it.
struct RangesRetype {
	bool reallocated = false; ///< The ranges were rebuilt as the new type's ranges.
	bool cleared = false;     ///< The rebuild had to empty them first: range 0 is the only one left.
};

/// @brief What `Source::setOscType` does to one source's ranges while this is alive. A claim or an import watches the
///        type change it makes through the source's setting Edit, which reports nothing back. One at a time, on the
///        interaction tier.
class RangesRetypeWatch {
public:
	explicit RangesRetypeWatch(const Source& source);
	~RangesRetypeWatch();
	RangesRetypeWatch(const RangesRetypeWatch&) = delete;
	RangesRetypeWatch& operator=(const RangesRetypeWatch&) = delete;

	/// @return What happened to the watched source's ranges so far.
	[[nodiscard]] RangesRetype result() const { return result_; }

	/// @brief `Source::setOscType`'s report: @p source's ranges were rebuilt, emptied first when @p cleared.
	static void rangesRetyped(const Source& source, bool cleared);

private:
	const Source* source_;
	RangesRetype result_{};
};
