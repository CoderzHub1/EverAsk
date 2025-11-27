import io
import time
import struct
import numpy as np
import requests

URL = "http://127.0.0.1:5000/transcribe"
SAMPLE_RATE = 8000
DURATION_SEC = 1.5
FREQ = 440.0


def build_wav_bytes(samples_u8: np.ndarray, sr: int) -> bytes:
    # Ensure uint8
    data_bytes = samples_u8.tobytes()
    num_channels = 1
    bits_per_sample = 8
    byte_rate = sr * num_channels * bits_per_sample // 8
    block_align = num_channels * bits_per_sample // 8
    data_size = len(data_bytes)
    chunk_size = 36 + data_size

    buf = io.BytesIO()
    # RIFF header
    buf.write(b"RIFF")
    buf.write(struct.pack('<I', chunk_size))
    buf.write(b"WAVE")
    # fmt chunk
    buf.write(b"fmt ")
    buf.write(struct.pack('<I', 16))       # PCM header size
    buf.write(struct.pack('<H', 1))        # PCM format
    buf.write(struct.pack('<H', 1))        # mono
    buf.write(struct.pack('<I', sr))
    buf.write(struct.pack('<I', byte_rate))
    buf.write(struct.pack('<H', block_align))
    buf.write(struct.pack('<H', bits_per_sample))
    # data chunk
    buf.write(b"data")
    buf.write(struct.pack('<I', data_size))
    buf.write(data_bytes)
    return buf.getvalue()


def gen_tone_u8(sr: int, seconds: float, freq: float) -> np.ndarray:
    t = np.arange(int(sr * seconds)) / sr
    # Sine in [-1,1]
    x = 0.6 * np.sin(2 * np.pi * freq * t)
    # Map to unsigned 8-bit [0,255]
    u8 = np.clip((x * 127.0 + 128.0), 0, 255).astype(np.uint8)
    return u8


def post_with_retries(wav_bytes: bytes, retries: int = 60, delay: float = 2.0):
    for i in range(retries):
        try:
            resp = requests.post(URL, data=wav_bytes, headers={"Content-Type": "audio/wav"}, timeout=30)
            print(f"Attempt {i+1}: status {resp.status_code}")
            if resp.ok:
                print(resp.json())
                return True
            else:
                print("Response text:", resp.text[:300])
        except Exception as e:
            print(f"Attempt {i+1} failed: {e}")
        time.sleep(delay)
    return False


if __name__ == "__main__":
    print("Building test 8kHz 8-bit WAV tone…")
    u8 = gen_tone_u8(SAMPLE_RATE, DURATION_SEC, FREQ)
    wav = build_wav_bytes(u8, SAMPLE_RATE)
    print(f"WAV size: {len(wav)} bytes; posting to {URL}")
    ok = post_with_retries(wav)
    if not ok:
        raise SystemExit("/transcribe test failed after retries")
