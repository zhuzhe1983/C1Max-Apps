#!/usr/bin/env python3
"""Original quiet, two-part mechanical shutter transient; no sampled audio."""
import math, random, struct, wave
from pathlib import Path
rate = 48000
random.seed(5648)
frames = []
low = 0.0
for i in range(int(rate * .22)):
    t = i / rate
    noise = random.uniform(-1, 1)
    low = low * .74 + noise * .26
    sound = 0.0
    for start, gain, decay, frequency in [(0.008,.14,.013,1700),(.061,.11,.025,950)]:
        dt = t-start
        if dt >= 0:
            attack = min(1.0, dt/.001)
            env = attack * math.exp(-dt/decay)
            sound += gain * env * (.7 * (noise-low) + .3 * math.sin(2*math.pi*frequency*dt))
    value = round(max(-.18,min(.18,sound))*32767)
    frames.append(struct.pack('<hh',value,value))
path=Path(__file__).resolve().parents[1]/'assets/shutter.wav'
with wave.open(str(path),'wb') as out:
    out.setnchannels(2);out.setsampwidth(2);out.setframerate(rate);out.writeframes(b''.join(frames))
print(path)
