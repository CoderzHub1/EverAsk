from flask import Flask, send_file, request, jsonify
import os
from datetime import datetime
from urllib.request import urlopen, Request
from urllib.parse import urlencode
import io
import wave
import numpy as np
from dotenv import load_dotenv
from google import genai
import speech_recognition as sr
import speech_recognition as sr

app = Flask(__name__)

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

    prompt = f"Answer this in short and sweet to be converted to speech through google tts service: {text}"
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
    size = len(raw_bytes)
    print("Received WAV bytes:", size)

    # Save incoming samples to disk
    samples_dir = os.path.join(os.path.dirname(__file__), "samples")
    os.makedirs(samples_dir, exist_ok=True)

    ts = datetime.utcnow().strftime("%Y%m%d-%H%M%S-%f")
    filename = f"chunk-{ts}.wav"
    filepath = os.path.join(samples_dir, filename)

    try:
        with open(filepath, "wb") as f:
            f.write(raw_bytes)
        print(f"Saved sample to {filepath}")
        # Use SpeechRecognition with Google Web Speech API
        recognizer = sr.Recognizer()
        # Load WAV from memory via AudioFile
        with sr.AudioFile(io.BytesIO(raw_bytes)) as source:
            audio_data = recognizer.record(source)
        try:
            text = recognizer.recognize_google(audio_data, language="en-US")
            # Delete file after successful transcription
            try:
                os.remove(filepath)
                print(f"Deleted sample {filepath}")
            except Exception as de:
                print(f"Warning: failed to delete {filepath}: {de}")
        except sr.UnknownValueError:
            text = ""
        except sr.RequestError as e:
            # Keep the file for debugging if API fails
            return jsonify({
                "saved": True,
                "filename": filename,
                "bytes": size,
                "error": f"Google Speech API request failed: {e}"
            }), 502

        return jsonify({
            "saved": True,
            "filename": filename,
            "bytes": size,
            "transcript": text
        })
    except Exception as e:
        print("Failed to save sample:", e)
        return jsonify({"saved": False, "error": str(e)}), 500

app.run(host="0.0.0.0", port=5000)