#pragma once

/// @file
/// @brief A gesture's change of an arp setting: a clip's arp (a synth's, a MIDI or CV clip's, a kit's whole-kit arp)
///        or a kit row's.
///
/// An owner in the current song changes through SetSetting, which writes its record and projects the value onto the
/// engine's arp with the follow-ups its menu runs. An owner outside the song (a clip being built) takes the value
/// through the projection alone.

#include "serialization/value.h"

#include <string_view>

class Drum;
class InstrumentClip;

namespace deluge::edits {

/// @brief Change @p clip's arp setting at @p path.
/// @param clip  The clip whose arp it is.
/// @param path  A path of the arp groups' table (`arpeggiator/...`).
/// @param value The new value, in its file form.
void clipArpSettingFor(::InstrumentClip& clip, std::string_view path, serialization::Value value);

/// @brief Change kit row @p drum's arp setting at @p path.
/// @param drum  The kit row's drum.
/// @param path  A path of the arp groups' table (`arpeggiator/...`).
/// @param value The new value, in its file form.
void laneArpSettingFor(::Drum& drum, std::string_view path, serialization::Value value);

} // namespace deluge::edits
