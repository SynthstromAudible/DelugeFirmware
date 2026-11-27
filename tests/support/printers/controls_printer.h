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

#include "support/printers/list_printer.h"
#include "support/printers/optional_printer.h"
#include "support/printers/param_id_printer.h"
#include "support/printers/param_target_printer.h"
#include "support/printers/remote_controls_printer.h"
#include "ui/controls/knob_math.h"
#include "ui/controls/page.h"
#include "ui/controls/resolve.h"
#include "ui/controls/section.h"
#include "ui/controls/sink.h"

#include <ostream>
#include <span>
#include <utility>

/// @file
/// @brief Prints the controls layer's values (a page and its slots, a host command, a knob's kind, a write's
///        resolution and a mod button's section result) in a failed CppSpec comparison, alone or as a container
///        element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::ui::controls {

/// @brief Prints a slot field by field, an empty target as `none`, e.g.
///        `{target {space 0, ParamId 12}, kind 1, press 3, press_param none}`.
/// @param os The stream to print to.
/// @param s The slot.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Slot& s) {
	using deluge::test::printers::print_optional;
	os << "{target ";
	print_optional(os, s.target) << ", " << s.press << ", press_param ";
	return print_optional(os, s.press_param) << "}";
}

/// @brief Prints a page field by field, its slots in brackets, e.g.
///        `{section 1, page 0, group 0, slots [{…} …], keeps_stutter 0}`.
/// @param os The stream to print to.
/// @param p The page.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Page& p) {
	os << "{section " << p.section << ", page " << p.page_id << ", group " << +p.group << ", slots ";
	deluge::test::printers::print_list(os, std::span{p.slots});
	return os << ", keeps_stutter " << int{p.keeps_stutter} << "}";
}

/// @brief Prints a host command as its number, e.g. `host_command 2`.
/// @param os The stream to print to.
/// @param command The command.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, HostCommand command) {
	return os << "host_command " << +std::to_underlying(command);
}

/// @brief Prints a knob's kind as its number, e.g. `knob_kind 1`.
/// @param os The stream to print to.
/// @param kind The kind.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, KnobKind kind) {
	return os << "knob_kind " << +std::to_underlying(kind);
}

/// @brief Prints how a write is resolved as its number, e.g. `resolution 1`.
/// @param os The stream to print to.
/// @param resolution The resolution.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, Resolution resolution) {
	return os << "resolution " << +std::to_underlying(resolution);
}

/// @brief Prints a mod button's result flag by flag, e.g. `{change_mode 1, end_stutter 0, toggle_vu_meter 0, taken 1}`.
/// @param os The stream to print to.
/// @param r The result.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SectionResult& r) {
	return os << "{change_mode " << int{r.change_mode} << ", end_stutter " << int{r.end_stutter} << ", toggle_vu_meter "
	          << int{r.toggle_vu_meter} << ", taken " << int{r.taken} << "}";
}

} // namespace deluge::ui::controls
