#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

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

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  delay(700);
}

void loop() {
  unsigned long t = millis();

  // Trigger blink
  if (!isBlinking && t - lastBlinkTrigger > blinkDelay) {
    isBlinking = true;
    closing = true;
    blinkProgress = 0;
    lastBlinkTrigger = t;
  }

  // Blink update
  if (isBlinking) {
    if (closing) {
      blinkProgress += blinkSpeed;
      if (blinkProgress >= 1.0) {
        blinkProgress = 1.0;
        closing = false;
        delay(40);  // short pause fully closed
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
  static unsigned long moveTime = 0;
  if (t - moveTime > random(1200, 2500) && !isBlinking) {
    int m = random(0, 8);
    if (m == 0) { targetOffsetX = -10; targetOffsetY = 0; }
    else if (m == 1) { targetOffsetX = 10; targetOffsetY = 0; }
    else if (m == 2) { targetOffsetX = -10; targetOffsetY = -8; }
    else if (m == 3) { targetOffsetX = 10; targetOffsetY = -8; }
    else if (m == 4) { targetOffsetX = -10; targetOffsetY = 8; }
    else if (m == 5) { targetOffsetX = 10; targetOffsetY = 8; }
    else { targetOffsetX = 0; targetOffsetY = 0; }

    moveTime = t;
  }

  // Smooth position interpolation
  static float offsetX = 0, offsetY = 0;
  offsetX += (targetOffsetX - offsetX) / moveSpeed;
  offsetY += (targetOffsetY - offsetY) / moveSpeed;

  display.clearDisplay();

  // Ease-out calculation
  float eased = blinkProgress;
  eased = eased * eased;    // ease-out effect (slows down at the end)

  drawBlinkEye(leftEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, eased);
  drawBlinkEye(rightEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, eased);

  display.display();
  delay(18);
}

// Draw smooth blinking eye
void drawBlinkEye(int x, int y, int w, int h, float easedBlink) {
  // easedBlink: 0 = open, 1 = closed

  int cover = easedBlink * (h / 2);

  // Base eye shape
  display.fillRoundRect(x, y, w, h, 5, WHITE);

  // Eyelids
  display.fillRect(x, y, w, cover, BLACK);               // top lid
  display.fillRect(x, y + h - cover, w, cover, BLACK);   // bottom lid
}
