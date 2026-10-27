#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generated data for the 909 drum engine (firmware/src/dsp/drum909.c, fxbus.c).

Writes two headers (both flash-resident `static const` data):

  x0x_drum_samples.h  the hi-hat, ride and crash as 8-bit mu-law (G.711) with
                      its 256-entry int16 decode table (assets/909/*.wav, from
                      ER-99 via 9W9, GPL-3.0): 110 KB instead of 221 KB of int16,
                      37 dB below the signal (the 909's own cymbals are 6-bit).
                      hh.wav is 24-bit: it is rounded to 16 bits first.
  x0x_drum_tables.h   the tanh lookup used by every saturator, and 9W9's EXP pot
                      curves (er99_pots.h: min * (max/min)^(pot/127)) evaluated
                      here, so the device needs no powf and gets the same values
                      9W9 computes with libm.

  tools/gen_drum_samples.py [build/gen/x0x_drum_samples.h] [--ref-wavs DIR]
--ref-wavs also writes the samples as the device plays them (mu-law decoded,
16-bit) to DIR/{hh,ride,crash}.wav, for the 9W9 reference in tests/host.
The tables header is written next to the samples header.
"""
import math
import struct
import sys
from pathlib import Path

TOTAL_B = [0]
SRC = Path(__file__).resolve().parents[1]
ASSETS = SRC / "assets" / "909"

# (C name, file). The closed hat plays the open hat's buffer (as in 9W9).
SAMPLES = [("x0x_smp_hh", "hh.wav"), ("x0x_smp_ride", "ride.wav"), ("x0x_smp_crash", "crash.wav")]

# 9W9's EXP pot ranges (er99_pots.h). One table per distinct (min, max).
EXP_RANGES = [
    ("BD_DECAY", 100.0, 4000.0), ("BD_PITCH", 0.43, 4.7), ("DRIVE", 0.85, 12.0),
    ("SD_TUNE", 130.0, 320.0), ("SD_TONE", 300.0, 4000.0),
    ("LT_TUNE", 50.0, 90.0), ("LT_DECAY", 80.0, 2600.0),
    ("MT_TUNE", 80.0, 125.0), ("MT_DECAY", 70.0, 2200.0),
    ("HT_TUNE", 110.0, 170.0), ("HT_DECAY", 60.0, 2000.0),
    ("RS_TUNE", 150.0, 300.0), ("CP_TUNE", 650.0, 1400.0), ("CP_TAIL", 120.0, 1000.0),
    ("OH_DECAY", 20.0, 1200.0), ("CH_DECAY", 15.0, 300.0), ("CY_DECAY", 100.0, 3000.0),
    ("SMP_PITCH", 0.25, 4.0), ("FX_HPF", 30.0, 800.0),
]

TANH_N = 1024          # points over [0, TANH_MAX]; linear interpolation
TANH_MAX = 8.0


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def cfloat(x):
    s = repr(f32(x))
    if "e" not in s and "." not in s:
        s += ".0"
    return s + "f"


def read_wav(path):
    """first channel as a list of int16 (16-bit kept, 24-bit rounded)"""
    d = path.read_bytes()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a WAV")
    i, ch, bits, data = 12, 1, 16, None
    while i + 8 <= len(d):
        cid, sz = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if cid == b"fmt ":
            ch, bits = struct.unpack("<H", d[i + 10:i + 12])[0], struct.unpack("<H", d[i + 22:i + 24])[0]
        elif cid == b"data":
            data = d[i + 8:i + 8 + sz]
            break                                   # what 9W9's loader does: stop at data
        i += 8 + sz + (sz & 1)
    if data is None:
        raise SystemExit(f"{path}: no data chunk")
    bp = bits // 8 * ch
    out = []
    for k in range(len(data) // bp):
        p = data[k * bp:k * bp + bits // 8]
        if bits == 16:
            v = struct.unpack("<h", p)[0]
        elif bits == 24:
            v24 = int.from_bytes(p, "little", signed=True)
            v = max(-32768, min(32767, int(math.floor(v24 / 256.0 + 0.5))))
        else:
            raise SystemExit(f"{path}: {bits}-bit not handled")
        out.append(v)
    return out, bits


def arr(ctype, name, vals, fmt=str, per=12, shared=False):
    # shared: a table several units include, defined once (in the unit that defines
    # X0X_DRUM_TABLES_DEFINE, drum909.c) and declared extern in the rest. Static gave each unit its own
    # copy: two of x0x_tanh_tab and x0x_pot_exp, 13.8 KB of the app's flash.
    if shared:
        return "\n".join([f"#ifdef X0X_DRUM_TABLES_DEFINE",
                          f"const {ctype} {name}[{len(vals)}] = {{"] +
                         ["    " + ", ".join(fmt(v) for v in vals[i:i + per]) + "," for i in range(0, len(vals), per)] +
                         ["};", "#else", f"extern const {ctype} {name}[{len(vals)}];", "#endif"])
    lines = [f"static const {ctype} {name}[{len(vals)}] = {{"]
    for i in range(0, len(vals), per):
        lines.append("    " + ", ".join(fmt(v) for v in vals[i:i + per]) + ",")
    lines.append("};")
    return "\n".join(lines)


MU_BIAS, MU_CLIP = 0x84, 32635


def mulaw_enc(x):
    """G.711 mu-law of an int16"""
    sign = 0x80 if x < 0 else 0
    x = min(-x if x < 0 else x, MU_CLIP) + MU_BIAS
    e, m = 7, 0x4000
    while e > 0 and not x & m:
        e -= 1
        m >>= 1
    return ~(sign | e << 4 | (x >> (e + 3)) & 0x0F) & 0xFF


def mulaw_dec(u):
    u = ~u & 0xFF
    x = ((((u & 0x0F) << 3) + MU_BIAS) << ((u >> 4) & 7)) - MU_BIAS
    return -x if u & 0x80 else x


# IMA ADPCM (4 bit), the decoder of firmware/x0x/drum909.c d9_adpcm (X0X stored mu-law: 110 KB; 55 KB here, the
# full band, so it fits the FM-1's flash next to Felucca's own sets). Two samples a byte, the first in the low nibble.
IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def ima_step(code, pred, idx):
    st = IMA_STEP[idx]
    d = st >> 3
    if code & 4:
        d += st
    if code & 2:
        d += st >> 1
    if code & 1:
        d += st >> 2
    pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
    idx = max(0, min(88, idx + IMA_IDX[code & 7]))
    return pred, idx


def ima_encode(pcm):
    """codes (one per sample), and the samples the decoder makes of them"""
    pred, idx, codes, dec = 0, 0, [], []
    for x in pcm:
        best = None
        for c in range(16):                      # the code whose decoded value is nearest (the decoder's own steps)
            p, _ = ima_step(c, pred, idx)
            e = abs(p - x)
            if best is None or e < best[0]:
                best = (e, c)
        c = best[1]
        pred, idx = ima_step(c, pred, idx)
        codes.append(c)
        dec.append(pred)
    return codes, dec


# 6-bit mu-law: code c is the G.711 byte (c << 2) | 2 (the middle of the four it stands for)
MU6 = [mulaw_dec(c << 2 | 2) for c in range(64)]
MU6_SORTED = sorted(range(64), key=lambda c: MU6[c])


def mu6_encode(pcm):
    """the nearest 6-bit code of each sample, and what the decoder makes of them"""
    import bisect
    vals = [MU6[c] for c in MU6_SORTED]
    codes, dec = [], []
    for x in pcm:
        i = bisect.bisect_left(vals, x)
        cand = [j for j in (i - 1, i) if 0 <= j < 64]
        j = min(cand, key=lambda j: abs(vals[j] - x))
        codes.append(MU6_SORTED[j])
        dec.append(vals[j])
    return codes, dec


def pot_exp(lo, hi, pot):
    """er99_pot_to_value for an EXP pot, in float as 9W9 evaluates it"""
    ratio = f32(f32(hi) / f32(lo))
    t = f32(pot / 127.0)
    return f32(f32(lo) * f32(ratio ** t))


def main():
    args = sys.argv[1:]
    ref_wavs = None
    if "--ref-wavs" in args:
        i = args.index("--ref-wavs")
        ref_wavs = Path(args[i + 1])
        del args[i:i + 2]
    out_s = Path(args[0]) if args else SRC / "build" / "gen" / "x0x_drum_samples.h"
    out_t = out_s.parent / "x0x_drum_tables.h"
    out_s.parent.mkdir(parents=True, exist_ok=True)

    hdr = ["/* Generated by tools/gen_drum_samples.py -- do not edit.",
           " * Hi-hat, ride and crash from ER-99 (Matthew Cieplak) via 9W9, GPL-3.0.",
           " * Mono, 44.1 kHz, 6-bit mu-law: the top 6 bits of G.711 (the 909's own cymbal ROMs are 6-bit), four samples",
           " * in three bytes (sample k of a group: bits 6k .. 6k + 5 of the 24-bit little-endian word); x0x_mu6",
           " * decodes a code to int16 (full scale 32768). 83 KB, X0X's 8-bit was 110 KB. */",
           "#pragma once", "#include <stdint.h>", "",
           arr("int16_t", "x0x_mu6", MU6, per=16), ""]
    total = 0
    for name, fn in SAMPLES:
        pcm, bits = read_wav(ASSETS / fn)
        total += len(pcm)
        hdr.append(f"/* {fn}: {len(pcm)} frames, source {bits}-bit */")
        hdr.append(f"#define {name.upper()}_LEN {len(pcm)}u")
        codes, dec = mu6_encode(pcm)
        codes += [0] * (-len(codes) % 4)
        packed = []
        for i in range(0, len(codes), 4):
            w = codes[i] | codes[i + 1] << 6 | codes[i + 2] << 12 | codes[i + 3] << 18
            packed += [w & 255, w >> 8 & 255, w >> 16 & 255]
        hdr.append(arr("uint8_t", name, packed, per=24))
        err = (sum((a - b) ** 2 for a, b in zip(pcm, dec)) / max(1, sum(a * a for a in pcm))) ** 0.5
        print(f"gen_drum_samples: {fn} 6-bit {len(packed)} B, error {20 * math.log10(max(err, 1e-9)):.1f} dB")
        TOTAL_B[0] += len(packed)
        if ref_wavs:
            ref_wavs.mkdir(parents=True, exist_ok=True)
            body = b"".join(struct.pack("<h", v) for v in dec)
            (ref_wavs / fn).write_bytes(b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVEfmt " +
                                        struct.pack("<IHHIIHH", 16, 1, 1, 44100, 88200, 2, 16) +
                                        b"data" + struct.pack("<I", len(body)) + body)
        hdr.append("")
    out_s.write_text("\n".join(hdr))

    tab = [f32(math.tanh(TANH_MAX * i / TANH_N)) for i in range(TANH_N + 2)]
    t = ["/* Generated by tools/gen_drum_samples.py -- do not edit. */",
         "#pragma once", "",
         f"/* tanh(u) at u = i * {TANH_MAX} / {TANH_N}, i = 0..{TANH_N + 1} (one guard point) */",
         f"#define X0X_TANH_N {TANH_N}",
         f"#define X0X_TANH_SCALE {cfloat(TANH_N / TANH_MAX)}",
         arr("float", "x0x_tanh_tab", tab, cfloat, per=6, shared=True), "",
         "/* 9W9 EXP pots (er99_pots.h): value = min * (max/min)^(pot/127), pot 0..127 */"]
    for i, (n, lo, hi) in enumerate(EXP_RANGES):
        t.append(f"#define X0X_EXP_{n} {i}   /* {lo:g} .. {hi:g} */")
    t.append(f"#define X0X_EXP_COUNT {len(EXP_RANGES)}")
    # STUDIO: each curve as {min, log2(max / min)}; drum909.c d9_pot_value evaluates min * 2^(log2(max/min) pot/127)
    # when a pot is set (fm_exp2f): 19 x 8 bytes instead of 19 x 128 x 4 (9.7 KB of flash). The values are 9W9's
    # within the float rounding of fm_exp2f (tests: drum909 renders)
    t.append("#ifdef X0X_DRUM_TABLES_DEFINE")
    t.append(f"const float x0x_pot_exp_range[{len(EXP_RANGES)}][2] = {{")
    for n, lo, hi in EXP_RANGES:
        t.append(f"    {{{cfloat(f32(lo))}, {cfloat(f32(math.log2(f32(f32(hi) / f32(lo)))))}}},   /* {n} */")
    t.append("};")
    t.append("#else")
    t.append(f"extern const float x0x_pot_exp_range[{len(EXP_RANGES)}][2];")
    t.append("#endif")
    t.append("")
    out_t.write_text("\n".join(t))
    print(f"gen_drum_samples: {total} frames ({TOTAL_B[0]} bytes, 6-bit mu-law) -> {out_s.name}; "
          f"tanh {TANH_N + 2} + {len(EXP_RANGES)} pot curves -> {out_t.name}")


if __name__ == "__main__":
    main()
