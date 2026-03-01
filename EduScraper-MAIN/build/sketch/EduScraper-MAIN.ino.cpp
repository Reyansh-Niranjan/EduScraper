#include <Arduino.h>
#line 1 "C:\\Users\\DELL\\Desktop\\EduScraper-Device\\EduScraper-MAIN\\EduScraper-MAIN.ino"
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000

#include <SPI.h>
#include <TFT_eSPI.h>
#include <SD.h>
#include <TJpg_Decoder.h>

// SD card (shares SPI bus). Adjust CS if needed.
#define SD_MOSI TFT_MOSI
#define SD_MISO TFT_MISO
#define SD_SCK  TFT_SCLK
#define SD_CS   8

// Button pins (wire to GND, use internal pullups)
#define PIN_BTN_PREV 0
#define PIN_BTN_NEXT 1

// Screen constants
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// UI/Slideshow
static const uint32_t SLIDESHOW_INTERVAL_MS = 3000;

TFT_eSPI tft = TFT_eSPI();

// File list of discovered JPEGs
static const size_t MAX_FILES = 1024;
String imgFiles[MAX_FILES];
size_t imgCount = 0;
size_t currentIndex = 0;

// Buttons state
struct ButtonState {
  bool lastStable;
  bool lastRead;
  uint32_t lastChangeMs;
};

ButtonState btnPrev = { true, true, 0 };
ButtonState btnNext = { true, true, 0 };
static const uint32_t DEBOUNCE_MS = 30;
static const uint32_t LONG_PRESS_MS = 800;

bool slideshowEnabled = false;
uint32_t lastSlideMs = 0;

// Forward declarations
void drawSplash(const char *line2);
void scanForJpegs(const char *path);
bool drawJPEG(const char *filename);
void showCurrent(bool clear = true);
void drawHUD(const String &name, size_t index, size_t total);
bool isJpegName(const String &name);
bool readButton(ButtonState &b, int pin, bool &shortPressed, bool &longPressed);
bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);
String ensureLeadingSlash(const String &p);

void setup() {
  // Basic pins
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
  if (TFT_BL >= 0) {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
  }

  // Initialize display
  tft.init();

  // REQUIRED ILI9341 Memory Access Control fix
  tft.writecommand(0x36);
  tft.writedata(0x88);

  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setSwapBytes(true); // for pushImage RGB565 buffers

  // Init JPEG decoder callback
  TJpgDec.setCallback(tftOutput);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);

  drawSplash("Initializing SD...");

  // Initialize shared SPI and SD
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 20000000)) {
    drawSplash("SD init failed");
    delay(1500);
  }

  // Scan for JPEG files
  drawSplash("Scanning JPEGs...");
  imgCount = 0;
  scanForJpegs("/");

  if (imgCount == 0) {
    drawSplash("No JPEGs found");
    delay(1200);
  }

  // Show first image or idle screen
  showCurrent(true);
  lastSlideMs = millis();
}

void loop() {
  bool prevShort = false, prevLong = false;
  bool nextShort = false, nextLong = false;

  readButton(btnPrev, PIN_BTN_PREV, prevShort, prevLong);
  readButton(btnNext, PIN_BTN_NEXT, nextShort, nextLong);

  // Long-press on NEXT toggles slideshow
  if (nextLong) {
    slideshowEnabled = !slideshowEnabled;
    drawHUD(imgCount > 0 ? imgFiles[currentIndex] : String(""), imgCount > 0 ? currentIndex + 1 : 0, imgCount);
    lastSlideMs = millis();
  }

  // Short presses navigate
  if (prevShort && imgCount > 0) {
    slideshowEnabled = false;
    if (currentIndex == 0) currentIndex = imgCount - 1; else currentIndex--;
    showCurrent(true);
  }
  if (nextShort && imgCount > 0) {
    slideshowEnabled = false;
    currentIndex = (currentIndex + 1) % imgCount;
    showCurrent(true);
  }

  // Slideshow advance
  if (slideshowEnabled && imgCount > 0) {
    uint32_t now = millis();
    if (now - lastSlideMs >= SLIDESHOW_INTERVAL_MS) {
      lastSlideMs = now;
      currentIndex = (currentIndex + 1) % imgCount;
      showCurrent(true);
    }
  }
}

void drawSplash(const char *line2) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("JPEG Image Browser", SCREEN_W / 2, SCREEN_H / 2 - 18, 4);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  if (line2 && line2[0]) {
    tft.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 10, 2);
  }
  tft.setTextDatum(TL_DATUM);
}

