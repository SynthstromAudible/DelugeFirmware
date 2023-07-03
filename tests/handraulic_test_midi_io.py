import rtmidi
import usb 

import time
from rtmidi.midiconstants import *

#helper functions to send some midi via command line

note_on = [NOTE_ON, 60, 112]  # channel 1, middle C, velocity 112
note_off = [NOTE_OFF, 60, 0]


note_on = [NOTE_ON, 60, 112]  # channel 1, middle C, velocity 112
note_off = [NOTE_OFF, 60, 0]

def send_note(port):
    out = rtmidi.MidiOut()
    indices = [i for i, s in enumerate(out.get_ports()) if 'Deluge' in s]
    out_port = out.open_port(indices[port-1])
    with out_port:
        print("Sending NoteOn event.")
        out_port.send_message(note_on)
        time.sleep(1)
        print("Sending NoteOff event.")
        out_port.send_message(note_off)
        time.sleep(0.1)

    out.close_port()

def receive_note(port):
    midiin = rtmidi.MidiIn()
    indices = [i for i, s in enumerate(midiin.get_ports()) if 'Deluge' in s]
    midiin.open_port(indices[port-1])
    with midiin:
        try:
            timer = time.time()
            while True:
                msg = in_port.get_message()

                if msg:
                    message, deltatime = msg
                    timer += deltatime
                    print("[%s] @%0.6f %r" % (port, timer, message))

                time.sleep(0.01)
        except KeyboardInterrupt:
            print('')
        finally:
            print("Exit.")
            midiin.close_port()
    midiin.close_port()
