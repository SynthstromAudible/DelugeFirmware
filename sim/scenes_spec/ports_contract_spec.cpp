#include "port_contracts.h"
#include "scenes/menu/bind_host.h"

#include "cppspec.hpp"

#include "support/spec_main.h"

using namespace deluge::scenes;

// clang-format off
describe ports_contract_host("the app's ports keep the contract their fakes keep", ${
	it("holds every contract case on the app's ports", _ {
		const bind::Ports& p = menu::host_ports();
		expect(p.audio).not_().to_be_null();
		expect(p.card).not_().to_be_null();
		expect(p.ui).not_().to_be_null();
		expect(p.sound).not_().to_be_null();
		expect(p.clips).not_().to_be_null();
		expect(p.tracks).not_().to_be_null();
		expect(p.controls).not_().to_be_null();
		expect(p.clip_writes).not_().to_be_null();
		expect(p.sound_writes).not_().to_be_null();
		expect(p.learn).not_().to_be_null();
		if (p.audio != nullptr) {
			expect(bind::contract::voice_count_is_a_count(*p.audio)).to_be_true();
		}
		if (p.card != nullptr) {
			expect(bind::contract::card_facts_are_stable(*p.card)).to_be_true();
		}
		if (p.ui != nullptr) {
			expect(bind::contract::ui_facts_are_stable(*p.ui)).to_be_true();
			expect(bind::contract::flash_cursor_is_named(*p.ui)).to_be_true();
		}
		if (p.sound != nullptr) {
			// A slot id no song holds.
			expect(bind::contract::unknown_slot_is_empty(*p.sound, deluge::project::SlotAddress{.id = 0xFFFFFF}))
			    .to_be_true();
		}
		if (p.clips != nullptr) {
			// A clip id the default card's song does not hold.
			expect(bind::contract::unknown_clip_is_silent(*p.clips, deluge::project::ClipAddress{.clip_id = 0xFFFF'FFF0}))
			    .to_be_true();
		}
		if (p.clip_writes != nullptr) {
			// A clip id the default card's song does not hold.
			expect(bind::contract::unknown_clip_is_refused(*p.clip_writes, deluge::project::ClipAddress{.clip_id = 0xFFFF'FFF0}))
			    .to_be_true();
		}
		if (p.sound_writes != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_sound_is_refused(
			           *p.sound_writes,
			           deluge::project::SlotAddress{deluge::project::SlotAddress::Kind::OUTPUT, 0xFFFF'FFF0}))
			    .to_be_true();
			expect(bind::contract::velocity_of_no_device_is_refused(*p.sound_writes)).to_be_true();
		}
		if (p.learn != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_owner_knob_is_refused(
			           *p.learn, deluge::project::OwnerParamTarget{
			                         {deluge::project::ControlsAddress::Kind::OUTPUT, 0xFFFF'FFF0},
			                         deluge::project::engine_target(3)}))
			    .to_be_true();
		}
		if (p.tracks != nullptr) {
			// An output id no song holds.
			expect(bind::contract::unknown_chain_has_no_effects(
			           *p.tracks, deluge::project::ChainAddress{deluge::project::ChainAddress::Kind::OUTPUT, 0xFFFF'FFF0}))
			    .to_be_true();
		}
	});
});

// clang-format on

DELUGE_SPEC(ports_contract_spec, ports_contract_host)
