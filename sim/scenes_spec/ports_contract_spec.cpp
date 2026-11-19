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
		if (p.audio != nullptr) {
			expect(bind::contract::voice_count_is_a_count(*p.audio)).to_be_true();
		}
		if (p.ui != nullptr) {
			expect(bind::contract::ui_facts_are_stable(*p.ui)).to_be_true();
			expect(bind::contract::flash_cursor_is_named(*p.ui)).to_be_true();
		}
	});
});

// clang-format on

DELUGE_SPEC(ports_contract_spec, ports_contract_host)
