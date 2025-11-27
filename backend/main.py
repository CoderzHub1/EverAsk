from flask import Flask, send_file, request, jsonify
from urllib.request import urlopen, Request
from urllib.parse import urlencode
import io
from dotenv import load_dotenv
from google import genai
import whisper

app = Flask(__name__)

print("Loading Whisper model...")
model = whisper.load_model("small")
print("Whisper model loaded!")

GOOGLE_TTS_URL = "https://translate.google.com/translate_tts"

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
    audio_file = "input.wav"

    # save WAV file from ESP32
    with open(audio_file, "wb") as f:
        f.write(request.data)

    print("Received WAV audio, running Whisper...")

    # Run whisper
    result = model.transcribe(audio_file)
    text = result.get("text", "").strip()

    print("Transcript:", text)

    return jsonify({"transcript": text})

app.run(host="0.0.0.0", port=5000)