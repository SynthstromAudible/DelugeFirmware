#include "instruments/deluge/deluge_synth.h"
#include "plugin-api/state_handle.h"
#include "support/printers/plugin_id_printer.h"
#include "support/printers/value_printer.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <utility>

// clang-format off
describe deluge_instrument_plugin("the Deluge instrument as a plugin", ${
	it("satisfies the engine contract, and its state round-trips through a handle", _ {
		static_assert(deluge::plugin::InstrumentPlugin<deluge::instrument::DelugeSynth>);
		deluge::instrument::SoundState state;
		deluge::test::set(state, "transpose", "5");
		const auto handle = deluge::plugin::StateHandle::of<deluge::instrument::DelugeSynth>(std::move(state));
		auto read = deluge::plugin::StateHandle::of<deluge::instrument::DelugeSynth>(deluge::instrument::SoundState{});
		expect(read.decode(handle.encode({.input_tick_magnitude = 1}), {.input_tick_magnitude = 1}).has_value())
		    .to_be_true();
		// A file read normalises what it reads, so the state's encoding is what survives the round trip.
		expect(read.encode({.input_tick_magnitude = 1})).to_equal(handle.encode({.input_tick_magnitude = 1}));
		expect(deluge::instrument::voicing(*read.as<deluge::instrument::DelugeSynth>()).master.transpose).to_equal(5);
		expect(handle.plugin()).to_equal(deluge::plugin::kDelugeInstrument);
	});
});
// clang-format on

CPPSPEC_SPEC(deluge_instrument_plugin)
