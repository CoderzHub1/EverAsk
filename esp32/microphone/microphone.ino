#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>

// ---------- WiFi & server ----------
const char* ssid       = "Motorola";
const char* password   = "hell-nah";
const char* SERVER_URL = "http://10.205.157.83:5000/transcribe";

// ---------- Microphone ----------
const int MIC_PIN       = 34;
const int SAMPLE_RATE   = 8000;   // Hz
const int MAX_SECONDS   = 5;      // safe limit
int MAX_SAMPLES         = SAMPLE_RATE * MAX_SECONDS; 

// thresholds
// thresholds (not used anymore)
int voiceThreshold   = 2500;
int silenceThreshold = 2250;

// dynamic buffer
uint8_t *samples = nullptr;
size_t sampleCount = 0;

unsigned long lastSend = 0;

// ---------- Little-endian helpers ----------
void write_u32_le(uint8_t *buf, uint32_t value) {
  buf[0] = value & 0xFF;
  buf[1] = (value >> 8) & 0xFF;
  buf[2] = (value >> 16) & 0xFF;
  buf[3] = (value >> 24) & 0xFF;
}

void write_u16_le(uint8_t *buf, uint16_t value) {
  buf[0] = value & 0xFF;
  buf[1] = (value >> 8) & 0xFF;
}

// ---------- Build WAV ----------
void buildWav(std::vector<uint8_t> &wav) {
  const uint16_t numChannels   = 1;
  const uint16_t bitsPerSample = 8;

  uint32_t dataSize = sampleCount;
  uint32_t chunkSize = 36 + dataSize;
  uint32_t byteRate = SAMPLE_RATE * numChannels * bitsPerSample / 8;
  uint16_t blockAlign = numChannels * bitsPerSample / 8;

  wav.resize(44 + dataSize);
  uint8_t *h = wav.data();

  // RIFF Header
  h[0]='R'; h[1]='I'; h[2]='F'; h[3]='F';
  write_u32_le(h+4, chunkSize);
  h[8]='W'; h[9]='A'; h[10]='V'; h[11]='E';

  // fmt chunk
  h[12]='f'; h[13]='m'; h[14]='t'; h[15]=' ';
  write_u32_le(h+16, 16);
  write_u16_le(h+20, 1);
  write_u16_le(h+22, 1);
  write_u32_le(h+24, SAMPLE_RATE);
  write_u32_le(h+28, byteRate);
  write_u16_le(h+32, blockAlign);
  write_u16_le(h+34, bitsPerSample);

  // data chunk
  h[36]='d'; h[37]='a'; h[38]='t'; h[39]='a';
  write_u32_le(h+40, dataSize);

  memcpy(wav.data() + 44, samples, sampleCount);
}

// ---------- Send to Flask ----------
void sendWavToServer() {
  if (sampleCount == 0) {
    Serial.println("No samples recorded.");
    return;
  }

  std::vector<uint8_t> wavData;
  buildWav(wavData);

  Serial.print("Sending WAV of size: ");
  Serial.println(wavData.size());

  HTTPClient http;
  WiFiClient client;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected; skipping send.");
    return;
  }
  if (!http.begin(client, SERVER_URL)) {
    Serial.println("HTTP begin failed.");
    return;
  }
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(15000);
  http.setReuse(false);

  int code = http.POST((uint8_t*)wavData.data(), (size_t)wavData.size());
  Serial.print("Server response code: ");
  Serial.println(code);

  if (code > 0) {
    Serial.println(http.getString());
  } else {
    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  // ---------- PSRAM allocation ----------
  if (psramFound()) {
    Serial.println("PSRAM found. Allocating from PSRAM...");
    samples = (uint8_t*) ps_malloc(MAX_SAMPLES);
  } else {
    Serial.println("No PSRAM. Allocating from normal RAM...");
    samples = (uint8_t*) malloc(MAX_SAMPLES);
  }

  if (!samples) {
    Serial.println("ERROR: Could not allocate buffer!");
    while(true);
  }

  // ---------- WiFi ----------
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  // Configure ADC for better range on ESP32
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_PIN, ADC_11db);
}

void loop() {
  int raw = analogRead(MIC_PIN);

  // Sample continuously at ~8kHz into buffer
  if (sampleCount < MAX_SAMPLES) {
    samples[sampleCount++] = map(raw, 0, 4095, 0, 255);
  }

  // Every 4 seconds, send whatever is in buffer
  if (millis() - lastSend >= 4000) {
    Serial.println("Sending 4-second chunk...");
    sendWavToServer();
    sampleCount = 0;      // reset buffer after sending
    lastSend = millis();  // reset timer
  }

  // If buffer overflows before 4s, send immediately to avoid loss
  if (sampleCount >= MAX_SAMPLES) {
    Serial.println("Buffer full; sending chunk early...");
    sendWavToServer();
    sampleCount = 0;
    lastSend = millis();
  }

  delayMicroseconds(125); // ~8kHz
}
