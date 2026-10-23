# SPDX-License-Identifier: Apache-2.0
"""Generate original, deterministic gameplay tones for the asset packer."""
import math
import struct
import wave
from pathlib import Path

RATE = 16000
TONES = {
    "start.wav": (660, 880, 120),
    "eat.wav": (440, 740, 85),
    "game_over.wav": (360, 180, 300),
}


def write_tone(path: Path, start_hz: int, end_hz: int, duration_ms: int) -> None:
    count = duration_ms * RATE // 1000
    phase = 0.0
    frames = bytearray()
    for index in range(count):
        hz = start_hz + (end_hz - start_hz) * index / max(count - 1, 1)
        phase += 2.0 * math.pi * hz / RATE
        edge = min(index, count - index - 1, 80) / 80.0
        sample = int((math.sin(phase) + 0.06 * math.sin(phase * 2.0)) * 12000 * edge)
        frames.extend(struct.pack("<h", sample))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(frames)


for filename, tone in TONES.items():
    write_tone(Path(__file__).with_name(filename), *tone)
