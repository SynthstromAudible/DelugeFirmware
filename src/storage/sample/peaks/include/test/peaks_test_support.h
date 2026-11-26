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

/// peaks_test_support.h — host test builds only (the crate's `test_support` feature): seeding a fixture's index.
#ifndef DELUGE_PEAKS_TEST_SUPPORT_H
#define DELUGE_PEAKS_TEST_SUPPORT_H

#include "deluge_storage_sample_peaks.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Make every block of `index` complete with `peak`, for a fixture whose sample has no frames behind it.
/// @param index The index; null is ignored.
/// @param peak  The peak every block and the range hold.
void deluge_peaks_fill_uniform(DelugePeakIndex* index, DelugePeak peak);

/// @brief Widen the range by `peak` without completing any block, for a fixture drawn at a scale its peaks do not
///        reach.
/// @param index The index; null is ignored.
/// @param peak  The peak to widen the range by.
void deluge_peaks_widen_range(DelugePeakIndex* index, DelugePeak peak);

#ifdef __cplusplus
}
#endif

#endif // DELUGE_PEAKS_TEST_SUPPORT_H
