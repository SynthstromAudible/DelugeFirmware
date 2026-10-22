#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "deluge-project/param_address.h"
#include "engines/deluge/cables.h"
#include "scenes/bind/target.h"

#include <cstdint>
#include <vector>

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

/// @brief Takes a menu's writes. The app's sink applies each as the target's Edit; a spec's records them.
class WriteSink {
public:
	virtual ~WriteSink() = default;
	/// @brief Apply, or record, @p request.
	/// @return Whether it was taken: false where the app refused it, which left the song as it was.
	virtual bool write(const WriteRequest& request) = 0;
	/// @brief Write the param at @p address over a region, as a gold knob turns it; no MIDI-follow feedback (the knob
	/// sends its own).
	/// @param address The param's address.
	/// @param value   The new value, as the engine stores it.
	/// @param pos     The region's start; with @p length 0, the value is set at the playhead.
	/// @param length  The region's length, or 0.
	/// @return Whether it was taken.
	virtual bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) = 0;
	/// @brief Delete the automation of the param at @p address.
	/// @return Whether it was taken.
	virtual bool delete_automation(const project::ParamAddress& address) = 0;
};

/// @brief A param write by address, as a RecordingSink keeps it.
struct ParamWrite {
	project::ParamAddress address;
	int32_t value{0};
	int32_t pos{0};
	int32_t length{0};
};

/// @brief A sink that records every request, in order.
class RecordingSink final : public WriteSink {
public:
	bool write(const WriteRequest& request) override;
	bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length) override;
	bool delete_automation(const project::ParamAddress& address) override;
	std::vector<WriteRequest> requests;
	std::vector<ParamWrite> param_writes;
	std::vector<project::ParamAddress> deletes;
};

} // namespace deluge::scenes::bind
