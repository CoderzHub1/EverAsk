from flask import Flask, send_file, request, jsonify
from urllib.request import urlopen, Request
from urllib.parse import urlencode
import io
import wave
import numpy as np
from dotenv import load_dotenv
from google import genai
import whisper

app = Flask(__name__)

print("Loading Whisper model...")
model = whisper.load_model("small")
print("Whisper model loaded!")

GOOGLE_TTS_URL = "https://translate.google.com/translate_tts"

def _decode_wav_to_16k_mono_float32(wav_bytes: bytes) -> np.ndarray:
    """Decode WAV bytes (8- or 16-bit, mono/stereo, any SR) to 16 kHz mono float32 [-1, 1]."""
    with wave.open(io.BytesIO(wav_bytes), 'rb') as wf:
        n_channels = wf.getnchannels()
        sampwidth = wf.getsampwidth()  # bytes per sample
        framerate = wf.getframerate()
        n_frames = wf.getnframes()
        frames = wf.readframes(n_frames)

    if sampwidth == 1:
        # 8-bit PCM is unsigned [0,255] → center to [-1,1]
        audio = np.frombuffer(frames, dtype=np.uint8).astype(np.float32)
        audio = (audio - 128.0) / 128.0
    elif sampwidth == 2:
        # 16-bit PCM signed little-endian
        audio = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0
    else:
        raise ValueError(f"Unsupported WAV sample width: {sampwidth} bytes")

    if n_channels > 1:
        audio = audio.reshape(-1, n_channels).mean(axis=1)

    # Remove DC offset and normalize
    if audio.size > 0:
        audio = audio - float(np.mean(audio))
        max_abs = float(np.max(np.abs(audio)))
        if max_abs > 0:
            audio = audio / max_abs

    target_sr = 16000
    if framerate != target_sr and audio.size > 1:
        # Linear resample to 16 kHz
        old_indices = np.linspace(0, len(audio) - 1, num=len(audio), dtype=np.float32)
        new_length = int(round(len(audio) * (target_sr / float(framerate))))
        if new_length <= 1:
            new_length = 2
        new_indices = np.linspace(0, len(audio) - 1, num=new_length, dtype=np.float32)
        audio = np.interp(new_indices, old_indices, audio).astype(np.float32)
    else:
        audio = audio.astype(np.float32, copy=False)

    return audio

@app.route("/tts")
def tts_api():
    load_dotenv()
    text = request.args.get("text", "Hello world")
    client = genai.Client()

    prompt = f"Answer this in 2-3 short sentences: {text}"
    response = client.models.generate_content(
        model="gemini-2.0-flash-lite",
        contents=prompt
    )
    tts_text = response.text

    if len(tts_text) > 200:
        tts_text = tts_text[:197] + "..."

    print("Gemini Response for TTS:", tts_text)

    params = urlencode({
        "ie": "UTF-8",
        "client": "tw-ob",
        "q": tts_text,
        "tl": "en"
    })

    url = GOOGLE_TTS_URL + "?" + params

    req = Request(url, headers={'User-Agent': 'Mozilla/5.0'})
    with urlopen(req) as resp:
        mp3_data = resp.read()

    return send_file(
        io.BytesIO(mp3_data),
        mimetype="audio/mpeg"
    )

@app.route("/transcribe", methods=["POST"])
def transcribe():
    raw_bytes = request.data
    print("Received WAV bytes:", len(raw_bytes))

    # Decode and resample to 16 kHz mono float32 for Whisper
    try:
        audio_16k = _decode_wav_to_16k_mono_float32(raw_bytes)
    except Exception as e:
        return jsonify({"error": f"Invalid WAV: {e}"}), 400

    print("Decoded audio length (samples at 16kHz):", len(audio_16k))

    # Transcribe with Whisper (numpy array supported; already 16kHz)
    result = model.transcribe(audio_16k, fp16=False, language='en')
    text = (result.get("text") or "").strip()

    print("Transcript:", text)

    return jsonify({"transcript": text})

app.run(host="0.0.0.0", port=5000)