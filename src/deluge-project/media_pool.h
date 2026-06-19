/*
 * Copyright © 2024 Synthstrom Audible Limited
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

// Heavyweight model types referenced only by pointer here — forward declarations suffice, so the
// project library does not pull in the output/instrument headers.
class Output;
class Instrument;
class MIDIInstrument;

namespace deluge::project {

/// A song's outputs (the head of the intrusive output list) plus the hibernating-instrument pools.
/// A component of deluge::project::Song.
struct MediaPool {
	Output* firstOutput{nullptr};
	/// All Instruments here have inValidState set to false when added to this list.
	Instrument* firstHibernatingInstrument{nullptr};
	MIDIInstrument* hibernatingMIDIInstrument{nullptr};
};

} // namespace deluge::project
