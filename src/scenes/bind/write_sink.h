#pragma once

/// @file
/// @brief Where a menu's writes go: requests a host applies as Edits, so the scenes name no Edit and no engine object.

#include "scenes/bind/target.h"

#include <cstdint>
#include <vector>

namespace deluge::scenes::bind {

/// @brief One menu write: what, where, and the menu value the user chose.
struct WriteRequest {
	Target target;
	Addresses where;
	int32_t menu_value{0};
	/// PARAM only: the stored word the menu value stands for, in the param's display.
	int32_t param_value{0};
	bool operator==(const WriteRequest&) const = default;
};

/// @brief Takes a menu's writes. The app's sink applies each as the target's Edit; a spec's records them.
class WriteSink {
public:
	virtual ~WriteSink() = default;
	/// @brief Apply, or record, @p request.
	/// @return Whether it was taken: false where the app refused it, which left the song as it was.
	virtual bool write(const WriteRequest& request) = 0;
};

/// @brief A sink that records every request, in order.
class RecordingSink final : public WriteSink {
public:
	bool write(const WriteRequest& request) override;
	std::vector<WriteRequest> requests;
};

} // namespace deluge::scenes::bind
