#include "fakes.h"
#include "port_contracts.h"

#include <cppspec.hpp>

using namespace deluge::scenes::bind;

// clang-format off
describe ports_contract("the scenes' ports, as their fakes answer them", ${
	it("answers what the spec set, and the contract holds", _ {
		test::FakeHost host;
		host.audio.inputs = true;
		host.audio.voices = 3;
		host.card.sample_loading = true;
		host.ui.cursor = FlashCursor::Off;
		host.ui.performance = PerformanceEditing::Param;
		const Ports p = host.ports();
		expect(p.audio->audio_inputs_present()).to_be_true();
		expect(p.audio->live_voice_count()).to_equal(3);
		expect(p.card->sample_load_in_flight()).to_be_true();
		expect(p.card->output_may_be_replaced()).to_be_false();
		expect(static_cast<int>(p.ui->flash_cursor())).to_equal(static_cast<int>(FlashCursor::Off));
		expect(static_cast<int>(p.ui->performance_editing_mode())).to_equal(static_cast<int>(PerformanceEditing::Param));
		expect(contract::ui_facts_are_stable(*p.ui)).to_be_true();
		expect(contract::voice_count_is_a_count(*p.audio)).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
		host.card.replacing = true;
		expect(p.card->output_may_be_replaced()).to_be_true();
		expect(contract::card_facts_are_stable(*p.card)).to_be_true();
		expect(contract::flash_cursor_is_named(*p.ui)).to_be_true();
	});
});

CPPSPEC_SPEC(ports_contract)
