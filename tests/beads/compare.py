#!/usr/bin/env python3
"""Compare the fixed-point port's renders against the float reference.

For each scenario: overall level difference, sample-level SNR (how exactly the
port tracks the original), loudness-envelope correlation, and spectral
centroid difference (does it sound like the same thing). Writes WAVs of both
for listening, and exits non-zero if any scenario fails its thresholds.
"""
import sys
import wave
import numpy as np

sys.path.insert(0, ".")

SCENARIOS = {}
for line in open("scenarios.h"):
    line = line.strip()
    if line.startswith('{ "'):
        parts = [p.strip() for p in line.strip("{},").split(",")]
        name = parts[0].strip('"')
        SCENARIOS[name] = float(parts[3].rstrip("f"))  # sample rate

def load(path):
    x = np.fromfile(path, dtype=np.float32).reshape(-1, 2)
    return x

def write_wav(path, x, sr):
    y = np.clip(x, -1, 1)
    y = (y * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(int(sr))
        w.writeframes(y.tobytes())

def envelope(x, win):
    m = x.mean(axis=1)
    n = len(m) // win
    return np.sqrt((m[: n * win].reshape(n, win) ** 2).mean(axis=1) + 1e-12)

def centroids(x, sr, win):
    m = x.mean(axis=1)
    n = len(m) // win
    frames = m[: n * win].reshape(n, win) * np.hanning(win)
    spec = np.abs(np.fft.rfft(frames, axis=1))
    freqs = np.fft.rfftfreq(win, 1 / sr)
    energy = spec.sum(axis=1) + 1e-9
    return (spec * freqs).sum(axis=1) / energy, energy

# Brightness tolerance per scenario, in percent. reverse_up reads short reversed
# grains at +12 semitones, where the spectral centroid swings with sub-sample
# grain timing: the original differs from itself by 15% when Time moves by
# 0.00002, and Beads keeps its trigger phase in 32-bit floats, which drift by a
# fraction of a sample per grain. The port keeps exact time, so the two drift
# apart; anything inside the original's own sensitivity passes.
CENTROID_TOLERANCE = {"reverse_up": 20.0}

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "out"
    failed = 0
    print(f"{'scenario':14} {'level dB':>9} {'SNR dB':>8} {'env corr':>9} {'centroid':>9}  verdict")
    for name, sr in SCENARIOS.items():
        ref = load(f"{out}/{name}_ref.f32")
        port = load(f"{out}/{name}_port.f32")
        n = min(len(ref), len(port))
        ref, port = ref[:n], port[:n]
        write_wav(f"{out}/{name}_ref.wav", ref, sr)
        write_wav(f"{out}/{name}_port.wav", port, sr)
        rms_r = np.sqrt((ref ** 2).mean()) + 1e-12
        rms_p = np.sqrt((port ** 2).mean()) + 1e-12
        level = 20 * np.log10(rms_p / rms_r)
        err = np.sqrt(((ref - port) ** 2).mean()) + 1e-12
        snr = 20 * np.log10(rms_r / err)
        win = int(sr * 0.02)
        er, ep = envelope(ref, win), envelope(port, win)
        corr = np.corrcoef(er, ep)[0, 1]
        cr, wr = centroids(ref, sr, 1024)
        cp, _ = centroids(port, sr, 1024)
        loud = wr > np.percentile(wr, 30)
        cdiff = np.median(np.abs(cr[loud] - cp[loud]) / (cr[loud] + 1)) * 100
        ok = abs(level) < 1.0 and corr > 0.95 and cdiff < CENTROID_TOLERANCE.get(name, 10.0)
        failed += not ok
        print(f"{name:14} {level:9.2f} {snr:8.1f} {corr:9.3f} {cdiff:8.1f}%  {'ok' if ok else 'FAIL'}")
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
