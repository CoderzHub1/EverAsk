#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// ------------------------------
// WiFi Credentials
// ------------------------------
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// ------------------------------
// TTS API URL
// ------------------------------
const char* TTS_URL = "http://192.168.0.105:5000/tts?text=Hello%20World";

// ------------------------------
// Audio objects
// ------------------------------
AudioGeneratorMP3 *mp3;
AudioFileSourceHTTPStream *file;
AudioFileSourceBuffer *buff;
AudioOutputI2S *out;

void setup() {
  Serial.begin(115200);
  delay(500);

  // -----------------------------
  // Connect to WiFi
  // -----------------------------
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // -----------------------------
  // Setup Audio Output (I2S in DAC mode)
  // -----------------------------
  out = new AudioOutputI2S();
  
  // Use internal DAC (GPIO25 for left channel)
  out->SetPinout(25, 26, -1);   // BCLK=25, WS=26, DATA not used
  out->SetGain(0.8);            // Volume

  // Enable DAC mode
  out->SetOutputModeMono(true);
  out->SetStereoMode(false);
  out->SetUseDAC(true);         // << THIS ENABLES ESP32 INTERNAL DAC

  // -----------------------------
  // Prepare MP3 Stream
  // -----------------------------
  file = new AudioFileSourceHTTPStream(TTS_URL);
  buff = new AudioFileSourceBuffer(file, 4096);
  mp3 = new AudioGeneratorMP3();

  Serial.println("Starting MP3 playback...");
  mp3->begin(buff, out);
}

void loop() {
  if (mp3->isRunning()) {
    if (!mp3->loop()) {
      mp3->stop();
      Serial.println("Playback finished.");
    }
  }
}


s