void drawHUD(const String &name, size_t index, size_t total) {
  // HUD bar at top
  tft.fillRect(0, 0, SCREEN_W, 18, TFT_DARKGREY);
  tft.drawFastHLine(0, 18, SCREEN_W, TFT_NAVY);

  String left = String("[ ") + (slideshowEnabled ? "SLIDE" : "MANUAL") + " ]";
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setCursor(4, 4);
  tft.print(left);

  String right = (total > 0) ? (String(index) + "/" + String(total)) : String("0/0");
  int16_t tw = tft.textWidth(right, 2);
  tft.drawString(right, SCREEN_W - 4 - tw, 2, 2);

  // Filename (trim if too long)
  String base = name;
  int16_t maxw = SCREEN_W - 8;
  while (tft.textWidth(base, 2) > maxw) {
    if (base.length() <= 4) break;
    base.remove(0, 1);
  }
  tft.drawString(base, 4, 20, 2);
}

void showCurrent(bool clear) {
  if (clear) {
    tft.fillScreen(TFT_BLACK);
  }
  if (imgCount == 0) {
    drawHUD("<no jpeg files>", 0, 0);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Place .JPG files on SD", SCREEN_W / 2, SCREEN_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
    return;
  }

  String path = imgFiles[currentIndex];
  drawHUD(path, currentIndex + 1, imgCount);
  if (!drawJPEG(path.c_str())) {
    // Show error placeholder
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Failed to draw JPEG", SCREEN_W / 2, SCREEN_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
  }
}

bool isJpegName(const String &name) {
  int dot = name.lastIndexOf('.');
  if (dot < 0) return false;
  String ext = name.substring(dot + 1);
  ext.toLowerCase();
  return (ext == "jpg" || ext == "jpeg");
}

void scanForJpegs(const char *path) {
  File root = SD.open(path);
  if (!root || !root.isDirectory()) return;
  File entry;
  while ((entry = root.openNextFile())) {
    String name = entry.name();
    if (entry.isDirectory()) {
      scanForJpegs(name.c_str());
    } else if (isJpegName(name)) {
      if (imgCount < MAX_FILES) {
        imgFiles[imgCount++] = name;
      }
    }
    entry.close();
  }
  root.close();
}
bool drawJPEG(const char *filename) {
  // Center the image using its actual size
  int16_t w = 0, h = 0;
  String path = ensureLeadingSlash(String(filename));
  if (!TJpgDec.getFsJpgSize(&w, &h, path.c_str())) {
    // If size query fails, still attempt to draw at safe origin
    w = SCREEN_W; h = SCREEN_H;
  }
  int16_t x = (SCREEN_W - w) / 2;
  int16_t y = (SCREEN_H - h) / 2;
  if (y < 20) y = 20; // avoid HUD overlap
  if (x < 0) x = 0;
  return TJpgDec.drawFsJpg(x, y, path.c_str()) == 1;
}

bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  // Clip blocks above HUD and outside screen
  if (y >= SCREEN_H || x >= SCREEN_W) return 0;
  if (y + h <= 18) return 1; // skip drawing under HUD line
  int16_t drawY = y;
  uint16_t skipRows = 0;
  if (drawY < 18) {
    skipRows = 18 - drawY;
    drawY = 18;
  }
  if (skipRows > 0) {
    bitmap += skipRows * w;
    h -= skipRows;
    if (h == 0) return 1;
  }
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (drawY + h > SCREEN_H) h = SCREEN_H - drawY;
  tft.pushImage(x, drawY, w, h, bitmap);
  return 1;
}

String ensureLeadingSlash(const String &p) {
  if (p.length() == 0) return String("/");
  if (p[0] == '/') return p;
  return String("/") + p;
}

bool readButton(ButtonState &b, int pin, bool &shortPressed, bool &longPressed) {
  shortPressed = false;
  longPressed = false;
  bool reading = digitalRead(pin);
  uint32_t now = millis();

  if (reading != b.lastRead) {
    b.lastChangeMs = now;
    b.lastRead = reading;
  }

  if ((now - b.lastChangeMs) > DEBOUNCE_MS) {
    if (reading != b.lastStable) {
      // State changed
      b.lastStable = reading;
      if (b.lastStable == true) {
        // Released (active-low); decide short vs long
        uint32_t held = now - b.lastChangeMs; // approximate hold duration
        if (held >= LONG_PRESS_MS) longPressed = true; else shortPressed = true;
      }
    }
  }

  return b.lastStable == false; // pressed (active-low)
}

//yeah I need to add wireless features later
