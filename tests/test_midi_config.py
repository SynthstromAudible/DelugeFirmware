import rtmidi
import usb 
import pytest

@pytest.fixture
def midiin():
    return rtmidi.MidiIn()

@pytest.fixture
def midiout():
    return rtmidi.MidiOut()


def test_midi_input_count(midiin):
    ins = midiin.get_ports()
    assert 3 == sum('Deluge' in port for port in ins)

def test_midi_output_count(midiout):
    outs = midiout.get_ports()
    assert 3 == sum('Deluge' in port for port in outs)