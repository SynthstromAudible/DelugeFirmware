#pragma once

/// @file
/// @brief The voice usage page's sampling clock: where the focus is relative to the page, the counters, and when the
///        next sample is due.

#include "scenes/menu/context.h"
#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace deluge::scenes::menu {

/// @brief `cpu::VoiceUsage`'s counters (`cpu/voice_usage.h:94-111`): the last sample, the maximum, and a whole-number
///        average over every sample since the page opened.
class VoiceUsageSampler {
public:
	/// @brief Forget every sample (`VoiceUsage::beginSession`, `:70-73`).
	void reset() { *this = {}; }
	/// @brief Take one sample of @p live voices (`VoiceUsage::sample`, `:94-99`).
	void sample(int32_t live) {
		live_ = live;
		total_ += static_cast<uint64_t>(std::max<int32_t>(live, 0));
		count_++;
		maximum_ = std::max(maximum_, live);
	}
	[[nodiscard]] int32_t live() const { return live_; }
	[[nodiscard]] int32_t maximum() const { return maximum_; }
	/// @brief `averageVoiceCount` (`:101-103`): 0 before any sample.
	[[nodiscard]] int32_t average() const { return count_ == 0 ? 0 : static_cast<int32_t>(total_ / count_); }
	[[nodiscard]] uint32_t samples() const { return count_; }

private:
	int32_t live_ = 0;
	int32_t maximum_ = 0;
	uint64_t total_ = 0;
	uint32_t count_ = 0;
};

/// @brief The voice usage page's clock (`VoiceUsage::timerCallback`, `cpu/voice_usage.h:77-87`): arriving on the page
///        or a counter begins its session, and while the focus stays there a sample is taken every
///        `cpu::VoiceUsage::kRefreshIntervalMs`.
class VoiceUsageClock {
public:
	/// @brief Where the focus is relative to the voice usage page: its counters sample there.
	enum class Place : uint8_t { None, Page, Counter };

	/// @brief Where @p focused sits relative to the voice usage page.
	static Place place_of(const Node* focused);

	/// @brief Advance by @p dt_ms with the focus where @p st has it.
	/// @param ctx   The menu context; its facts give the live voice count.
	/// @param st    The menu state; its path gives the focus.
	/// @param dt_ms The time since the last tick.
	/// @return True when the open counter should re-read its value (the VERTICAL style's `readValueAgain`).
	bool tick(const MenuContext& ctx, const MenuState& st, uint32_t dt_ms);

	/// @brief The time until the next sample, or nullopt with the focus off the page.
	std::optional<uint32_t> next_timer(const MenuState& st) const;

	/// @brief Forget where the focus was, so the next tick on the page begins a session.
	void leave() { at_ = Place::None; }

	/// @brief The counters the page's columns read.
	const VoiceUsageSampler& sampler() const { return sampler_; }

private:
	VoiceUsageSampler sampler_;
	uint32_t elapsed_ms_ = 0;
	/// Where the last tick found the focus.
	Place at_ = Place::None;
};

} // namespace deluge::scenes::menu
