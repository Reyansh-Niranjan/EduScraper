#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SD.h>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include "ota_service.h"

// Required by SafeGithubOTA for TLS + JSON parsing on ESP32 loop task.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#ifndef FW_VERSION
#define FW_VERSION "0.0.1"
#endif

TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h
OtaService ota;

static int logCursorY = 86;
static const int LOG_LINE_HEIGHT = 18;

// ESP32 filesystem root (/) corresponds to SD card root (D:\ style on PC).
static constexpr const char *SPLASH_IMAGE_PATH = "/assets/R2_Reyansh-LOGO.jpg";
static constexpr uint16_t SPLASH_BG = TFT_BLACK;
static constexpr uint16_t SPLASH_BAR_BG = TFT_DARKGREY;
static constexpr uint16_t SPLASH_BAR_FG = TFT_CYAN;
static constexpr uint16_t SPLASH_LOG_COLOR = TFT_WHITE;
static constexpr uint32_t SPLASH_MIN_LOADING_MS = 1500;
static constexpr uint32_t SPLASH_LOG_STEP_MS = 280;
static constexpr size_t SPLASH_LOG_QUEUE_SIZE = 36;
static constexpr size_t SPLASH_LOG_TEXT_MAX = 128;

static int splashImageX = 0;
static int splashImageCenterY = 0;
static uint16_t splashImageW = 0;
static uint16_t splashImageH = 0;
static bool splashImageReady = false;

static char splashLogQueue[SPLASH_LOG_QUEUE_SIZE][SPLASH_LOG_TEXT_MAX];
static size_t splashLogHead = 0;
static size_t splashLogTail = 0;
static size_t splashLogCount = 0;

static void queueSplashLog(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (splashLogCount == SPLASH_LOG_QUEUE_SIZE) {
    splashLogTail = (splashLogTail + 1) % SPLASH_LOG_QUEUE_SIZE;
    splashLogCount--;
  }

  strncpy(splashLogQueue[splashLogHead], line, SPLASH_LOG_TEXT_MAX - 1);
  splashLogQueue[splashLogHead][SPLASH_LOG_TEXT_MAX - 1] = '\0';
  splashLogHead = (splashLogHead + 1) % SPLASH_LOG_QUEUE_SIZE;
  splashLogCount++;
}

static bool popSplashLog(char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return false;
  }

  if (splashLogCount == 0) {
    return false;
  }

  strncpy(out, splashLogQueue[splashLogTail], outSize - 1);
  out[outSize - 1] = '\0';
  splashLogTail = (splashLogTail + 1) % SPLASH_LOG_QUEUE_SIZE;
  splashLogCount--;
  return true;
}

static void otaLogRouter(const char *line) {
  queueSplashLog(line);
}

static bool tftJpgOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (y >= tft.height() || x >= tft.width()) {
    return 0;
  }
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

static uint16_t blend565(uint16_t from, uint16_t to, uint8_t amount) {
  const uint8_t fr = (from >> 11) & 0x1F;
  const uint8_t fg = (from >> 5) & 0x3F;
  const uint8_t fb = from & 0x1F;
  const uint8_t tr = (to >> 11) & 0x1F;
  const uint8_t tg = (to >> 5) & 0x3F;
  const uint8_t tb = to & 0x1F;

  const uint8_t rr = fr + ((tr - fr) * amount) / 255;
  const uint8_t rg = fg + ((tg - fg) * amount) / 255;
  const uint8_t rb = fb + ((tb - fb) * amount) / 255;
  return static_cast<uint16_t>((rr << 11) | (rg << 5) | rb);
}

static void splashDrawImageAt(int y) {
  if (splashImageReady) {
    TJpgDec.drawSdJpg(splashImageX, y, SPLASH_IMAGE_PATH);
    return;
  }

  const int fallbackW = 180;
  const int fallbackH = 90;
  const int x = (tft.width() - fallbackW) / 2;
  tft.fillRoundRect(x, y, fallbackW, fallbackH, 10, TFT_DARKGREY);
  tft.drawRoundRect(x, y, fallbackW, fallbackH, 10, TFT_WHITE);
  tft.setTextColor(TFT_WHITE, SPLASH_BG);
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setCursor(x + 16, y + (fallbackH / 2));
  tft.print("Logo missing");
}

static void splashDrawFrame(int imageY,
                            uint8_t progress,
                            const char *logLine,
                            bool showLoading,
                            uint8_t fade) {
  tft.fillScreen(SPLASH_BG);

  tft.setTextColor(blend565(TFT_LIGHTGREY, SPLASH_BG, fade), SPLASH_BG);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setCursor((tft.width() / 2) - 36, 26);
  tft.print("Made By");

  splashDrawImageAt(imageY);

  if (!showLoading) {
    return;
  }

  const int barW = tft.width() - 40;
  const int barH = 12;
  const int barX = 20;
  const int barY = tft.height() - 72;
  const uint16_t bg = blend565(SPLASH_BAR_BG, SPLASH_BG, fade);
  const uint16_t fg = blend565(SPLASH_BAR_FG, SPLASH_BG, fade);
  const uint16_t border = blend565(TFT_WHITE, SPLASH_BG, fade);

  tft.fillRoundRect(barX, barY, barW, barH, 6, bg);
  const int fillW = (barW * progress) / 100;
  if (fillW > 0) {
    tft.fillRoundRect(barX, barY, fillW, barH, 6, fg);
  }
  tft.drawRoundRect(barX - 1, barY - 1, barW + 2, barH + 2, 7, border);

  if (logLine != nullptr) {
    tft.setTextColor(blend565(SPLASH_LOG_COLOR, SPLASH_BG, fade), SPLASH_BG);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setCursor(14, barY + 34);
    tft.print(logLine);
  }
}

