#pragma once

/// @file
/// @brief A gesture's change of an arp setting: a clip's arp (a synth's, a MIDI or CV clip's, a kit's whole-kit arp)
///        or a kit row's.
///
/// An owner in the current song changes through SetSetting, which writes its record and projects the value onto the
/// engine's arp with the follow-ups its menu runs. An owner outside the song (a clip being built) takes the value
/// through the projection alone.

#include "serialization/value.h"

#include <cstdint>
#include <string_view>

class ArpeggiatorSettings;
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

/// @brief Change the arp setting at @p path of whichever owner in the current song holds @p arp: a clip's, or a kit
///        row's. An editor that holds the arp it shows (the sound editor) names it this way.
/// @param arp   The arp: some clip's `arpSettings` or some kit row's.
/// @param path  A path of the arp groups' table (`arpeggiator/...`).
/// @param value The new value, in its file form.
void arpSettingFor(::ArpeggiatorSettings& arp, std::string_view path, serialization::Value value);

/// @brief Choice @p index of arp row @p path, in its file form: what a menu listing the row's choices in order writes.
/// @param path  A CHOICE row of the arp table.
/// @param index The choice's index.
/// @return The choice's word; the first choice for an index out of range.
[[nodiscard]] serialization::Value arpChoice(std::string_view path, int32_t index);

} // namespace deluge::edits
