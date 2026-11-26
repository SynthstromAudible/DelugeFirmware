#include "io/midi/learned_dispatch.h"
#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/views/knob_focus_from_stack.h"
#include "io/midi/follow_host.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device.h"
#include "model/output.h"
#include "model/song/song.h"
#include "ui/controls/learned.h"
#include "util/containers.h"

#include <cstddef>
#include <cstdint>
#include <variant>

namespace deluge::midi {

namespace {

/// @brief The learned knobs one message moves, owner by owner, copied out of the index (a write the dispatch makes can
///        rebuild it) into a list kept from message to message. Each hit is assigned over the one before it in its
///        place, so a message reuses the list's room and its device names' storage, and allocates only where it
///        outgrows them.
class KnobHitsOf final : public ui::controls::HitsOf {
public:
	/// @brief Aim at the knobs a message moves, raw (a knob learned on an MPE zone never matches it).
	/// @param from    The cable it came on.
	/// @param channel Its channel.
	/// @param number  A CC number, or 128 for a pitch bend.
	/// @return This list, aimed.
	KnobHitsOf& aim(MIDICable& from, uint8_t channel, uint8_t number) {
		from_ = &from;
		channel_ = channel;
		number_ = number;
		return *this;
	}
	[[nodiscard]] bool any() override { return mappingIndex().anyKnob(*from_, channel_, number_); }
	[[nodiscard]] ui::controls::KnobHits of(project::ControlsAddress owner) override {
		size_t count = 0;
		mappingIndex().forEachKnobWithSource(
		    owner, *from_, channel_, number_, [&](const IndexedEntry& hit, const project::MidiCommand& source) {
			    const auto* target = std::get_if<project::OwnerParamTarget>(&hit.target);
			    if (target == nullptr) {
				    return;
			    }
			    if (count < hits_.size()) {
				    ui::controls::LearnedHit& kept = hits_[count];
				    kept.target = *target;
				    kept.source = source;
				    kept.relative = hit.relative;
			    }
			    else {
				    hits_.push_back({.target = *target, .source = source, .relative = hit.relative});
			    }
			    count++;
		    });
		return ui::controls::KnobHits{hits_.data(), count};
	}

private:
	MIDICable* from_{nullptr};                           ///< the cable the message came on
	uint8_t channel_{0};                                 ///< its channel, raw
	uint8_t number_{0};                                  ///< its CC number, or 128 for a pitch bend
	deluge::fast_vector<ui::controls::LearnedHit> hits_; ///< the hits, kept from message to message
};

/// @brief The one list of hits, aimed at a message: the dispatch runs on the app loop only.
/// @param from    The cable it came on.
/// @param channel Its channel, raw.
/// @param number  A CC number, or 128 for a pitch bend.
/// @return The list, aimed.
KnobHitsOf& knob_hits(MIDICable& from, uint8_t channel, uint8_t number) {
	static KnobHitsOf hits;
	return hits.aim(from, channel, number);
}

/// @brief 128 stands for pitch bend among a knob's CC numbers.
constexpr uint8_t kPitchBend = 128;

} // namespace

void learnedCcToSong(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value) {
	if (currentSong == nullptr) {
		return;
	}
	ui::controls::dispatch_song_cc(
	    knob_hits(cable, channel, cc), gui::shell_host::control_facts(),
	    *deluge::gui::shell_host::host_ports().controls_sink, takeoverStore(),
	    {.channel = channel, .number = cc, .value = value, .cable_relative = cable.is_relative});
}

void learnedCcToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value) {
	if (currentSong == nullptr || !output.id()) {
		return;
	}
	ui::controls::dispatch_output_cc(
	    *output.id(), knob_hits(cable, channel, cc), gui::shell_host::control_facts(),
	    *deluge::gui::shell_host::host_ports().controls_sink, takeoverStore(),
	    {.channel = channel, .number = cc, .value = value, .cable_relative = cable.is_relative});
}

bool learnedPitchBendToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint16_t value14) {
	if (currentSong == nullptr || !output.id()) {
		return false;
	}
	return ui::controls::dispatch_output_bend(
	    *output.id(), knob_hits(cable, channel, kPitchBend), gui::shell_host::control_facts(),
	    *deluge::gui::shell_host::host_ports().controls_sink,
	    {.channel = channel, .number = kPitchBend, .value = value14, .cable_relative = cable.is_relative});
}

const ui::controls::Facts& followFacts() {
	return gui::shell_host::control_facts();
}

ui::controls::KnobFocus followFocus(Clip& clip) {
	return gui::knobFocusOfClip(clip);
}

ui::controls::Sink& followSink() {
	return *deluge::gui::shell_host::host_ports().controls_sink;
}

} // namespace deluge::midi
