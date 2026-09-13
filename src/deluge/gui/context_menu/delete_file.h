/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/context_menu.h"

namespace deluge::gui::context_menu {
class DeleteFile final : public ContextMenuForSaving {
public:
	DeleteFile() = default;

	std::span<char const*> getOptions() override;
	bool acceptCurrentOption() override;

	char const* getTitle() override;

	/// @name How deletes ended
	/// Counted when an outcome is applied. Read by the path-operations gate.
	/// @{
	/// @brief Deletes that removed the file.
	[[nodiscard]] static int32_t deletesRemoved() { return removed_; }
	/// @brief Deletes that found the file already gone.
	[[nodiscard]] static int32_t deletesMissing() { return missing_; }
	/// @brief Deletes the card refused.
	[[nodiscard]] static int32_t deletesFailed() { return failed_; }
	/// @brief Whether a delete has been handed over and not yet answered.
	[[nodiscard]] static bool deletePending() { return pending_; }
	/// @}

private:
	static inline int32_t removed_ = 0;
	static inline int32_t missing_ = 0;
	static inline int32_t failed_ = 0;
	static inline bool pending_ = false;
};

extern DeleteFile deleteFile;
} // namespace deluge::gui::context_menu
