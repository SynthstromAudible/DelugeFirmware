/*
 * Copyright © 2015-2025 Synthstrom Audible Limited
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

/// Stack-overflow guard: compares the live stack pointer against `program_stack_start` and FREEZEs (E338)
/// if headroom drops below a threshold, logging the closest approach seen. ALPHA/BETA only; a no-op
/// otherwise (and on BSPs that don't describe a stack region, e.g. the host sim). This is a runtime/stack
/// concern, not an allocator one — it lived on the GeneralMemoryAllocator historically; it stands alone now.
void checkStack(char const* caller);
