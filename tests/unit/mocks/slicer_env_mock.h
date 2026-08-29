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

#include <cstdint>

/// @file
/// The world `gui/ui/slicer.cpp` is compiled against in the unit-test binary.
///
/// The point of this mock is that the REAL `Slicer` translation unit links and runs: the audition
/// specs drive `Slicer::previewForTarget()` and `Slicer::graphicsRoutine()` themselves, not a
/// re-statement of what they are believed to do. (A spec that re-states the caller cannot notice a
/// caller that never re-asks -- which is precisely the hole rung 1's A/B gate had.)
///
/// Only the collaborators the audition path actually touches behave; everything else `slicer.cpp`
/// references is defined just far enough to satisfy the linker.
namespace deluge::test {

/// When true, every `AudioFileHolder::loadFile()` answers `Error::LOAD_IN_PROGRESS` -- the "sample
/// is not resident and the loader has not finished fetching it" case.
extern bool forceLoadInProgress;

/// How many times `InstrumentClipView::sendAuditionNote()` has been called.
extern int32_t auditionNoteCount;

/// The `on` argument of the most recent `sendAuditionNote()` call (1 = note-on, 0 = note-off).
extern int32_t lastAuditionOn;

/// Rebuild the fake kit/drum/sample world and zero the counters. Call at the top of every spec.
void resetSlicerEnv();

} // namespace deluge::test