static void runSplashSequence() {
  tft.fillScreen(SPLASH_BG);

  if (SD.begin()) {
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tftJpgOutput);
    splashImageReady = TJpgDec.getSdJpgSize(&splashImageW, &splashImageH, SPLASH_IMAGE_PATH);
  } else {
    splashImageReady = false;
  }

  if (!splashImageReady) {
    splashImageW = 180;
    splashImageH = 90;
  }

  splashImageX = (tft.width() - splashImageW) / 2;
  splashImageCenterY = (tft.height() - splashImageH) / 2 - 10;

  splashDrawFrame(splashImageCenterY, 0, nullptr, false, 0);
  delay(500);

  const int upTargetY = splashImageCenterY - 26;
  for (int i = 0; i <= 14; ++i) {
    const float t = static_cast<float>(i) / 14.0f;
    const float eased = t * t * (3.0f - 2.0f * t);
    const int y = splashImageCenterY - static_cast<int>((splashImageCenterY - upTargetY) * eased);
    splashDrawFrame(y, 0, nullptr, false, 0);
    delay(26);
  }

  char currentLog[SPLASH_LOG_TEXT_MAX] = "[BOOT] Starting OTA";
  uint8_t progress = 4;
  const uint32_t loadStartMs = millis();
  uint32_t lastLogDrawMs = 0;

  while (!ota.isSetupAndCheckDone() || (millis() - loadStartMs) < SPLASH_MIN_LOADING_MS) {
    ota.stepSetupAndCheckAsync();
    ota.loop();

    char nextLog[SPLASH_LOG_TEXT_MAX];
    if (popSplashLog(nextLog, sizeof(nextLog))) {
      strncpy(currentLog, nextLog, sizeof(currentLog) - 1);
      currentLog[sizeof(currentLog) - 1] = '\0';
    }

    if (millis() - lastLogDrawMs >= SPLASH_LOG_STEP_MS) {
      lastLogDrawMs = millis();
      if (progress < 92) {
        progress = static_cast<uint8_t>(progress + 3);
      }
      splashDrawFrame(upTargetY, progress, currentLog, true, 0);
    }

    delay(20);
  }

  if (ota.isSetupAndCheckSuccessful()) {
    strncpy(currentLog, "[BOOT] OTA ready", sizeof(currentLog) - 1);
  } else {
    strncpy(currentLog, "[BOOT] OTA setup failed", sizeof(currentLog) - 1);
  }
  currentLog[sizeof(currentLog) - 1] = '\0';
  splashDrawFrame(upTargetY, 100, currentLog, true, 0);
  delay(350);

  for (int i = 0; i <= 8; ++i) {
    const uint8_t fade = static_cast<uint8_t>((i * 255) / 8);
    splashDrawFrame(upTargetY, 100, currentLog, true, fade);
    delay(45);
  }

  for (int i = 0; i <= 14; ++i) {
    const float t = static_cast<float>(i) / 14.0f;
    const float eased = t * t * (3.0f - 2.0f * t);
    const int y = upTargetY + static_cast<int>((splashImageCenterY - upTargetY) * eased);
    splashDrawFrame(y, 100, nullptr, false, 255);
    delay(24);
  }

  tft.fillScreen(TFT_BLACK);
}

static void tftInitUi() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextWrap(true, false);

  // Bold, non-italic FreeFont for a readable status header.
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(0, 24);
  tft.println("EduScraper OTA Test");

  tft.drawFastHLine(0, 34, tft.width(), TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 52);
  tft.println("Live logs:");

  tft.setFreeFont(&FreeSansBold9pt7b);
  logCursorY = 86;
}

static void tftLogLine(const char *line) {
  if (line == nullptr) {
    return;
  }

  if (logCursorY > tft.height() - 8) {
    tft.fillRect(0, 70, tft.width(), tft.height() - 70, TFT_BLACK);
    logCursorY = 86;
    tft.setCursor(0, logCursorY);
    tft.print("...");
    logCursorY += LOG_LINE_HEIGHT;
  }

  tft.setCursor(0, logCursorY);
  tft.println(line);
  logCursorY += LOG_LINE_HEIGHT;
}

static void tftLogf(const char *fmt, ...) {
  char line[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  tftLogLine(line);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Begin OTA startup before splash; progress is advanced cooperatively during loading.
  ota.setLogCallback(otaLogRouter);
  queueSplashLog("[BOOT] OTA startup queued");
  ota.beginSetupAndCheckAsync();

  tft.init();
  tft.setRotation(1);
  tft.writecommand(0x36);
  tft.writedata(0x40);
  tft.setTextWrap(true, false);

  runSplashSequence();
  tftInitUi();
  tftLogf("[BOOT] FW version: %s", FW_VERSION);

  char pendingLog[SPLASH_LOG_TEXT_MAX];
  while (popSplashLog(pendingLog, sizeof(pendingLog))) {
    tftLogLine(pendingLog);
  }

  if (!ota.isSetupAndCheckSuccessful()) {
    tftLogLine("[BOOT] OTA init did not complete successfully");
  }

  ota.setLogCallback(tftLogLine);
}

void loop() {
  ota.loop();
  delay(50);
}
