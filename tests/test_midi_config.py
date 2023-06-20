import rtmidi
import usb 
import pytest
import time
from rtmidi.midiconstants import *

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


inputs = rtmidi.MidiIn()
outputs = rtmidi.MidiOut()

note_on = [NOTE_ON, 60, 112]  # channel 1, middle C, velocity 112
note_off = [NOTE_OFF, 60, 0]


note_on = [NOTE_ON, 60, 112]  # channel 1, middle C, velocity 112
note_off = [NOTE_OFF, 60, 0]

def send_note(port):
    midiout, port_name = rtmidi.open_midioutput(name=f'Deluge MIDI {port}')
    with midiout:
        print("Sending NoteOn event.")
        midiout.send_message(note_on)
        time.sleep(1)
        print("Sending NoteOff event.")
        midiout.send_message(note_off)
        time.sleep(0.1)

    del midiout

def receive_note(port):
    midin, port_name = rtmidi.open_midiinput(name=f'Deluge MIDI {port}')
    with midiin:
        try:
            timer = time.time()
            while True:
                msg = midiin.get_message()

                if msg:
                    message, deltatime = msg
                    timer += deltatime
                    print("[%s] @%0.6f %r" % (port_name, timer, message))

                time.sleep(0.01)
        except KeyboardInterrupt:
            print('')
        finally:
            print("Exit.")
            midiin.close_port()
    del midiin
