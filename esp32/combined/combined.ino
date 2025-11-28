#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- OLED Display ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- WiFi & server ----------
const char* ssid       = "Motorola";
const char* password   = "hell-nah";
const char* SERVER_URL = "http://10.205.157.83:5000/transcribe";

// ---------- Microphone ----------
const int MIC_PIN       = 34;
const int SAMPLE_RATE   = 8000;   // Hz
const int MAX_SECONDS   = 4;      // safe limit
int MAX_SAMPLES         = SAMPLE_RATE * MAX_SECONDS; 

// dynamic buffer
uint8_t *samples = nullptr;
size_t sampleCount = 0;

unsigned long lastSend = 0;
unsigned long nextSampleTime = 0; 
unsigned long sampleInterval = 1000000 / SAMPLE_RATE; // microseconds between samples

// ---------- Eye animation variables ----------
// Eye positions
int leftEyeX = 45;
int rightEyeX = 80;
int eyeY = 18;
int eyeWidth = 25;
int eyeHeight = 30;

// Offsets
int targetOffsetX = 0;
int targetOffsetY = 0;
int moveSpeed = 5;

// Blinking
bool isBlinking = false;
bool closing = true;
float blinkProgress = 0;        // 0 → 1 (open → closed)
float blinkSpeed = 0.15;        // Faster blinking
unsigned long lastBlinkTrigger = 0;
int blinkDelay = 3500;          // Blinks faster/more frequent
unsigned long lastEyeMove = 0;
unsigned long blinkPauseStart = 0;
bool inBlinkPause = false;

// Eye position interpolation
float offsetX = 0, offsetY = 0;

// Display update timing
unsigned long lastDisplayUpdate = 0;
const int displayInterval = 18; // milliseconds between display updates

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

// ---------- Eye animation functions ----------
void updateEyeAnimation() {
  unsigned long t = millis();

  // Trigger blink
  if (!isBlinking && t - lastBlinkTrigger > blinkDelay) {
    isBlinking = true;
    closing = true;
    blinkProgress = 0;
    lastBlinkTrigger = t;
    inBlinkPause = false;
  }

  // Blink update
  if (isBlinking) {
    if (closing) {
      blinkProgress += blinkSpeed;
      if (blinkProgress >= 1.0) {
        blinkProgress = 1.0;
        closing = false;
        inBlinkPause = true;
        blinkPauseStart = t;
      }
    } else if (inBlinkPause) {
      // Non-blocking pause when fully closed
      if (t - blinkPauseStart >= 40) {
        inBlinkPause = false;
      }
    } else {
      blinkProgress -= blinkSpeed;
      if (blinkProgress <= 0.0) {
        blinkProgress = 0.0;
        isBlinking = false;
      }
    }
  }

  // Eye movement only when NOT blinking
  if (t - lastEyeMove > random(1200, 2500) && !isBlinking) {
    int m = random(0, 8);
    if (m == 0) { targetOffsetX = -10; targetOffsetY = 0; }
    else if (m == 1) { targetOffsetX = 10; targetOffsetY = 0; }
    else if (m == 2) { targetOffsetX = -10; targetOffsetY = -8; }
    else if (m == 3) { targetOffsetX = 10; targetOffsetY = -8; }
    else if (m == 4) { targetOffsetX = -10; targetOffsetY = 8; }
    else if (m == 5) { targetOffsetX = 10; targetOffsetY = 8; }
    else { targetOffsetX = 0; targetOffsetY = 0; }

    lastEyeMove = t;
  }

  // Smooth position interpolation
  offsetX += (targetOffsetX - offsetX) / moveSpeed;
  offsetY += (targetOffsetY - offsetY) / moveSpeed;
}

void drawBlinkEye(int x, int y, int w, int h, float easedBlink) {
  // easedBlink: 0 = open, 1 = closed

  int cover = easedBlink * (h / 2);

  // Base eye shape
  display.fillRoundRect(x, y, w, h, 5, WHITE);

  // Eyelids
  display.fillRect(x, y, w, cover, BLACK);               // top lid
  display.fillRect(x, y + h - cover, w, cover, BLACK);   // bottom lid
}

void renderEyes() {
  display.clearDisplay();

  // Ease-out calculation
  float eased = blinkProgress;
  eased = eased * eased;    // ease-out effect (slows down at the end)

  drawBlinkEye(leftEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, eased);
  drawBlinkEye(rightEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, eased);

  display.display();
}

void setup() {
  Serial.begin(115200);

  // ---------- OLED Display Setup ----------
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.display();
  delay(700);

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
  
  // Initialize timing
  nextSampleTime = micros();
  lastSend = millis();
  lastEyeMove = millis();
  lastDisplayUpdate = millis();
  
  Serial.print("Sample rate: ");
  Serial.print(SAMPLE_RATE);
  Serial.println(" Hz");
  Serial.print("Sample interval: ");
  Serial.print(sampleInterval);
  Serial.println(" microseconds");
  Serial.print("Buffer size: ");
  Serial.print(MAX_SAMPLES);
  Serial.println(" samples");
  
  Serial.println("System ready - Eyes and Microphone active!");
}

void loop() {
  unsigned long currentTime = micros();
  unsigned long currentMillis = millis();
  
  // ---------- Microphone Sampling ----------
  // Sample at precise intervals for true 8kHz rate
  if (currentTime >= nextSampleTime) {
    int raw = analogRead(MIC_PIN);
    
    // Sample continuously at 8kHz into buffer
    if (sampleCount < MAX_SAMPLES) {
      samples[sampleCount++] = map(raw, 0, 4095, 0, 255);
    }
    
    nextSampleTime = currentTime + sampleInterval;
  }

  // ---------- Eye Animation Update (only update display at intervals) ----------
  if (currentMillis - lastDisplayUpdate >= displayInterval) {
    updateEyeAnimation();
    renderEyes();
    lastDisplayUpdate = currentMillis;
  }

  // ---------- Audio Processing ----------
  // Every 4 seconds, send whatever is in buffer
  if (currentMillis - lastSend >= 4000) {
    Serial.print("Sending 4-second chunk... Samples collected: ");
    Serial.print(sampleCount);
    Serial.print(" (Expected: ");
    Serial.print(SAMPLE_RATE * 4);
    Serial.println(")");
    sendWavToServer();
    sampleCount = 0;      // reset buffer after sending
    lastSend = currentMillis;  // reset timer
    nextSampleTime = micros(); // reset sample timing
  }

  // If buffer overflows before 4s, send immediately to avoid loss
  if (sampleCount >= MAX_SAMPLES) {
    Serial.print("Buffer full; sending chunk early... Samples: ");
    Serial.print(sampleCount);
    Serial.print(" Time elapsed: ");
    Serial.print(currentMillis - lastSend);
    Serial.println("ms");
    sendWavToServer();
    sampleCount = 0;
    lastSend = currentMillis;
    nextSampleTime = micros();
  }
}