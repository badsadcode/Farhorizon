#!/usr/bin/env python3
"""Synthesises FARHORIZON's placeholder sound effects (all original, procedurally generated).

These are stand-ins until real recorded/designed audio exists. They aim for mood rather than
realism: low industrial hums, tired ventilation, metal groaning under stress, a cold empty
drone for open space, and clunky old-school machinery. Deterministic (fixed seeds).

Usage:  generate_placeholder_sounds.py <output-dir>     (requires numpy)
"""
import sys
import wave
from pathlib import Path

import numpy as np

SR = 48000
rng = np.random.default_rng(1977)


def t(seconds):
    return np.arange(int(SR * seconds)) / SR




def lowpass_fast(x, cutoff, passes=1):
    # Vectorised approximation: exponential smoothing via cumulative filter in chunks.
    a = np.exp(-2.0 * np.pi * cutoff / SR)
    y = x.copy()
    for _ in range(passes):
        out = np.empty_like(y)
        acc = 0.0
        block = 4096
        coeff = (1 - a) * a ** np.arange(block)[::-1]
        for s in range(0, len(y), block):
            seg = y[s:s + block]
            n = len(seg)
            # direct convolution with the truncated kernel for each block start state
            conv = np.convolve(seg, (1 - a) * a ** np.arange(n))[:n]
            out[s:s + n] = conv + acc * a ** (np.arange(n) + 1)
            acc = out[s + n - 1]
        y = out
    return y


def highpass(x, cutoff):
    return x - lowpass_fast(x, cutoff)


def brown(n):
    w = rng.standard_normal(n)
    b = np.cumsum(w)
    b = highpass(b, 8.0)
    return b / (np.max(np.abs(b)) + 1e-9)


def loopable(x, fade=0.25):
    """Crossfades the tail into the head so the file loops without a click."""
    n = int(SR * fade)
    head, tail = x[:n].copy(), x[-n:].copy()
    ramp = np.linspace(0, 1, n)
    x = x[:-n].copy()
    x[:n] = head * ramp + tail * (1 - ramp)
    return x


def normalize(x, peak=0.8):
    return x * (peak / (np.max(np.abs(x)) + 1e-9))


def write(path, x, stereo_spread=0.0):
    """Mono 16-bit PCM (the mixer spreads/pans at runtime); `stereo_spread` is kept for API stability."""
    x = np.clip(x, -1, 1)
    pcm = (x * 32767).astype('<i2')
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f"{path.name}: {len(x) / SR:.1f} s")


def env(n, attack, release):
    e = np.ones(n)
    a, r = int(SR * attack), int(SR * release)
    if a:
        e[:a] = np.linspace(0, 1, a)
    if r:
        e[-r:] *= np.exp(-np.linspace(0, 6, r))
    return e


def hull_hum():
    tt = t(10.25)
    hum = (0.5 * np.sin(2 * np.pi * 50 * tt) + 0.3 * np.sin(2 * np.pi * 100 * tt + 0.3) +
           0.12 * np.sin(2 * np.pi * 150 * tt) + 0.06 * np.sin(2 * np.pi * 247 * tt))
    hum *= 0.85 + 0.15 * np.sin(2 * np.pi * 0.1 * tt)  # slow beating (10 s period -> loops cleanly)
    rumble = lowpass_fast(brown(len(tt)), 90) * 0.9
    return normalize(loopable(hum * 0.5 + rumble), 0.6)


def vents():
    tt = t(8.25)
    n = rng.standard_normal(len(tt))
    air = highpass(lowpass_fast(n, 1800), 300)
    air *= 0.75 + 0.25 * np.sin(2 * np.pi * 0.125 * tt) + 0.08 * np.sin(2 * np.pi * 3.0 * tt)
    rattle = (rng.random(len(tt)) < 0.0004).astype(float)
    rattle = lowpass_fast(rattle, 2500) * 20
    return normalize(loopable(air + rattle * 0.3), 0.5)


def creak(seed):
    r = np.random.default_rng(seed)
    dur = r.uniform(2.0, 3.2)
    tt = t(dur)
    f0 = r.uniform(55, 90)
    sweep = f0 * (1 + 0.35 * np.sin(2 * np.pi * r.uniform(0.2, 0.5) * tt))
    phase = 2 * np.pi * np.cumsum(sweep) / SR
    tone = np.sin(phase) + 0.5 * np.sin(2.01 * phase) + 0.3 * np.sin(3.7 * phase)
    stick = (r.random(len(tt)) < 0.004) * r.uniform(0.5, 1.0, len(tt))  # stick-slip grit
    grit = lowpass_fast(stick, 1200) * 8
    e = np.sin(np.pi * np.clip(tt / dur, 0, 1)) ** 1.5
    return normalize((tone * 0.6 + grit) * e, 0.7)


