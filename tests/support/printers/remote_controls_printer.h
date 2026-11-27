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

#include "plugin/types/param_kind.h"
#include "plugin/types/remote_controls.h"
#include "support/printers/list_printer.h"
#include "support/printers/optional_printer.h"
#include "support/printers/param_id_printer.h"

#include <ostream>
#include <span>
#include <utility>

/// @file
/// @brief Prints a remote-controls page, its slots, a slot's press action and its param's kind in a failed CppSpec
///        comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::plugin {

/// @brief Prints a press action as its number, e.g. `press 3`.
/// @param os The stream to print to.
/// @param action The press action.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, PressAction action) {
	return os << "press " << +std::to_underlying(action);
}

/// @brief Prints a param's kind as its number, e.g. `kind 1`.
/// @param os The stream to print to.
/// @param kind The kind.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, ParamKind kind) {
	return os << "kind " << std::to_underlying(kind);
}

/// @brief Prints a slot field by field, an empty param as `none`, e.g.
///        `{param ParamId 12, kind 1, press 3, press_param none}`.
/// @param os The stream to print to.
/// @param s The slot.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RemoteSlot& s) {
	using deluge::test::printers::print_optional;
	os << "{param ";
	print_optional(os, s.param) << ", " << s.press << ", press_param ";
	return print_optional(os, s.press_param) << "}";
}

/// @brief Prints a page field by field, its slots in brackets, e.g.
///        `{section 1, page 0, group 0, name "lpf", slots [{…} …], for_preset 0, keeps_stutter 0}`.
/// @param os The stream to print to.
/// @param p The page.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RemotePage& p) {
	os << "{section " << p.section << ", page " << p.page_id << ", group " << +p.group << ", name \"" << p.name
	   << "\", slots ";
	deluge::test::printers::print_list(os, std::span{p.slots});
	return os << ", for_preset " << int{p.for_preset} << ", keeps_stutter " << int{p.keeps_stutter} << "}";
}

} // namespace deluge::plugin
