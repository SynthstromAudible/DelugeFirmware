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

#include "definitions_cxx.hpp"
#include "io/midi/midi_device.h"

enum class RootComplexType {
	RC_DIN,
	RC_USB_PERIPHERAL,
	/// needs the RC_ prefix even though this is an enum class because USB_HOST is a preprocessor macro in some of the
	/// RZA1 headers
	RC_USB_HOST,
};

/// Represents a group of cables we can do I/O on.
///
/// The name is meant to be analogous to a "root complex" in a PCIe device hierarchy, which represents the physical
/// bridge to the PCIe bus by the bus controller. Similarly, this class represents the interface from the Deluge
/// software to a MIDI device connection.
class MIDIRootComplex {
	class CableIterator {
	private:
		MIDIRootComplex& parent_;
		size_t index_;

	public:
		CableIterator(const CableIterator&) = default;
		CableIterator(CableIterator&&) = default;
		CableIterator& operator=(const CableIterator&) = delete;
		CableIterator& operator=(CableIterator&&) = delete;

		CableIterator(MIDIRootComplex& parent, size_t index) : parent_{parent}, index_{index} {}

		CableIterator& operator++() {
			index_++;
			return *this;
		}

		CableIterator operator++(int) {
			auto pre = *this;
			index_++;
			return pre;
		}

		friend bool operator==(CableIterator const& a, CableIterator const& b) {
			return &a.parent_ == &b.parent_ && a.index_ == b.index_;
		};

		friend bool operator!=(CableIterator const& a, CableIterator const& b) {
			return &a.parent_ != &b.parent_ || a.index_ != b.index_;
		};

		MIDICable& operator*() { return *parent_.getCable(index_); }
		MIDICable* operator->() { return parent_.getCable(index_); }
	};

	class CableSet {
	public:
		MIDIRootComplex& parent;

		explicit CableSet(MIDIRootComplex& ccplex) : parent{ccplex} {}

		CableIterator begin() { return CableIterator{parent, 0}; }
		CableIterator end() { return CableIterator{parent, parent.getNumCables()}; }
	};

public:
	MIDIRootComplex() = default;
	MIDIRootComplex(const MIDIRootComplex&) = delete;
	MIDIRootComplex(MIDIRootComplex&&) = delete;
	MIDIRootComplex& operator=(const MIDIRootComplex&) = delete;
	MIDIRootComplex& operator=(MIDIRootComplex&&) = delete;

	virtual ~MIDIRootComplex() = default;

	[[nodiscard]] virtual RootComplexType getType() const = 0;

	[[nodiscard]] virtual size_t getNumCables() const = 0;
	[[nodiscard]] virtual MIDICable* getCable(size_t cableIdx) = 0;

	CableSet getCables() { return CableSet{*this}; }

	/// Flush as much data as possible from any internal buffers to hardware queues
	virtual void flush() = 0;

	/// Poll the root complex, calling back in to the MIDI engine for any new messages.
	[[nodiscard]] virtual Error poll() = 0;
};