def hangar_drone():
    tt = t(12.25)
    drone = 0.6 * np.sin(2 * np.pi * 41 * tt) + 0.3 * np.sin(2 * np.pi * 61.5 * tt) + 0.15 * np.sin(2 * np.pi * 82 * tt)
    drone *= 0.8 + 0.2 * np.sin(2 * np.pi * (1 / 12) * tt)
    air = lowpass_fast(rng.standard_normal(len(tt)), 400) * 0.6
    thumps = np.zeros(len(tt))
    for k in np.arange(1.3, 12.0, 2.9):  # distant machinery
        i = int(k * SR)
        n = int(0.6 * SR)
        seg = np.sin(2 * np.pi * 38 * t(0.6)) * np.exp(-t(0.6) * 7)
        thumps[i:i + n] += seg[:len(thumps[i:i + n])]
    return normalize(loopable(drone * 0.5 + air + thumps * 0.7), 0.6)


def space_void():
    tt = t(16.25)
    sub = 0.5 * np.sin(2 * np.pi * 32 * tt) * (0.6 + 0.4 * np.sin(2 * np.pi * (1 / 16) * tt))
    breath = lowpass_fast(rng.standard_normal(len(tt)), 220, passes=2)
    breath *= 0.6 + 0.4 * np.sin(2 * np.pi * (1 / 8) * tt + 1.0)
    shimmer = 0.04 * np.sin(2 * np.pi * 523.25 * tt) * (0.5 + 0.5 * np.sin(2 * np.pi * (1 / 16) * tt))
    return normalize(loopable(sub + breath * 1.5 + shimmer), 0.5)


def engine_loop():
    tt = t(4.25)
    base = 58
    saw = sum(np.sin(2 * np.pi * base * k * tt) / k for k in range(1, 9))
    rumble = lowpass_fast(rng.standard_normal(len(tt)), 160) * 2.0
    hiss = highpass(rng.standard_normal(len(tt)), 2000) * 0.08
    return normalize(loopable(saw * 0.35 + rumble + hiss), 0.7)


def laser_loop():
    tt = t(2.25)
    f = 196
    buzz = np.sign(np.sin(2 * np.pi * f * tt)) * 0.3 + np.sin(2 * np.pi * 2 * f * tt) * 0.3
    buzz *= 0.7 + 0.3 * np.sin(2 * np.pi * 14 * tt)
    crackle = (rng.random(len(tt)) < 0.01) * rng.uniform(-1, 1, len(tt))
    return normalize(loopable(lowpass_fast(buzz, 3500) + crackle * 0.4), 0.55)


def clamp():
    tt = t(1.6)
    thunk = np.sin(2 * np.pi * 70 * tt) * np.exp(-tt * 6) + 0.5 * np.sin(2 * np.pi * 143 * tt) * np.exp(-tt * 9)
    click = rng.standard_normal(len(tt)) * np.exp(-tt * 80)
    ring = 0.2 * np.sin(2 * np.pi * 611 * tt) * np.exp(-tt * 4)
    return normalize(thunk + click * 0.5 + ring, 0.9)


def airlock():
    tt = t(3.0)
    noise = rng.standard_normal(len(tt))
    hiss = highpass(noise, 600) * np.exp(-((tt - 0.9) ** 2) / 0.35)
    thunk_t = t(0.8)
    thunk = np.zeros(len(tt))
    i = int(2.1 * SR)
    seg = np.sin(2 * np.pi * 62 * thunk_t) * np.exp(-thunk_t * 7)
    thunk[i:i + len(seg)] = seg[:len(thunk) - i]
    return normalize(hiss * 0.6 + thunk, 0.85)


def ui_beep():
    tt = t(0.09)
    return normalize(np.sign(np.sin(2 * np.pi * 1320 * tt)) * env(len(tt), 0.002, 0.03) * 0.5, 0.45)


def scoop():
    tt = t(0.25)
    tick = np.sin(2 * np.pi * 900 * tt) * np.exp(-tt * 30) + np.sin(2 * np.pi * 1730 * tt) * np.exp(-tt * 45) * 0.5
    return normalize(tick, 0.5)


def radio_blip():
    tt = t(0.35)
    chirp = np.sin(2 * np.pi * (700 + 900 * tt / 0.35) * tt)
    static = highpass(rng.standard_normal(len(tt)), 1500) * 0.3
    return normalize((chirp * 0.7 + static) * env(len(tt), 0.005, 0.1), 0.45)


def warning():
    tt = t(0.6)
    tone = np.sign(np.sin(2 * np.pi * 440 * tt)) * ((tt % 0.3) < 0.18)
    return normalize(lowpass_fast(tone, 2500), 0.5)


def wind():
    tt = t(14.25)
    n = rng.standard_normal(len(tt))
    gust = 0.55 + 0.45 * np.sin(2 * np.pi * (1 / 7) * tt + 0.5) * np.sin(2 * np.pi * (1 / 14) * tt + 1.2)
    body = lowpass_fast(n, 500, passes=2) * 2.5
    whistle = highpass(lowpass_fast(n, 1400), 700) * 0.6
    grit = (rng.random(len(tt)) < 0.002) * rng.uniform(-1, 1, len(tt))
    grit = highpass(grit, 2000) * 2.0
    return normalize(loopable((body + whistle * gust) * gust + grit * gust), 0.6)


