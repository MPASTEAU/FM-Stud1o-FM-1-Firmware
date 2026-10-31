#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The demo songs as PROJECTS on an FM-1, with no program space (the device's build has no FELUCCA_DEMO: its flash is full).

  fm1_demo_projects.py --build [--songs 8,7,...]    the projects' flash with the demos, build/demo_projects.img
  fm1_demo_projects.py --send [--port NAME] [--yes] that image onto the FM-1 (USB-MIDI)

--build compiles tools/fm1_demo_projects.c once per song (the firmware for the host, as the emulator: main.c
felucca_demo with -DDAH_DEMO_SONG=N), powers on with each and saves its music as a project (projfs.c), one after the
other into one projects' flash, then checks it: the image restored as the device would (editor_backup.c ED_BACKUP_PUT),
each project opened (another song in RAM before) plays the SONG exactly as the demo does (12 s of audio, hashed).
The image is the editor backup's object 18 (the flash of projfs.c, up to its last used sector).

--send restores it with the full backup's PUT (web/fm1backup.js restoreBackup: begin, 256-byte chunks 7-bit packed,
commit), SysEx F0 7D 46 4C 67 ... F7, each request answered before the next. IT REPLACES EVERY PROJECT ON THE DEVICE
(the music in RAM stays; the projects are on SAVE > FILE / PROJECTS). Stop the transport first. Needs mido with
python-rtmidi, e.g. a venv:
  python3 -m venv .venv && .venv/bin/pip install mido python-rtmidi
  .venv/bin/python tools/fm1_demo_projects.py --send
