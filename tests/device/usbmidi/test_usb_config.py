"""The Deluge's USB identity, read from this machine's USB bus. Run with a Deluge plugged in, from
tests/device/usbmidi: `python3 -m pytest test_usb_config.py test_midi_config.py` (or `./dbt test usbmidi`).

Gate usbmidi:
    a Deluge on this machine's USB enumerates as 16d0:0ce2 at high speed with one configuration whose interface is a
    USB-MIDI one with two endpoints, and the host sees three "Deluge" MIDI ports each way (test_midi_config.py)
"""

import pytest
import rtmidi
import usb


@pytest.fixture
def deluge_dev():
    dev = usb.core.find(idVendor=0x16D0, idProduct=0x0CE2)
    return dev


@pytest.fixture
def deluge_dev_cfg(deluge_dev):
    return deluge_dev.configurations()[0]


@pytest.fixture
def deluge_midi_if(deluge_dev_cfg):
    return deluge_dev_cfg.interfaces()[0]


def test_deluge_dev_exists(deluge_dev):
    assert deluge_dev is not None


def test_deluge_usb_device(deluge_dev):
    assert deluge_dev.bDeviceClass == 0
    assert deluge_dev.bNumConfigurations == 1
    assert deluge_dev.speed == 3  # high speed


def test_deluge_usb_midi_config(deluge_dev_cfg):
    # it has one interface
    assert deluge_dev_cfg.bNumInterfaces == 1


def test_deluge_midi_interface(deluge_midi_if):
    # it is an audio device
    assert deluge_midi_if.bInterfaceClass == 1

    # it is the midi subclass
    assert deluge_midi_if.bInterfaceSubClass == 3

    assert deluge_midi_if.bNumEndpoints == 2


midiin = rtmidi.MidiIn()
midiout = rtmidi.MidiOut()

print(midiin.get_ports())
