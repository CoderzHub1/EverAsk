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
from flask_cors import CORS

app = Flask(__name__)
CORS(app)  # Enable CORS for all routes

GOOGLE_TTS_URL = "https://translate.google.com/translate_tts"

# Global state for tracking conversation
conversation_state = {
    "listening_for_prompt": False,
    "accumulated_prompt": "",
    "last_transcript": ""
}

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
    global conversation_state
    
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
            print(f"Transcribed: '{text}'")
            
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

        # Process the transcript according to the "ever ask" logic
        response_data = process_transcript(text, filename, size)
        return jsonify(response_data)
        
    except Exception as e:
        print("Failed to save sample:", e)
        return jsonify({"saved": False, "error": str(e)}), 500

def process_transcript(text, filename, size):
    global conversation_state
    
    text_lower = text.lower()
    
    # Check if "ever ask" is mentioned
    if "ever ask" in text_lower:
        print("'ever ask' detected - starting prompt collection")
        conversation_state["listening_for_prompt"] = True
        conversation_state["accumulated_prompt"] = ""
        conversation_state["last_transcript"] = text
        
        return {
            "saved": True,
            "filename": filename,
            "bytes": size,
            "transcript": text,
            "status": "trigger_detected",
            "message": "Started listening for prompt"
        }
    
    # If we're listening for a prompt
    elif conversation_state["listening_for_prompt"]:
        # If transcript is empty, we've reached the end of the prompt
        if text.strip() == "":
            print("Empty transcript detected - ending prompt collection")
            
            if conversation_state["accumulated_prompt"].strip():
                # Send accumulated prompt to TTS
                final_prompt = conversation_state["accumulated_prompt"].strip()
                print(f"Final prompt: '{final_prompt}'")
                
                # Reset state
                conversation_state["listening_for_prompt"] = False
                conversation_state["accumulated_prompt"] = ""
                conversation_state["last_transcript"] = text
                
                # Generate TTS response
                try:
                    tts_response = generate_tts_response(final_prompt)
                    return {
                        "saved": True,
                        "filename": filename,
                        "bytes": size,
                        "transcript": text,
                        "status": "prompt_complete",
                        "prompt": final_prompt,
                        "tts_generated": True,
                        "message": "Prompt processed and TTS generated"
                    }
                except Exception as e:
                    return {
                        "saved": True,
                        "filename": filename,
                        "bytes": size,
                        "transcript": text,
                        "status": "prompt_complete",
                        "prompt": final_prompt,
                        "tts_generated": False,
                        "error": str(e),
                        "message": "Prompt processed but TTS failed"
                    }
            else:
                # No prompt was accumulated
                conversation_state["listening_for_prompt"] = False
                conversation_state["accumulated_prompt"] = ""
                conversation_state["last_transcript"] = text
                
                return {
                    "saved": True,
                    "filename": filename,
                    "bytes": size,
                    "transcript": text,
                    "status": "prompt_cancelled",
                    "message": "No prompt was provided"
                }
        
        # Add to accumulated prompt
        else:
            conversation_state["accumulated_prompt"] += " " + text
            conversation_state["last_transcript"] = text
            print(f"Accumulated prompt: '{conversation_state['accumulated_prompt'].strip()}'")
            
            return {
                "saved": True,
                "filename": filename,
                "bytes": size,
                "transcript": text,
                "status": "accumulating_prompt",
                "accumulated_prompt": conversation_state["accumulated_prompt"].strip(),
                "message": "Adding to prompt"
            }
    
    # Normal transcription (not listening for prompt)
    else:
        conversation_state["last_transcript"] = text
        return {
            "saved": True,
            "filename": filename,
            "bytes": size,
            "transcript": text,
            "status": "normal",
            "message": "Normal transcription"
        }

def generate_tts_response(prompt):
    """Generate TTS response and notify frontend"""
    load_dotenv()
    client = genai.Client()

    ai_prompt = f"Answer this in short and sweet to be converted to speech through google tts service: {prompt}"
    response = client.models.generate_content(
        model="gemini-2.0-flash-lite",
        contents=ai_prompt
    )
    tts_text = response.text

    if len(tts_text) > 200:
        tts_text = tts_text[:197] + "..."

    print("Gemini Response for TTS:", tts_text)
    
    # Store the latest response for frontend to fetch
    conversation_state["latest_tts_text"] = tts_text
    
    return tts_text

@app.route("/status")
def get_status():
    """Get current conversation status and latest TTS text"""
    return jsonify({
        "listening_for_prompt": conversation_state["listening_for_prompt"],
        "accumulated_prompt": conversation_state["accumulated_prompt"],
        "last_transcript": conversation_state["last_transcript"],
        "latest_tts_text": conversation_state.get("latest_tts_text", "")
    })

@app.route("/clear_tts")
def clear_tts():
    """Clear the latest TTS text after frontend has processed it"""
    conversation_state["latest_tts_text"] = ""
    return jsonify({"status": "cleared"})

app.run(host="0.0.0.0", port=5000)