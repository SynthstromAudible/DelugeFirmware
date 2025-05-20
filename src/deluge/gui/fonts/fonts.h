/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include <stdint.h>

typedef struct {
	uint16_t glyph_index;
	uint8_t w_px;
} lv_font_glyph_dsc_t;

extern const uint8_t font_metric_bold_20px[];
extern const lv_font_glyph_dsc_t font_metric_bold_20px_desc[];

extern const uint8_t font_metric_bold_9px[];
extern const lv_font_glyph_dsc_t font_metric_bold_9px_desc[];

extern const uint8_t font_apple[];
extern const lv_font_glyph_dsc_t font_apple_desc[];

extern const uint8_t font_5px[];
extern const lv_font_glyph_dsc_t font_5px_desc[];

extern const uint8_t font_metric_bold_13px[];
extern const lv_font_glyph_dsc_t font_metric_bold_13px_desc[];
