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

#include "io/midi/cable_types/din.h"
#include "io/midi/midi_root_complex.h"
#include <array>

class DINRootComplex : public MIDIRootComplex {
private:
public:
	/// The one and only DIN cable connection
	MIDICableDINPorts cable;

	DINRootComplex();
	~DINRootComplex() override;

	[[nodiscard]] RootComplexType getType() const override { return RootComplexType::RC_DIN; };

	[[nodiscard]] size_t getNumCables() const override { return 1; }
	[[nodiscard]] MIDICable* getCable(size_t cableIdx) override { return (cableIdx == 0) ? &cable : nullptr; }

	void flush() override;
	[[nodiscard]] Error poll() override;
};
