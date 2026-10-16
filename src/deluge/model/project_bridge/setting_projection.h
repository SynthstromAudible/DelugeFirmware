#pragma once

/// @file
/// @brief The engine side of a setting group: copying a group's record onto the engine object that plays it, and
///        reading that copy back.
///
/// A setting's record (`deluge-project/setting_group.h`) is its truth; the engine objects (the song's reverb and
/// clock, an `Output`, a `Drum`, a `NoteRow`, an `AudioClip`) hold a copy the render reads. `projectSetting` makes the
/// copy equal the record and runs what a change of that setting sets off. The `apply*Setting` functions are that copy
/// and its follow-ups for one owner: the projection calls them with the record's value, and a gesture on an owner
/// outside the current song (being loaded, previewed, built) calls them directly, there being no record to write.

#include "definitions_cxx.hpp" // Error
#include "deluge-project/setting_group.h"
#include "serialization/value.h"

#include <functional>
#include <optional>
#include <string_view>

class ArpeggiatorSettings;
class AudioClip;
class AudioOutput;
class Clip;
class Drum;
class InstrumentClip;
class Kit;
class NoteRow;
class Output;
class Song;

namespace deluge::bridge {

/// @brief Copy @p address's value at @p path from its record onto the engine object, and run that setting's
///        follow-ups.
/// @param song    The song whose records and engine objects the address names.
/// @param address The group and owner.
/// @param path    A path of the group's table.
/// @param previous The record's value before the change, when there was one: what an engine that reads the record
///                 itself held before it (a note row's direction).
/// @return Error::UNSPECIFIED when the record or the engine owner does not resolve.
Error projectSetting(::Song& song, const project::SettingAddress& address, std::string_view path,
                     const serialization::Value* previous = nullptr);

/// @brief The engine copy's value at @p path, in the record's form: what the coherence check holds equal to the record.
/// @param song    The song whose engine objects the address names.
/// @param address The group and owner.
/// @param path    A path of the group's table.
/// @return The value, or nullopt when the engine owner does not resolve or the path has no engine copy.
std::optional<serialization::Value> engineSettingValue(::Song& song, const project::SettingAddress& address,
                                                       std::string_view path);

/// @name Applying one owner's setting to its engine object
/// @{
Error applySongSetting(::Song& song, std::string_view path, const serialization::Value& value);
Error applyTrackSetting(::Output& output, std::string_view path, const serialization::Value& value);
Error applyLaneSetting(::Drum& drum, std::string_view path, const serialization::Value& value);
/// A bound row reads its direction from the record, so a projection passes the direction the record held before
/// (@p previous): the row's setter works out the play position from the direction it is leaving.
Error applyNoteRowSetting(::NoteRow& row, ::InstrumentClip& clip, ::Song& song, std::string_view path,
                          const serialization::Value& value, const serialization::Value* previous = nullptr);
Error applyAudioClipSetting(::AudioClip& clip, ::Song& song, std::string_view path, const serialization::Value& value);
Error applyClipSetting(::Clip& clip, ::Song& song, std::string_view path, const serialization::Value& value);
Error applyAudioInputSetting(::AudioOutput& output, ::Song& song, std::string_view path,
                             const serialization::Value& value);
Error applyClipValueSetting(::InstrumentClip& clip, ::Song& song, std::string_view path,
                            const serialization::Value& value);
/// @}

/// @brief Whose arp an arp setting changes: a clip's (a synth's, a MIDI or CV clip's, or a kit's whole-kit arp), or a
///        kit row's.
struct ArpOwner {
	::InstrumentClip* clip{nullptr}; ///< the clip, for a clip's arp
	::Drum* drum{nullptr};           ///< the kit row, for a kit row's arp
	::Kit* kit{nullptr};             ///< the kit row's kit
};

/// @brief Whether @p owner's arp holds its host values (rate, gate, rhythm, …) as settings: a MIDI or CV clip's, a MIDI
///        or gate kit row's. Elsewhere they are params.
/// @param owner The arp's owner.
/// @return True where they are settings.
[[nodiscard]] bool arpHostValuesAreSettings(const ArpOwner& owner);

/// @brief Copy an arp setting's value onto @p arp, with the follow-ups its menu runs: the preset shown re-derived from
///        the modes, a chosen preset's modes set, the arp restarted when its pattern changes, a new note pattern for
///        the pattern note mode.
/// @param arp   The owner's arp.
/// @param owner Whose arp it is.
/// @param song  The song it plays in.
/// @param path  A path of the arp groups' table.
/// @param value The new value, in its file form.
/// @return Error::UNSPECIFIED for a host value where it is a param, or a value the path does not take.
Error applyArpSetting(::ArpeggiatorSettings& arp, const ArpOwner& owner, ::Song& song, std::string_view path,
                      const serialization::Value& value);

/// @brief Run @p change, which moves the part of @p clip's sample it plays (a marker, the direction), with the clip's
///        voice taken off the sample around it: the clusters the new part needs are claimed (enqueued, never a card
///        read on the UI's time), and a clip that was playing resumes from its play position.
/// @param clip   The audio clip.
/// @param song   The song it plays in.
/// @param change The change, typically an audio clip setting's Edit.
void withVoiceReseated(::AudioClip& clip, ::Song& song, const std::function<void()>& change);

/// @name An engine owner's address in @p song, or nullopt when it is not one of the song's
/// @{
std::optional<project::TrackAddress> trackAddressOf(::Output& output, ::Song& song);
std::optional<project::TrackAddress> trackAddressOf(::AudioOutput& output, ::Song& song);
std::optional<project::LaneAddress> laneAddressOf(::Drum& drum, ::Song& song);
std::optional<project::NoteRowAddress> noteRowAddressOf(::NoteRow& row, ::InstrumentClip& clip, ::Song& song);
std::optional<project::ClipAddress> audioClipAddressOf(::AudioClip& clip, ::Song& song);
std::optional<project::ClipAddress> clipAddressOf(::Clip& clip, ::Song& song);
std::optional<project::ClipAddress> clipAddressOf(::InstrumentClip& clip, ::Song& song);
/// @}

/// @brief Which of a track's two groups holds @p path: its identity (the name) or its instrument device.
/// @param path A path of either group.
/// @return SettingGroupId::TRACK or SettingGroupId::INSTRUMENT_DEVICE.
project::SettingGroupId trackGroupOf(std::string_view path);

} // namespace deluge::bridge
