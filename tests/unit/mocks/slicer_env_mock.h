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
///
/// FAILURE MODES, for whoever extends this. A collaborator `slicer.cpp` gains that is not stubbed
/// is an undefined-symbol LINK error -- loud, and the reason compiling a firmware UI translation
/// unit into the test binary is affordable. The failure that is NOT loud is a value-returning
/// function stubbed as if it returned void, whose caller then reads garbage; `DELUGE_STUB_RET` in
/// the .cpp exists to prevent that, and every value-returning symbol goes through it. A stub that
/// returns a plausible-but-inert value (`nullptr`, `Error::NONE`) is still only inert: a spec that
/// drives a path THROUGH one has to give it real behaviour, exactly as the collaborators above
/// were given theirs.
namespace deluge::test {

/// When true, every `AudioFileHolder::loadFile()` answers `Error::LOAD_IN_PROGRESS` -- the "sample
/// is not resident and the loader has not finished fetching it" case.
extern bool forceLoadInProgress;

/// Runs ONCE inside the next `AudioFileHolder::loadFile()`, before that call decides its answer,
/// then clears itself.
///
/// The seam for the only interleaving a fully inline slicer drive can actually have. `loadFile()` is
/// the one place `previewForTarget()` can yield -- on the shipping synchronous backing it blocks on
/// the card, and the fiber pump runs -- so a second pad tap, or a BACK press, lands THERE or not at
/// all. Nothing else in the drive gives control up.
extern void (*duringNextLoadFile)();

/// How many times `InstrumentClipView::sendAuditionNote()` has been called.
extern int32_t auditionNoteCount;

/// The `on` argument of the most recent `sendAuditionNote()` call (1 = note-on, 0 = note-off).
extern int32_t lastAuditionOn;

/// How many times `deluge::storage::Owner::run()` has been called.
///
/// The audition's tier is a property, not an implementation detail: with the synchronous loader --
/// the shipping default this binary is built at -- `previewForTarget()`'s `loadFile()` reads the
/// card, and on the Rust BSP that read HOLDS the calling context, so run on the interaction tier it
/// stalls the UI. Counting the dispatch is what lets a spec state that, since `Owner::run()` is
/// inline here and an audition that skipped it would otherwise look identical.
extern int32_t ownerRunCount;

/// Rebuild the fake kit/drum/sample world and zero the counters. Call at the top of every spec.
void resetSlicerEnv();

} // namespace deluge::test
