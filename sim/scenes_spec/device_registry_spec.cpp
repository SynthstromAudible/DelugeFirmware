/// @file
/// @brief The host's MIDI device registry (`io/midi/device_registry.h`): a cable's session handle and back.

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device_manager.h"

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
});

// clang-format on

DELUGE_SPEC(device_registry_spec, device_registry)
