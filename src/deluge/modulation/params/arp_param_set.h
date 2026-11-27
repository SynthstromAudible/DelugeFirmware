#pragma once

/// @file
/// @brief An arp's param set: its sixteen params (`arp::kParams`), in its owner's ParamManager, for any owner.

#include "arp/params.h"
#include "arp/values.h"
#include "modulation/params/param_set.h"

#include <array>

/// @brief The runtime store of one arp's params: a sound's, a kit's, a kit row's, or a MIDI or CV clip's. It sits in
///        its owner's ParamManager as the last main collection, so playback, undo, cloning and stealing carry it as
///        they carry the others. It lives in external memory: every MIDI and CV clip holds one.
class ArpParamSet final : public ParamSet {
public:
	/// @param summary Its slot's summary.
	/// @param owner   Who holds the arp; it decides the starting values (set_to_defaults).
	ArpParamSet(ParamCollectionSummary* summary, deluge::arp::Owner owner);
	ArpParamSet(ArpParamSet const& other);
	Error clone(ParamCollection*& result, bool copy_automation,
	            int32_t reverse_direction_with_length = 0) const override;
	deluge::modulation::params::Kind getParamKind() override { return deluge::modulation::params::Kind::ARP; }

	/// @brief Give every param its owner's starting word (`arp::default_value`).
	void set_to_defaults();

	/// @brief The words the arp plays by now.
	[[nodiscard]] deluge::arp::Values values() const;

	/// @brief The word of @p id now.
	[[nodiscard]] int32_t word(deluge::plugin::ParamId id) const;

	/// @brief Set @p id's base word, as a load or a setting write does (no automation recorded).
	void set_word(deluge::plugin::ParamId id, int32_t word);

	/// @brief Who holds the arp.
	[[nodiscard]] deluge::arp::Owner owner() const { return owner_; }

private:
	deluge::arp::Owner owner_;
	std::array<AutoParam, deluge::arp::kParamCount> params_{};
};
