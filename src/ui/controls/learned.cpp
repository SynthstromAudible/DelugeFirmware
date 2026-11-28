/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "ui/controls/learned.h"

#include "plugin/types/plugin_id.h"
#include "ui/controls/knob_math.h"

namespace deluge::ui::controls {

namespace {

using project::ControlsAddress;
using project::OwnerId;
using project::ParamAddress;

/// A CC value from 16 to 111 is no relative controller's: those send small offsets around 0 and 128.
constexpr uint16_t kFirstAbsolute = 16;
constexpr uint16_t kLastAbsolute = 111;
/// A pitch bend's 14-bit value onto a param's word, centred on 0.
constexpr uint32_t kBendShift = 18;
constexpr uint32_t kBendCentre = 0x8000'0000;

/// One owner a message is offered to: whose knobs, and where their params are.
struct Owner {
	ControlsAddress knobs;   ///< whose knobs the hits are
	OwnerId param_owner;     ///< the owner its params' addresses name
	plugin::PluginId plugin; ///< whose descriptor rows its params have
};

/// What a dispatch keeps of one output while it offers it a message: the clip its writes go to, which a hit's take
/// moves.
struct Playing {
	uint32_t output_id;
	uint32_t clip_id; ///< the active clip, or its take once a hit made it

	/// The step region held on the clip, read before a hit's take moves it; then the take made (once a hit asks).
	std::optional<StepRegion> region_then_take(const Facts& facts, Sink& sink) {
		const std::optional<StepRegion> region = facts.step_region(clip_id);
		if (const std::optional<uint32_t> take = sink.take_for(output_id)) {
			clip_id = *take;
		}
		return region;
	}
};

/// Whether a knob over @p param turns a param of its owner: an engine param or the owner's arp's.
bool turns_param(const project::ParamTarget& param) {
	return param.space == project::ParamSpace::ENGINE || param.space == project::ParamSpace::ARP;
}

/// The plugin whose descriptor rows a playing output's own params have: a synth's are a sound's, a kit's and an audio
/// track's are their effects'.
plugin::PluginId plugin_of(PlayingOutput::Kind kind) {
	return kind == PlayingOutput::Kind::SYNTH ? plugin::kDelugeInstrument : plugin::kGlobalEffectable;
}

/// Move the param of one learned knob @p hit as the CC @p message says. @p playing is the output (none for the song).
void move_knob(const LearnedHit& hit, const Owner& owner, Playing* playing, const Facts& facts, Sink& sink,
               TakeoverStore& takeovers, const LearnedMessage& message) {
	// A knob turns only its owner's engine and arp params: one over any other asks for no take.
	if (!turns_param(hit.target.param)) {
		return;
	}
	bool relative = hit.relative;
	if (relative && message.value >= kFirstAbsolute && message.value <= kLastAbsolute) {
		relative = false;
		sink.knob_absolute(hit.target, hit.source);
	}
	const std::optional<StepRegion> region =
	    playing != nullptr ? playing->region_then_take(facts, sink) : facts.step_region(std::nullopt);
	const std::optional<ParamAddress> learned = resolve_learned(
	    hit.target, owner.param_owner, playing != nullptr ? std::optional{playing->clip_id} : std::nullopt);
	const std::optional<ParamAddress> address = learned ? facts.playing_address(*learned) : std::nullopt;
	if (!address) {
		return;
	}
	std::optional<int32_t> value = value_for_move(facts, *address, region);
	if (!value) {
		if (!sink.make_param(*address)) {
			return;
		}
		value = value_for_move(facts, *address, region);
	}
	const CcMove move{.address = *address,
	                  .info = facts.param_info(owner.plugin, hit.target.param),
	                  .value = value.value_or(0),
	                  .region = region,
	                  .cc_value = static_cast<uint8_t>(message.value),
	                  .cable_relative = message.cable_relative,
	                  .knob_relative = relative};
	if (const std::optional<int32_t> pos = move_by_cc(move, takeovers.at(hit.target, hit.source), facts, sink)) {
		sink.learned_written(*address, *pos);
	}
}

/// Move the param of one learned bend knob @p hit as @p message says.
/// @return Whether its param was written.
bool move_bend(const LearnedHit& hit, const Owner& owner, Playing& playing, const Facts& facts, Sink& sink,
               const LearnedMessage& message) {
	// A knob turns only its owner's engine and arp params: one over any other asks for no take.
	if (!turns_param(hit.target.param)) {
		return false;
	}
	const std::optional<StepRegion> region = playing.region_then_take(facts, sink);
	const std::optional<ParamAddress> learned = resolve_learned(hit.target, owner.param_owner, playing.clip_id);
	const std::optional<ParamAddress> address = learned ? facts.playing_address(*learned) : std::nullopt;
	if (!address) {
		return false;
	}
	const auto word = static_cast<int32_t>((uint32_t{message.value} << kBendShift) - kBendCentre);
	const auto [pos, length] = region.value_or(StepRegion{0, 0});
	return sink.write_param(*address, word, pos, length, Resolution::LEARNED);
}

/// Offer @p message to @p output's owners in order: its own knobs, then, for a kit, each sound row's on the clip its
/// writes go to by then. @p offer is called with each owner and its hits.
template <typename Offer>
void offer_output(const PlayingOutput& output, HitsOf& hits, const Facts& facts, const Playing& playing,
                  const Offer& offer) {
	offer(Owner{.knobs = output.owner, .param_owner = output.param_owner, .plugin = plugin_of(output.kind)},
	      hits.of(output.owner));
	if (output.kind != PlayingOutput::Kind::KIT) {
		return;
	}
	for (const KitRow& row : facts.sound_rows(playing.clip_id)) {
		const ControlsAddress knobs{.kind = ControlsAddress::Kind::DRUM, .id = row.drum_id};
		offer(Owner{.knobs = knobs, .param_owner = row.param_owner, .plugin = plugin::kDelugeInstrument},
		      hits.of(knobs));
	}
}

/// The output @p output_id among the playing ones, with knobs of its own: a MIDI or CV track has none. The list is
/// read again for each output a message is offered to, as an output offered before it may have moved onto its take.
std::optional<PlayingOutput> learnable(uint32_t output_id, const Facts& facts) {
	for (const PlayingOutput& output : facts.playing_outputs()) {
		if (output.owner.id == output_id) {
			return output.kind != PlayingOutput::Kind::OTHER ? std::optional{output} : std::nullopt;
		}
	}
	return std::nullopt;
}

} // namespace

std::optional<int32_t> value_for_move(const Facts& facts, const ParamAddress& address,
                                      const std::optional<StepRegion>& region) {
	return region ? facts.param_value_at(address, static_cast<uint32_t>(region->first)) : facts.param_value(address);
}

std::optional<int32_t> move_by_cc(const CcMove& move, TakeoverState& state, const Facts& facts, Sink& sink) {
	const int32_t knob_pos = knob_pos_of(move.address, move.info, move.value);
	TakeoverInputs inputs = facts.takeover_inputs(move.cable_relative);
	inputs.knob_relative = move.knob_relative;
	inputs.step_editing = move.region.has_value();
	const int32_t new_pos = knob_pos_after(knob_pos, move.cc_value, inputs, state);
	if (new_pos == knob_pos) {
		return std::nullopt;
	}
	const auto [pos, length] = move.region.value_or(StepRegion{0, 0});
	(void)sink.write_param(move.address, value_of_knob(move.address, move.info, new_pos), pos, length,
	                       Resolution::LEARNED);
	return new_pos;
}

std::optional<ParamAddress> resolve_learned(const project::OwnerParamTarget& target, OwnerId param_owner,
                                            std::optional<uint32_t> clip_id) {
	if (!turns_param(target.param)) {
		return std::nullopt;
	}
	switch (param_owner.kind) {
	case OwnerId::Kind::SONG:
		return ParamAddress::of(OwnerId::song(), target.param);
	case OwnerId::Kind::CLIP:
		if (!clip_id) {
			return std::nullopt;
		}
		return ParamAddress::of(OwnerId::clip(*clip_id), target.param);
	case OwnerId::Kind::INSTRUMENT:
	case OwnerId::Kind::DRUM:
	case OwnerId::Kind::NOTE_ROW:
		break;
	}
	return ParamAddress::of(param_owner, target.param, clip_id);
}

void dispatch_song_cc(HitsOf& hits, const Facts& facts, Sink& sink, TakeoverStore& takeovers,
                      const LearnedMessage& message) {
	const ControlsAddress song{.kind = ControlsAddress::Kind::SONG, .id = 0};
	const Owner song_owner{.knobs = song, .param_owner = OwnerId::song(), .plugin = plugin::kGlobalEffectable};
	for (const LearnedHit& hit : hits.of(song)) {
		move_knob(hit, song_owner, nullptr, facts, sink, takeovers, message);
	}
}

void dispatch_output_cc(uint32_t output_id, HitsOf& hits, const Facts& facts, Sink& sink, TakeoverStore& takeovers,
                        const LearnedMessage& message) {
	if (!hits.any()) {
		return;
	}
	const std::optional<PlayingOutput> output = learnable(output_id, facts);
	if (!output) {
		return;
	}
	Playing playing{.output_id = output_id, .clip_id = output->clip_id};
	offer_output(*output, hits, facts, playing, [&](const Owner& owner, KnobHits owner_hits) {
		for (const LearnedHit& hit : owner_hits) {
			move_knob(hit, owner, &playing, facts, sink, takeovers, message);
		}
	});
}

bool dispatch_output_bend(uint32_t output_id, HitsOf& hits, const Facts& facts, Sink& sink,
                          const LearnedMessage& message) {
	if (!hits.any()) {
		return false;
	}
	const std::optional<PlayingOutput> output = learnable(output_id, facts);
	if (!output) {
		return false;
	}
	Playing playing{.output_id = output_id, .clip_id = output->clip_id};
	bool heard = false;
	offer_output(*output, hits, facts, playing, [&](const Owner& owner, KnobHits owner_hits) {
		heard = heard || !owner_hits.empty();
		for (const LearnedHit& hit : owner_hits) {
			if (move_bend(hit, owner, playing, facts, sink, message)) {
				break;
			}
		}
	});
	return heard;
}

} // namespace deluge::ui::controls
