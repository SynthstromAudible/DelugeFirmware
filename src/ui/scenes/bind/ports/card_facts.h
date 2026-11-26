#pragma once

/// @file
/// @brief What a scene reads of the loads the card is running, and of the file last loaded from it.

#include <string>

namespace deluge::ui::scenes::bind {

/// @brief The card's live facts, answered by the host.
class CardFacts {
public:
	virtual ~CardFacts() = default;
	/// @brief Whether an explicit sample load or a folder import is running: either one closes the sample browser to
	///        the presses that would start another.
	[[nodiscard]] virtual bool sample_load_in_flight() const = 0;
	/// @brief Whether a load that can replace an output or a sound's sample is running (an instrument, pattern, song,
	///        drum-randomizer, sample-claim, folder-import or reversion load). A recording does not start over one.
	[[nodiscard]] virtual bool output_may_be_replaced() const = 0;
	/// @brief The file the sample browser last started to preview or load, which a sample browser opens beside when
	///        its holder names no file in SAMPLES/; empty when there is none.
	[[nodiscard]] virtual std::string last_sample_path() const = 0;
};

} // namespace deluge::ui::scenes::bind
