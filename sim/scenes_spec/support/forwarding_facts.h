#pragma once

/// @file
/// @brief `ForwardingFacts`: the host's runtime facts with a chosen few answered otherwise, for an example that needs a
///        state the host cannot be put into on demand (a sample load running, a popup up).

#include "scenes/bind/runtime_facts.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace deluge::scenes::spec {

/// @brief The host's facts, with the few a spec needs to say otherwise said otherwise.
class ForwardingFacts final : public deluge::scenes::bind::RuntimeFacts {
public:
	/// @brief What this wrapper answers itself; every other fact is the real host's.
	struct Overrides {
		bool sample_load_in_flight = false; ///< a sample load said to be running
		bool popup_showing = false;         ///< a popup said to be up
	};
	ForwardingFacts(const deluge::scenes::bind::RuntimeFacts& real, Overrides overrides)
	    : real_(real), overrides_(overrides) {}
	[[nodiscard]] bool sample_load_in_flight() const override { return overrides_.sample_load_in_flight; }
	[[nodiscard]] bool popup_showing() const override { return overrides_.popup_showing; }
	[[nodiscard]] std::optional<uint16_t> default_range(deluge::project::SlotAddress slot,
	                                                    uint8_t source) const override {
		return real_.default_range(slot, source);
	}
	[[nodiscard]] std::optional<deluge::scenes::bind::DxCartridgeNames> dx_cartridge() const override {
		return real_.dx_cartridge();
	}
	[[nodiscard]] bool kit_takes_whole_folder(deluge::project::SlotAddress slot) const override {
		return real_.kit_takes_whole_folder(slot);
	}
	[[nodiscard]] bool audio_inputs_present() const override { return real_.audio_inputs_present(); }
	[[nodiscard]] uint8_t loaded_sources(deluge::project::SlotAddress slot) const override {
		return real_.loaded_sources(slot);
	}
	[[nodiscard]] const deluge::instrument::SoundState* sound_state(deluge::project::SlotAddress slot) const override {
		return real_.sound_state(slot);
	}
	[[nodiscard]] const deluge::effects::EffectStates*
	chain_effects(deluge::project::ChainAddress chain) const override {
		return real_.chain_effects(chain);
	}
	[[nodiscard]] int32_t played_bend_range(const deluge::project::SettingAddress& owner,
	                                        int32_t which) const override {
		return real_.played_bend_range(owner, which);
	}
	[[nodiscard]] bool allows_note_tails(deluge::project::SlotAddress slot) const override {
		return real_.allows_note_tails(slot);
	}
	[[nodiscard]] uint8_t arp_preset(const deluge::project::SettingAddress& address) const override {
		return real_.arp_preset(address);
	}
	[[nodiscard]] std::optional<int32_t> param_value(const deluge::project::ParamAddress& address) const override {
		return real_.param_value(address);
	}
	[[nodiscard]] std::optional<int32_t> param_value_at(const deluge::project::ParamAddress& address,
	                                                    uint32_t pos) const override {
		return real_.param_value_at(address, pos);
	}
	[[nodiscard]] deluge::scenes::bind::CableList cables(const deluge::project::ParamAddress& owner) const override {
		return real_.cables(owner);
	}
	[[nodiscard]] std::vector<deluge::scenes::bind::TrackFact> tracks() const override { return real_.tracks(); }
	[[nodiscard]] std::optional<deluge::scenes::bind::AudioClipFact>
	audio_clip(deluge::project::ClipAddress clip) const override {
		return real_.audio_clip(clip);
	}
	[[nodiscard]] bool clip_playing(deluge::project::ClipAddress clip) const override {
		return real_.clip_playing(clip);
	}
	[[nodiscard]] int32_t clip_live_pos(deluge::project::ClipAddress clip) const override {
		return real_.clip_live_pos(clip);
	}
	[[nodiscard]] bool clip_arrangement_only(deluge::project::ClipAddress clip) const override {
		return real_.clip_arrangement_only(clip);
	}
	[[nodiscard]] bool clip_recording_linearly(deluge::project::ClipAddress clip) const override {
		return real_.clip_recording_linearly(clip);
	}
	[[nodiscard]] bool stutter_active() const override { return real_.stutter_active(); }
	[[nodiscard]] std::pair<int64_t, int64_t> audio_clip_samples(deluge::project::ClipAddress clip, int32_t scroll,
	                                                             int32_t zoom) const override {
		return real_.audio_clip_samples(clip, scroll, zoom);
	}

private:
	const deluge::scenes::bind::RuntimeFacts& real_;
	Overrides overrides_;
};

} // namespace deluge::scenes::spec