"""
import argparse
import json
import os
import subprocess
import sys
import time
import zlib
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
OUT = SRC / "build"
HOST = OUT / "host"
IMG = OUT / "demo_projects.img"
SECTOR = 4096
PFS_BYTES = 40 * SECTOR                                  # (projfs.c PFS_NSECT sectors)
PFS_ID = 18                                              # (editor_backup.c ED_BK_PFS)
PUT = 67                                                 # (editor.c ED_BACKUP_PUT)
# main.c DAH_DEMO_SONG: the song, the project's name (12 characters, the NAME page's set); the project ids in this order
SONGS = {8: "LO-FI", 7: "SHOWCASE", 6: "TIME", 5: "DARK", 4: "LONG CHORDS", 3: "SLOW", 2: "DEEP HOUSE", 1: "TRIP-HOP"}
RC = {1: "invalid object", 2: "data failed validation", 3: "stop playback first", 4: "flash write failed",
      5: "start again (timed out, USB reset or a save meanwhile)"}


def host_tool(n):
    exe = HOST / f"demo_project_{n}"
    cmd = [os.environ.get("CC", "cc"), "-O1", "-w", f"-DDAH_DEMO_SONG={n}", "-Ibuild/gen", "-Ifirmware/src", "-o", str(exe),
           str(SRC / "tools" / "fm1_demo_projects.c"), "-lm"]
    subprocess.run(cmd, cwd=SRC, check=True)
    return exe


def run(exe, *args):
    r = subprocess.run([str(exe), *map(str, args)], cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"demo: {exe.name} {' '.join(map(str, args))}: {r.stderr.strip() or r.returncode}")
    return dict(l.split(" ", 1) for l in r.stdout.splitlines() if l.startswith(("hash ", "name "))), r.stdout


def used_len(img):
    n = 0
    for s in range(len(img) // SECTOR):
        if img[s * SECTOR:(s + 1) * SECTOR] != b"\xff" * SECTOR:
            n = s + 1
    return n * SECTOR


def build(songs):
    if not (OUT / "gen" / "felucca_tables.h").exists():
        raise SystemExit("demo: build/gen missing: run tools/build.py (or ./build.sh) first")
    HOST.mkdir(parents=True, exist_ok=True)
    flash, hashes, made = IMG.with_suffix(".tmp"), {}, []
    prev = "-"
    for i, n in enumerate(songs, 1):
        exe = host_tool(n)
        got, text = run(exe, "build", prev, flash, i, SONGS[n])
        hashes[i] = got["hash"]
        made.append({"id": i, "song": n, "name": SONGS[n], "hash": got["hash"]})
        print(text.strip().splitlines()[0])
        prev = flash
    img = flash.read_bytes()
    flash.unlink()
    img = img[:used_len(img)]
    IMG.write_bytes(img)
    other = host_tool(0)                                 # (the FX test in RAM before each project opens)
    for p in made:
        got, _ = run(other, "verify", IMG, p["id"])
        ok = got.get("hash") == p["hash"] and got.get("name") == p["name"]
        print(f"verify: project {p['id']} {p['name']:<12} song {p['song']}: restored, opened, "
              f"{'plays as the demo' if ok else 'DIFFERS'} (hash {got.get('hash')} / {p['hash']})")
        if not ok:
            raise SystemExit("demo: a project does not play as its demo")
    IMG.with_suffix(".json").write_text(json.dumps({"bytes": len(img), "crc": zlib.crc32(img), "projects": made}, indent=1))
    print(f"{IMG}: {len(img)} B ({len(img) // SECTOR} of 40 sectors), CRC {zlib.crc32(img):08x}, "
          f"{len(made)} projects")


def u32(n):
    return [(n >> (7 * i)) & (15 if i == 4 else 127) for i in range(5)]


def pack7(b):
    a = []
    for off in range(0, len(b), 7):
        c = b[off:off + 7]
        a.append(sum((x >> 7) << i for i, x in enumerate(c)))
        a.extend(x & 127 for x in c)
    return a


def send(port, yes):
    try:
        import mido
    except ImportError:
        raise SystemExit("demo: needs mido and python-rtmidi (pip install mido python-rtmidi)")
    img = IMG.read_bytes() if IMG.exists() else None
    if img is None:
        raise SystemExit(f"demo: {IMG} missing: --build first")
    if len(img) % SECTOR or len(img) > PFS_BYTES:
        raise SystemExit("demo: the image is not whole sectors of the projects' flash")
    names = mido.get_output_names()
    out_name = next((n for n in names if port.lower() in n.lower()), None)
    in_name = next((n for n in mido.get_input_names() if port.lower() in n.lower()), None)
    if not out_name or not in_name:
        raise SystemExit(f"demo: no MIDI port with \"{port}\" (outputs: {', '.join(names) or 'none'})")
    meta = IMG.with_suffix(".json")
    listing = ", ".join(f"{p['id']} {p['name']}" for p in json.loads(meta.read_text())["projects"]) if meta.exists() else "?"
    print(f"{in_name}: {len(img)} B, the projects {listing}")
    if not yes and input("This REPLACES EVERY PROJECT on the FM-1. Stop the transport. Go on? [y/N] ").strip().lower() != "y":
        raise SystemExit("demo: nothing sent")
    replies = []
    inp = mido.open_input(in_name, callback=lambda m: m.type == "sysex" and replies.append(list(m.data)))
    outp = mido.open_output(out_name)

    def ask(args, timeout=4.0):
        replies.clear()
        outp.send(mido.Message("sysex", data=[0x7D, 0x46, 0x4C, PUT, *args]))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for r in list(replies):
                if len(r) >= 6 and r[:4] == [0x7D, 0x46, 0x4C, PUT] and r[4] == args[0] and r[5] == args[1]:
                    if len(r) < 7 or r[6]:
                        raise SystemExit(f"demo: the FM-1 refused ({RC.get(r[6] if len(r) > 6 else 0, 'rc ?')})")
                    return
            time.sleep(0.002)
        raise SystemExit("demo: no answer (is it Felucca with the projects, connected, stopped?)")

    try:
        ask([0, PFS_ID, *u32(len(img)), *u32(zlib.crc32(img))])
        try:
            for off in range(0, len(img), 256):
                ask([1, PFS_ID, *u32(off), *pack7(img[off:off + 256])])
                print(f"\r{off + 256} / {len(img)} B", end="", flush=True)
            print()
            ask([2, PFS_ID], timeout=15.0)               # (the commit: the rest erased, the CRC read back)
        except BaseException:
            try:
                ask([3, PFS_ID])                         # (abandoned: the transfer cancelled)
            except BaseException:
                pass
            raise
    finally:
        inp.close()
        outp.close()
    print("done: SAVE > FILE (PROJECTS) lists them; OPEN one, PLAY plays its SONG")


def main():
    global IMG
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", action="store_true", help="build and check build/demo_projects.img")
    ap.add_argument("--send", action="store_true", help="restore build/demo_projects.img on the FM-1")
    ap.add_argument("--songs", default=",".join(map(str, SONGS)), help="DAH_DEMO_SONG numbers, project 1 first (default %(default)s)")
    ap.add_argument("--port", default="Felucca", help="a part of the MIDI port's name (default %(default)s)")
    ap.add_argument("--yes", action="store_true", help="no question before --send")
    ap.add_argument("--img", type=Path, default=IMG, help="the image (default %(default)s)")
    a = ap.parse_args()
    IMG = a.img.resolve()
    if not a.build and not a.send:
        ap.error("--build and / or --send")
    if a.build:
        songs = [int(s) for s in a.songs.split(",") if s.strip()]
        if not songs or any(s not in SONGS for s in songs) or len(set(songs)) != len(songs):
            ap.error(f"--songs: some of {', '.join(map(str, SONGS))}, each once")
        build(songs)
    if a.send:
        send(a.port, a.yes)


if __name__ == "__main__":
    main()
