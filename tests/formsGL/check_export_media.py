"""Check the artifact made by test_export_media: timestamps, duration and A/V pulse sync."""
import array
import json
import math
import subprocess
import sys

base = sys.argv[1]
ffmpeg = "/opt/homebrew/bin/ffmpeg"
probe = json.loads(subprocess.check_output([
    "/opt/homebrew/bin/ffprobe", "-v", "error", "-show_streams", "-of", "json", base + ".mp4"
]))
v, a = probe["streams"]
assert v["codec_type"] == "video" and a["codec_type"] == "audio"
assert v["avg_frame_rate"] == "60/1" and int(v["nb_frames"]) == 90
assert float(v["start_time"]) == float(a["start_time"]) == 0
assert float(v["duration"]) == float(a["duration"]) == 1.5

def samples(path):
    raw = subprocess.check_output([ffmpeg, "-v", "error", "-i", path, "-map", "0:a:0", "-f", "f32le", "-ac", "1", "-ar", "48000", "pipe:1"])
    result = array.array("f"); result.frombytes(raw)
    return result

wav, embedded = samples(base + ".wav"), samples(base + ".mp4")
assert all(math.isfinite(x) for x in wav)
assert max(abs(x) for x in wav[:23000]) < 1e-6
assert max(abs(x) for x in wav[25000:35000]) > 1e-4
onset = next(i for i, x in enumerate(wav) if abs(x) > 1e-5)
assert abs(onset - 24000) < 64, onset  # within one synth control block of the visual pulse
# AAC is lossy; compare alignment against the lossless WAV using cross correlation.
start, stop = 25000, 35000
correlation = [(sum(wav[i] * embedded[i + lag] for i in range(start, stop, 8)), lag) for lag in range(-128, 129)]
lag = max(correlation)[1]
# AAC changes waveform phase slightly; allow less than 0.2 ms of correlation uncertainty.
assert abs(lag) <= 8, lag
# Check the visible pulse begins on the very same frame as the score's sound pulse.
frames = subprocess.check_output([ffmpeg, "-v", "error", "-i", base + ".mp4", "-map", "0:v:0", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"])
brightness = [frames[(n * 64 * 64 + 32 * 64 + 40) * 3] for n in range(90)]
assert brightness[29] < 10 and brightness[30] > 240
with open(base + ".media.log") as f:
    log = f.read()
assert "FAILURE" not in log and "ERROR" not in log
print(f"PASS: 1/3-bar common loop, 90 frames, embedded AAC starts at zero; WAV/AAC offset {lag} samples; visual/audio pulse at 0.5 s")