def laser_shot():
    # Tired capacitor discharge: a falling square sweep, filtered so it thuds more than it zaps.
    tt = t(0.28)
    f = 1400 * np.exp(-tt * 14) + 160
    phase = 2 * np.pi * np.cumsum(f) / SR
    tone = np.sign(np.sin(phase)) * 0.6 + np.sin(phase * 0.5) * 0.4
    hiss = highpass(rng.standard_normal(len(tt)), 2500) * np.exp(-tt * 40) * 0.4
    return normalize(lowpass_fast(tone, 3000) * np.exp(-tt * 13) + hiss, 0.55)


def cannon_shot():
    tt = t(0.45)
    thump = np.sin(2 * np.pi * (90 * np.exp(-tt * 6) + 40) * tt) * np.exp(-tt * 11)
    crack = lowpass_fast(rng.standard_normal(len(tt)), 2200) * np.exp(-tt * 35)
    return normalize(thump + crack * 0.8, 0.7)


def rocket_launch():
    tt = t(1.4)
    n = rng.standard_normal(len(tt))
    roar = lowpass_fast(n, 700, passes=2) * 3.0
    hiss = highpass(n, 2000) * 0.5
    e = np.minimum(1.0, tt / 0.05) * np.exp(-tt * 2.2)
    pop = np.sin(2 * np.pi * 55 * tt) * np.exp(-tt * 18)
    return normalize((roar + hiss) * e + pop, 0.65)


def shield_hit():
    # Electrical fizz with a ringing emitter tone.
    tt = t(0.5)
    fizz = highpass(rng.standard_normal(len(tt)), 1800) * ((rng.random(len(tt)) < 0.25) * 1.0)
    ring = np.sin(2 * np.pi * 820 * tt + 3 * np.sin(2 * np.pi * 37 * tt)) * 0.5
    return normalize((lowpass_fast(fizz, 6000) * 0.8 + ring) * np.exp(-tt * 9), 0.55)


def hull_hit():
    # Something heavy hits a steel bulkhead: inharmonic clang plus a dull body knock.
    tt = t(1.1)
    partials = [(173, 1.0, 5), (411, 0.6, 7), (697, 0.4, 9), (1123, 0.25, 12)]
    clang = sum(a * np.sin(2 * np.pi * f * tt) * np.exp(-tt * d) for f, a, d in partials)
    knock = lowpass_fast(rng.standard_normal(len(tt)), 400) * np.exp(-tt * 25) * 3
    return normalize(clang * 0.6 + knock, 0.8)


def explosion():
    tt = t(3.2)
    n = rng.standard_normal(len(tt))
    body = lowpass_fast(n, 180, passes=2) * 6
    crackle = highpass(n, 900) * (rng.random(len(tt)) < 0.04) * 1.5
    e = np.minimum(1.0, tt / 0.01) * np.exp(-tt * 1.6)
    boom = np.sin(2 * np.pi * (48 * np.exp(-tt * 2) + 22) * tt) * np.exp(-tt * 3)
    return normalize((body + crackle * np.exp(-tt * 3)) * e + boom, 0.9)


def alarm():
    # Old klaxon: two alternating detuned square tones through a cheap speaker.
    tt = t(1.2)
    f = np.where((tt % 0.4) < 0.2, 620, 520)
    phase = 2 * np.pi * np.cumsum(f) / SR
    tone = np.sign(np.sin(phase)) + 0.5 * np.sign(np.sin(phase * 1.01))
    return normalize(highpass(lowpass_fast(tone, 1800), 300) * env(len(tt), 0.01, 0.1), 0.4)


def main(out):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    write(out / "hull_hum.wav", hull_hum(), 0.5)
    write(out / "vents.wav", vents(), 1.0)
    for i in range(3):
        write(out / f"creak_{i}.wav", creak(100 + i), 1.0)
    write(out / "hangar_drone.wav", hangar_drone(), 1.0)
    write(out / "space_void.wav", space_void(), 1.0)
    write(out / "engine_loop.wav", engine_loop(), 0.3)
    write(out / "laser_loop.wav", laser_loop())
    write(out / "clamp.wav", clamp())
    write(out / "airlock.wav", airlock(), 0.5)
    write(out / "ui_beep.wav", ui_beep())
    write(out / "scoop.wav", scoop())
    write(out / "radio_blip.wav", radio_blip())
    write(out / "warning.wav", warning())
    write(out / "wind.wav", wind())
    # Stage 6 (appended so earlier files keep their random streams).
    write(out / "laser_shot.wav", laser_shot())
    write(out / "cannon_shot.wav", cannon_shot())
    write(out / "rocket_launch.wav", rocket_launch())
    write(out / "shield_hit.wav", shield_hit())
    write(out / "hull_hit.wav", hull_hit())
    write(out / "explosion.wav", explosion())
    write(out / "alarm.wav", alarm())


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
