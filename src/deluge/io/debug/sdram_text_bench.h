/*
 * Copyright © 2026 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */
#pragma once

// A/B benchmark for executing code from SDRAM (.sdram_text) vs internal SRAM (.text).
// Two byte-identical copies of each kernel are compiled, one per placement, and timed
// with the PMU cycle counter in the same run - cold-cache and warm-cache. Results are
// printed as JSON lines to the sysex debug console on first console attach.
// See docs/dev/sdram_text_prototype.md for protocol and interpretation.
void sdramTextBenchReport();
