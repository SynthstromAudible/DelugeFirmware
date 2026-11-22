#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "controls/sink.h"
#include "instruments/deluge/cables.h"
#include "plugin/types/sync.h"
#include "project/param_address.h"
#include "scenes/bind/target.h"

#include <cstdint>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief One menu write: what, where, and the menu value the user chose.
struct WriteRequest {
	Target target;
	Addresses where;
	int32_t menu_value{0};
	/// PARAM and CABLE: the stored word the menu value stands for (a cable's strength as the engine stores it).
	int32_t param_value{0};
	/// CABLE only: the polarity a new cable takes, or, with @ref polarity_only, the polarity an existing cable is set
	/// to.
	instrument::Polarity polarity{instrument::Polarity::UNIPOLAR};
	/// CABLE only: set an existing cable's polarity and leave its strength.
	bool polarity_only{false};
	bool operator==(const WriteRequest&) const = default;
};

/// @brief Whose effect settings a write names: a sound's slot, or a chain.
using controls::EffectOwner;

/// @brief Takes a menu's writes. The app's sink applies each as the target's Edit; a spec's fake records them.
class WriteSink {
public:
	virtual ~WriteSink() = default;
	/// @brief Apply, or record, @p request.
	/// @return Whether it was taken: false where the app refused it, which left the song as it was.
	virtual bool write(const WriteRequest& request) = 0;
	/// @brief Delete the automation of the param at @p address.
	/// @return Whether it was taken.
	virtual bool delete_automation(const project::ParamAddress& address) = 0;
	/// @brief Write @p text to the TEXT row @p target names (a MODEL target: a clip's, track's or lane's name, a MIDI
	/// track's device definition file) on the owner @p where names.
	/// @return Whether it was taken.
	virtual bool write_text(const Target& target, const Addresses& where, std::string_view text) = 0;
	/// @brief Make @p channel (an index of the input channel words) the input a new audio track starts on: the song's
	/// unsaved default, which the audio-source menu sets to whatever it last picked.
	virtual void set_default_audio_input_channel(int32_t channel) = 0;
	/// @brief Set the song's threshold-recording mode to the menu choice index @p mode (0 is off). Not an undoable
	/// edit: the song holds the mode only at run time, with no saved record, so the host owns it.
	virtual void set_song_threshold_recording(int32_t mode) = 0;
	/// @brief Set the reverb sidechain's sync type to @p type. Not an undoable edit: the engine holds the type only at
	/// run time, with no saved record, so the host owns it.
	virtual void set_reverb_sidechain_sync_type(plugin::SyncType type) = 0;
};

} // namespace deluge::scenes::bind
