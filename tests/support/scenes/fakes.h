#pragma once

/// @file
/// @brief Fakes of the scenes' ports: plain state a spec sets, and every call a sink takes, recorded in order.

#include "scenes/bind/ports/ports.h"

#include <cstdint>

namespace deluge::scenes::bind::test {

/// @brief AudioFacts a spec sets directly.
struct FakeAudioFacts final : AudioFacts {
	bool inputs{false};
	int32_t voices{0};
	[[nodiscard]] bool audio_inputs_present() const override { return inputs; }
	[[nodiscard]] int32_t live_voice_count() const override { return voices; }
};

/// @brief CardFacts a spec sets directly.
struct FakeCardFacts final : CardFacts {
	bool sample_loading{false};
	bool replacing{false};
	[[nodiscard]] bool sample_load_in_flight() const override { return sample_loading; }
	[[nodiscard]] bool output_may_be_replaced() const override { return replacing; }
};

/// @brief UiFacts a spec sets directly.
struct FakeUiFacts final : UiFacts {
	bool auditioning_now{false};
	bool popup{false};
	FlashCursor cursor{FlashCursor::Slow};
	PerformanceEditing performance{PerformanceEditing::Disabled};
	bool dx7_engine{false};
	[[nodiscard]] bool auditioning() const override { return auditioning_now; }
	[[nodiscard]] bool popup_showing() const override { return popup; }
	[[nodiscard]] FlashCursor flash_cursor() const override { return cursor; }
	[[nodiscard]] PerformanceEditing performance_editing_mode() const override { return performance; }
	[[nodiscard]] bool dx7_engine_enabled() const override { return dx7_engine; }
};

/// @brief One of every fake, and the Ports that point at them: a spec's whole host.
struct FakeHost {
	FakeAudioFacts audio;
	FakeCardFacts card;
	FakeUiFacts ui;
	/// @brief Ports pointing at this host's fakes; valid while it lives.
	[[nodiscard]] Ports ports() const { return {.audio = &audio, .card = &card, .ui = &ui}; }
};

} // namespace deluge::scenes::bind::test
