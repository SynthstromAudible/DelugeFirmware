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
	it("answers a sound's facts as the spec set them, and an unknown slot as empty", _ {
		test::FakeHost host;
		host.sound.loaded[7] = 0b10;
		host.sound.identities[{7, 0, 2}] = 99;
		const Ports p = host.ports();
		expect(p.sound->loaded_sources(deluge::project::SlotAddress{.id = 7})).to_equal(uint8_t{0b10});
		expect(p.sound->range_identity(deluge::project::SlotAddress{.id = 7}, 0, 2).value_or(0)).to_equal(uint64_t{99});
		expect(contract::unknown_slot_is_empty(*p.sound, deluge::project::SlotAddress{.id = 12345})).to_be_true();
	});
	it("answers a clip's play state as the spec set it, and an unknown clip as silent", _ {
		test::FakeHost host;
		host.clips.playing_at[5] = 96;
		const Ports p = host.ports();
		expect(p.clips->clip_playing(deluge::project::ClipAddress{.clip_id = 5})).to_be_true();
		expect(p.clips->clip_live_pos(deluge::project::ClipAddress{.clip_id = 5})).to_equal(96);
		expect(contract::unknown_clip_is_silent(*p.clips, deluge::project::ClipAddress{.clip_id = 54321})).to_be_true();
	});
});

CPPSPEC_SPEC(ports_contract)
