#pragma once

#include "storage/readback_policy.h"

#include <cstdint>

namespace deluge::storage {

/// @brief What one step of a channel read-back produced.
enum class ReadBackTick : uint8_t {
	Waiting,  ///< Nothing answered this tick. The CALLER applies its own stall bound.
	Advanced, ///< One stage matched and a channel op was served; more remain.
	Passed,   ///< The last stage matched; a channel op was served.
	Failed,   ///< A mismatch, or the channel reported failure. The save must fail.
};

/// @brief One read-back of a written document, driven a tick at a time through the byte channel.
///
/// Holds no stall bound. Both callers already own one, and a second inside this class would give two
/// disagreeing answers to how long is too long.
class ReadBackCheck {
public:
	/// @brief Start a check. `expect.staged` must stay alive until the check ends.
	void begin(const ReadBackExpectation& expect) {
		expect_ = expect;
		stage_ = ReadBackStage::Length;
	}

	/// @brief Advance by at most one channel operation.
	/// @param path The written file.
	[[nodiscard]] ReadBackTick step(const char* path);

	/// @brief The stage the next step will run.
	[[nodiscard]] ReadBackStage stage() const { return stage_; }

private:
	ReadBackExpectation expect_{};
	ReadBackStage stage_{ReadBackStage::Length};
};

/// @brief The same checks, blocking, by reopening the file. Legal ONLY on the storage owner.
/// @param path   The written file.
/// @param expect What it must match.
/// @return true when the length, head and (if any) tail all match.
[[nodiscard]] bool readBackSynchronously(const char* path, const ReadBackExpectation& expect);

} // namespace deluge::storage
