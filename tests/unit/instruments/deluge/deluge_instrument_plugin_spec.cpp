#include "instruments/deluge/plugin.h"
#include "plugin-api/state_handle.h"

#include <cppspec.hpp>

// clang-format off
describe deluge_instrument_plugin("the Deluge instrument as a plugin", ${
	it("satisfies the engine contract, and its state round-trips through a handle", _ {
		static_assert(deluge::plugin::InstrumentPlugin<deluge::instrument::DelugeInstrument>);
		deluge::instrument::SoundState state = deluge::instrument::new_synth_state();
		state.transpose = 5;
		const auto handle = deluge::plugin::StateHandle::of<deluge::instrument::DelugeInstrument>(state);
		auto read = deluge::plugin::StateHandle::of<deluge::instrument::DelugeInstrument>(
		    deluge::instrument::new_synth_state());
		expect(read.decode(handle.encode({.input_tick_magnitude = 1}), {.input_tick_magnitude = 1}).has_value())
		    .to_be_true();
		// A file read normalises what it reads, so the state's encoding is what survives the round trip.
		expect(read.encode({.input_tick_magnitude = 1}) == handle.encode({.input_tick_magnitude = 1})).to_be_true();
		expect(read.as<deluge::instrument::DelugeInstrument>()->transpose).to_equal(5);
		expect(handle.plugin() == deluge::plugin::kDelugeInstrument).to_be_true();
	});
});
// clang-format on

CPPSPEC_SPEC(deluge_instrument_plugin)
