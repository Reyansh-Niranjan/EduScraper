#include <Arduino.h>
#include <TFT_eSPI.h>
// put function declarations here:
TFT_eSPI tft = TFT_eSPI(); // Invoke library, pins defined in User_Setup.h
void setup() {
  // put your setup code here, to run once:
  tft.init();
  tft.setRotation(1);
  tft.writecommand(0x36);
  tft.writedata(0x40);
  tft.fillScreen(TFT_BLACK);
  tft.setFreeFont(&FreeSansBoldOblique24pt7b);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 50);
  tft.println("Hello World!");
}

void loop() {}
