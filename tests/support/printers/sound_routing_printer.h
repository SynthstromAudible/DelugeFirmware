#pragma once

#include "deluge-project/instrument_slot.h"

#include <ostream>

/// @file
/// @brief The printer for a sound's routing, e.g. `{side_chain_send 0, midi_channel 3, midi_note none}`.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a sound's routing, its channel and note as numbers or `none`.
/// @param os The stream to print to.
/// @param r  The routing.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SoundRouting& r) {
	os << "{side_chain_send " << r.side_chain_send << ", midi_channel ";
	if (r.midi_channel) {
		os << int{*r.midi_channel};
	}
	else {
		os << "none";
	}
	os << ", midi_note ";
	if (r.midi_note) {
		os << int{*r.midi_note};
	}
	else {
		os << "none";
	}
	return os << "}";
}

} // namespace deluge::project
