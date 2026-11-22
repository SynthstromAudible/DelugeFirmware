/// @file
/// @brief The host's MIDI device registry (`io/midi/device_registry.h`): a cable's session handle and back.

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device_manager.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

#include "support/spec_main.h"

using namespace deluge::midi;

namespace {

/// @brief A hosted cable the device manager does not list, so naming it changes no menu another spec draws.
struct SpecCable final : ::MIDICableUSBHosted {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "REGISTRY SPEC"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

} // namespace

// clang-format off
describe device_registry("the host's MIDI device handles", ${
	it("gives each fixed port its own constant, and turns it back into that port", _ {
		expect(id_of(MIDIDeviceManager::dinMIDIPorts).value).to_equal(kDin.value);
		expect(id_of(MIDIDeviceManager::upstreamUSBMIDICable1).value).to_equal(kUpstreamUsb1.value);
		expect(id_of(MIDIDeviceManager::upstreamUSBMIDICable2).value).to_equal(kUpstreamUsb2.value);
		expect(id_of(MIDIDeviceManager::upstreamUSBMIDICable3).value).to_equal(kUpstreamUsb3.value);
		expect(cable_of(kDin)).to_equal(static_cast<::MIDICable*>(&MIDIDeviceManager::dinMIDIPorts));
		expect(cable_of(kUpstreamUsb1)).to_equal(static_cast<::MIDICable*>(&MIDIDeviceManager::upstreamUSBMIDICable1));
		expect(cable_of(kUpstreamUsb2)).to_equal(static_cast<::MIDICable*>(&MIDIDeviceManager::upstreamUSBMIDICable2));
		expect(cable_of(kUpstreamUsb3)).to_equal(static_cast<::MIDICable*>(&MIDIDeviceManager::upstreamUSBMIDICable3));
	});

	it("names no cable for none, nor for a handle it never gave", _ {
		expect(cable_of(DeviceId{})).to_be_null();
		expect(cable_of(DeviceId{999})).to_be_null();
	});

	it("gives a hosted cable one handle, the same each time, and turns it back into that cable", _ {
		static SpecCable cable;
		static SpecCable other;
		const DeviceId first = id_of(cable);
		to_be_at_least(expect(first.value), kFirstHosted.value);
		expect(id_of(cable).value).to_equal(first.value);
		expect(cable_of(first)).to_equal(static_cast<::MIDICable*>(&cable));
		const DeviceId second = id_of(other);
		expect(second.value).not_().to_equal(first.value);
		expect(cable_of(second)).to_equal(static_cast<::MIDICable*>(&other));
	});

	// Last in the file: it names many cables in the session's table, which is this process's alone (one spec file per
	// process).
	it("gives every hosted cable a handle of its own, however many it has named, and keeps the ones it gave", _ {
		// Far more cables than a session meets (6 connected per USB controller, plus the ones the settings file and a
		// song name): none is refused.
		static std::array<SpecCable, 1000> cables;
		const DeviceId kept = id_of(cables[0]);
		expect(static_cast<bool>(kept)).to_be_true();
		size_t refused = 0;
		size_t wrong = 0;
		for (SpecCable& cable : cables) {
			const DeviceId id = id_of(cable);
			refused += id ? 0 : 1;
			wrong += cable_of(id) == static_cast<::MIDICable*>(&cable) ? 0 : 1;
		}
		expect(refused).to_equal(size_t{0});
		expect(wrong).to_equal(size_t{0});
		expect(id_of(cables[0]).value).to_equal(kept.value);
		expect(cable_of(kept)).to_equal(static_cast<::MIDICable*>(&cables[0]));
		expect(cable_of(DeviceId{id_of(cables.back()).value + 1})).to_be_null();
	});
});

// clang-format on

DELUGE_SPEC(device_registry_spec, device_registry)
