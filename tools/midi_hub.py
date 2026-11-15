#!/usr/bin/env python3
"""MIDI hub for the FM-1 (POC): every USB MIDI controller plugged into the computer plays the FM-1.

Two USB devices on a hub cannot talk to each other: a host has to pass the messages on. This script is that host
(the same idea as a phone app later). It forwards every other MIDI input to the FM-1 ("Felucca"), as is, and
translates an Akai MIDI Mix (factory mapping, all on channel 1) into the FM-1's standard CC map, strip n -> track n
(MIDI channel n):

  fader        -> CC7  LEVEL           knob row 1 -> CC91 REV send
  knob row 2   -> CC94 DLY send        knob row 3 -> CC10 PAN
  MUTE button  -> the track's LEVEL to 0 and back (its LED lit while muted)

Controllers plugged in or out while it runs are picked up (once a second).
    pip install mido python-rtmidi
    python3 tools/midi_hub.py [--fm1 NAME] [-v]
"""
import argparse
import time

import mido

MIX_KNOBS = ([16, 20, 24, 28, 46, 50, 54, 58],     # row 1
             [17, 21, 25, 29, 47, 51, 55, 59],     # row 2
             [18, 22, 26, 30, 48, 52, 56, 60])     # row 3
MIX_FADERS = [19, 23, 27, 31, 49, 53, 57, 61]
MIX_MUTES = [1, 4, 7, 10, 13, 16, 19, 22]           # notes (their LEDs: the same notes back)
ROW_CC = (91, 94, 10)


class MidiMix:
    def __init__(self, out_name):
        self.level = [100] * 8                      # each strip's fader (until it moves: the FM-1's default-ish)
        self.muted = [False] * 8
        self.led = mido.open_output(out_name) if out_name else None

    def translate(self, msg):
        """the FM-1 messages for one MIDI Mix message"""
        if msg.type == "control_change":
            for row, ccs in enumerate(MIX_KNOBS):
                if msg.control in ccs:
                    return [mido.Message("control_change", channel=ccs.index(msg.control),
                                         control=ROW_CC[row], value=msg.value)]
            if msg.control in MIX_FADERS:
                n = MIX_FADERS.index(msg.control)
                self.level[n] = msg.value
                return [] if self.muted[n] else [mido.Message("control_change", channel=n, control=7, value=msg.value)]
            return []
        if msg.type == "note_on" and msg.velocity and msg.note in MIX_MUTES:
            n = MIX_MUTES.index(msg.note)
            self.muted[n] = not self.muted[n]
            if self.led:
                self.led.send(mido.Message("note_on", note=msg.note, velocity=127 if self.muted[n] else 0))
            return [mido.Message("control_change", channel=n, control=7, value=0 if self.muted[n] else self.level[n])]
        return []


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fm1", default="Felucca", help="the FM-1's MIDI port name (part of it)")
    ap.add_argument("-v", action="store_true", help="print what goes to the FM-1")
    a = ap.parse_args()
    fm1, ins, mixes = None, {}, {}
    print("MIDI hub: waiting for the FM-1 and controllers (ctrl-C to stop)")
    while True:
        try:
            outs = mido.get_output_names()
            names = [n for n in mido.get_input_names() if a.fm1 not in n]
            if fm1 is None or fm1.name not in outs:
                port = next((n for n in outs if a.fm1 in n), None)
                if fm1 is not None:
                    fm1.close()
                    fm1 = None
                    print("FM-1 gone")
                if port:
                    fm1 = mido.open_output(port)
                    print("FM-1:", port)
            for n in list(ins):
                if n not in names:
                    ins.pop(n).close()
                    mixes.pop(n, None)
                    print("-", n)
            for n in names:
                if n not in ins:
                    ins[n] = mido.open_input(n)
                    if "MIDI Mix" in n:
                        mixes[n] = MidiMix(next((o for o in outs if "MIDI Mix" in o), None))
                    print("+", n, "(MIDI Mix: tracks 1-8)" if n in mixes else "")
            t_end = time.time() + 1.0
            while time.time() < t_end:              # a second of forwarding, then look at the ports again
                busy = False
                for n, port in ins.items():
                    for msg in port.iter_pending():
                        busy = True
                        if msg.type in ("clock", "active_sensing") or fm1 is None:
                            continue
                        for m in (mixes[n].translate(msg) if n in mixes else [msg]):
                            fm1.send(m)
                            if a.v:
                                print(n, "->", m)
                if not busy:
                    time.sleep(0.001)
        except KeyboardInterrupt:
            break
        except (IOError, OSError) as e:               # a port that went away mid-read: look again
            print("port error:", e)
            time.sleep(0.5)


if __name__ == "__main__":
    main()
