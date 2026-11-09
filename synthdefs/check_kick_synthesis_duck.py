"""Measure the actual offline SC output. Does not play audio."""
import math
import pathlib
import struct
import sys

def load(path):
    data = path.read_bytes()
    pos, rate, channels, samples = 12, 0, 0, []
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from('<4sI', data, pos)
        block = data[pos + 8:pos + 8 + size]
        if tag == b'fmt ':
            fmt, channels, rate = struct.unpack_from('<HHI', block)
            assert fmt == 3, 'Expected float WAV'
        elif tag == b'data':
            samples = [x[0] for x in struct.iter_unpack('<f', block)]
        pos += 8 + size + (size & 1)
    assert rate == 48000 and channels == 2
    assert samples and all(math.isfinite(x) for x in samples)
    return samples, rate, channels

def rms(audio, start, end):
    samples, rate, channels = audio
    part = samples[int(start * rate) * channels:int(end * rate) * channels]
    return math.sqrt(sum(x * x for x in part) / len(part))

root = pathlib.Path(sys.argv[1])
audios = {name: load(root / (name + '.wav')) for name in
          ['kick', 'kick_duck', 'short_kick', 'baseline', 'ducked', 'no_sample_duck', 'kick_gated', 'sample', 'sample_gated']}
assert rms(audios['kick'], 0.05, 0.25) < 1e-8
assert rms(audios['kick'], 0.31, 0.4) > 0.02
assert audios['kick'][0] == audios['kick_duck'][0], 'Ducking must never attenuate the kick'
assert rms(audios['short_kick'], 0.5, 0.65) < rms(audios['kick'], 0.5, 0.65) * 0.05
for name in ['ducked', 'no_sample_duck']:
    ratio = rms(audios[name], 0.31, 0.34) / rms(audios['baseline'], 0.31, 0.34)
    recovered = rms(audios[name], 0.95, 1.1) / rms(audios['baseline'], 0.95, 1.1)
    assert ratio < 0.25, (name, ratio)
    assert 0.98 < recovered < 1.02, (name, recovered)
    print(f'{name}: {20 * math.log10(ratio):.1f} dB image reduction, recovered {recovered:.3f}x')
for name in ['kick', 'sample']:
    gated = audios[name + '_gated']
    assert rms(audios[name], 0.38, 0.43) > 0.001
    assert rms(gated, 0.38, 0.43) < 1e-7, f'{name} leaks through the closed gate'
    ratio = rms(gated, 0.47, 0.52) / rms(audios[name], 0.47, 0.52)
    assert abs(ratio - 1) < 1e-5, f'{name} must resume the existing voice, not retrigger'
    print(f'{name}: closed gate is silent; reopening restores the ongoing tail ({ratio:.6f}x)')
print('PASS: synthesis, sample and synth gating, decay, full-image ducking, missing-sample trigger and kick immunity')
