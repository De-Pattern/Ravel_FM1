#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Move Ravel's saved projects and sound banks between a computer and an FM-1, over USB-MIDI.

  fm1_data.py info                         what the FM-1 holds
  fm1_data.py backup FILE.ravel            everything the FM-1 holds, into a file
  fm1_data.py restore FILE.ravel           a file's projects and banks onto the FM-1 (same slots)
      [--only project3,bankB] [--yes] [--port NAME]

A .ravel file is what the emulator's Export file makes (and Import file reads): every saved
project (1-32) and sound bank (A-D). Restoring replaces those slots on the FM-1 and leaves the
others alone. Ravel must be running on the FM-1 (the normal firmware, not update mode). The
messages are firmware/app/rv_xfer.c's; web/emu/xfer.js is the emulator's side. Needs mido with
python-rtmidi (as tools/fm1_install.py does).

Exit codes: 0 done, 1 error, 2 bad arguments or file, 3 FM-1 not found, 4 no answer.
"""
import argparse
import re
import struct
import sys
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0] if "/" in __file__ else ".")   # (fm1_install.py's MIDI wrapper)
STORES, PROJECTS, BANK0, MORE = 38, 7, 9, 13            # projects 1-7: slots 0-6; banks A-D: 9-12; projects 8-32: 13-37
SLOTS = [0, 1, 2, 3, 4, 5, 6, 9, 10, 11, 12] + list(range(MORE, MORE + 25))
CHUNK = 256
PORT_RE = re.compile(r"fm-1|felucca|ravel|composite|sinco|usb-midi", re.I)   # never probe other gear


def proj_no(s):
    return s + 1 if s < PROJECTS else s - MORE + 8


def slot_name(s):
    return f"project {proj_no(s)}" if s < PROJECTS or s >= MORE else f"bank {'ABCD'[s - BANK0]}"


def slot_key(s):
    return f"project{proj_no(s)}" if s < PROJECTS or s >= MORE else f"bank{'ABCD'[s - BANK0]}"


class Fail(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return out


def unpack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= (b & 0x7F) << nb
        nb += 7
        if nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def frame(cmd, body):
    body = bytes(body)
    m = bytes([0, 0x59, cmd, len(body) & 0xFF, (len(body) >> 8) & 0xFF, (len(body) >> 16) & 0xFF]) + body + bytes([~sum(body) & 0xFF])
    return bytes([0xF0, *pack7(m), 0xF7])


def parse(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) < 7 or d[0] != 0 or d[1] != 0x59 or (d[3] | d[4] << 8 | d[5] << 16) + 7 != len(d):
        return None
    if ~sum(d[6:-1]) & 0xFF != d[-1]:
        return None
    return d[2], d[6:-1]


# ------------------------------------------------------------------- file ---

def file_encode(entries):
    out = bytearray(b"RAVELDAT" + struct.pack("<II", 1, len(entries)))
    for slot, data in entries:
        out += struct.pack("<III", slot, len(data), zlib.crc32(data) & 0xFFFFFFFF) + data
    return bytes(out)


def file_decode(raw):
    if len(raw) < 16 or raw[:8] != b"RAVELDAT":
        raise Fail(2, "not a Ravel file")
    ver, n = struct.unpack_from("<II", raw, 8)
    if ver != 1:
        raise Fail(2, "a newer Ravel file than this tool reads")
    out, o = [], 16
    for _ in range(n):
        if o + 12 > len(raw):
            raise Fail(2, "the file is cut short")
        slot, ln, crc = struct.unpack_from("<III", raw, o)
        o += 12
        data = raw[o:o + ln]
        o += ln
        if len(data) != ln:
            raise Fail(2, "the file is cut short")
        if zlib.crc32(data) & 0xFFFFFFFF != crc:
            raise Fail(2, f"{slot_name(slot)} is damaged in the file")
        if slot not in SLOTS:
            raise Fail(2, "the file has a slot this tool doesn't know")
        out.append((slot, bytes(data)))
    return out


# ----------------------------------------------------------------- device ---

class Ravel:
    """asks, and waits for the answer with the same command (3 tries)"""

    def __init__(self, link, timeout=2.0):
        self.link, self.timeout = link, timeout

    def ask(self, cmd, body):
        for _ in range(3):
            self.link.drain()
            if not self.link.send(frame(cmd, body)):
                break
            end = time.monotonic() + self.timeout
            while (left := end - time.monotonic()) > 0:
                p = self.link.read(left)
                if p is None:
                    break
                m = parse(p)
                if m and m[0] == cmd:
                    return m[1]
        raise Fail(4, "the FM-1 doesn't answer (is Ravel running on it?)")

    def info(self):
        b = self.ask(0x60, b"")
        if b[:4] != b"RVX1":
            raise Fail(4, "the FM-1 answered, but not as Ravel")
        out = []
        for s in range(b[4]):
            ln, crc = struct.unpack_from("<II", b, 5 + s * 8)
            if ln != 0xFFFFFFFF and s in SLOTS:
                out.append((s, ln, crc))
        return out

    def read_slot(self, slot, ln, progress=None):
        data = bytearray()
        for off in range(0, ln, CHUNK):
            n = min(CHUNK, ln - off)
            r = self.ask(0x61, bytes([slot]) + struct.pack("<IH", off, n))
            if r[5] != 0 or len(r) != 6 + n:
                raise Fail(1, f"reading {slot_name(slot)} failed")
            data += r[6:]
            if progress:
                progress(slot, off + n, ln)
        return bytes(data)

    def write_slot(self, slot, data, progress=None):
        r = self.ask(0x62, bytes([slot]) + struct.pack("<I", len(data)))
        if r[1] != 0:
            raise Fail(1, f"{slot_name(slot)} won't fit on the FM-1 (or its flash failed)")
        for off in range(0, len(data), CHUNK):
            r = self.ask(0x63, bytes([slot]) + struct.pack("<I", off) + data[off:off + CHUNK])
            if r[5] != 0:
                raise Fail(1, f"writing {slot_name(slot)} failed")
            if progress:
                progress(slot, min(off + CHUNK, len(data)), len(data))
        r = self.ask(0x64, bytes([slot]) + struct.pack("<II", len(data), zlib.crc32(data) & 0xFFFFFFFF))
        if r[1] != 0:
            raise Fail(1, f"{slot_name(slot)} didn't check out after writing; try again")


def find_port(backend, want=None):
    ins = backend.input_names()
    cands = [n for n in ins if (want and want.lower() in n.lower()) or (not want and PORT_RE.search(n))]
    if not cands:
        raise Fail(3, "no FM-1 found: connect it by USB with Ravel running" + (f" (no port like {want!r})" if want else ""))
    from fm1_install import pair_output
    outs = backend.output_names()
    for name in cands:
        out = pair_output(name, outs)
        if out:
            return name, out
    raise Fail(3, "found the FM-1's input but not its output")


def progress_line(slot, a, b):
    if sys.stdout.isatty():
        print(f"\r  {slot_name(slot)}: {100 * a // max(b, 1):3d}%", end="", flush=True)
    if a >= b:
        print(("\r" if sys.stdout.isatty() else "") + f"  {slot_name(slot)}: done")


def main(argv=None, backend=None):
    ap = argparse.ArgumentParser(description="Ravel projects and sounds to and from an FM-1 over USB-MIDI")
    ap.add_argument("action", choices=["info", "backup", "restore"])
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port")
    ap.add_argument("--only", help="restore only these, e.g. project3,bankB")
    ap.add_argument("--yes", action="store_true", help="replace slots on the FM-1 without asking")
    a = ap.parse_args(argv)
    try:
        if a.action != "info" and not a.file:
            raise Fail(2, f"{a.action} needs a .ravel file")
        entries = None
        if a.action == "restore":
            with open(a.file, "rb") as f:
                entries = file_decode(f.read())
            if a.only:
                keep = {k.strip().lower().replace(" ", "") for k in a.only.split(",")}
                entries = [e for e in entries if slot_key(e[0]) in keep]
            if not entries:
                raise Fail(2, "nothing to restore")
        if backend is None:
            from fm1_install import MidoBackend
            backend = MidoBackend()
        i, o = find_port(backend, a.port)
        link = backend.open(i, o)
        try:
            dev = Ravel(link)
            have = dev.info()
            if a.action == "info":
                if not have:
                    print("The FM-1 has nothing saved.")
                for s, ln, crc in have:
                    print(f"  {slot_name(s):10s} {ln:6d} bytes  crc {crc:08x}")
            elif a.action == "backup":
                out = []
                for s, ln, crc in have:
                    data = dev.read_slot(s, ln, progress_line)
                    if zlib.crc32(data) & 0xFFFFFFFF != crc:
                        raise Fail(1, f"{slot_name(s)} came over damaged; try again")
                    out.append((s, data))
                with open(a.file, "wb") as f:
                    f.write(file_encode(out))
                print(f"Saved {len(out)} slot(s) to {a.file}.")
            else:
                over = [slot_name(s) for s, _ in entries if any(h[0] == s for h in have)]
                if over and not a.yes:
                    if input(f"This replaces {', '.join(over)} on the FM-1. Go ahead? [y/N] ").strip().lower() not in ("y", "yes"):
                        raise Fail(1, "cancelled")
                for s, data in entries:
                    dev.write_slot(s, data, progress_line)
                print(f"Restored {', '.join(slot_name(s) for s, _ in entries)}. Load a project with SAVE > PROJECT > LOAD.")
        finally:
            link.close()
        return 0
    except Fail as e:
        print(f"fm1_data: {e}", file=sys.stderr)
        return e.code
    except OSError as e:
        print(f"fm1_data: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
