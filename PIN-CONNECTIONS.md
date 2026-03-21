//This project is under testing therefore PIN CONNECTIONS ARE NOT RECOMMENDED

Components:
MAX98357A - Audio Amplifier x1
INMP441 - MIC x1
ESP32-S3 x1
TFT display ili9341 x1
Buttons x4

PIN connections:
ILI9341 TFT (SPI):
    CS: GPIO5
    DC: GPIO7
    RESET: GPIO6
    MOSI: GPIO11
    SCK: GPIO12
    MISO: GPIO13 (MISO is not needed but good to connect)
SD CARD:
    CS: GPIO10
    MOSI: GPIO11
    SCK: GPIO12
    MISO: GPIO13
OTHER COMPONENTS' CONNECTIONS WOULD BE PUBLISHED AFTER A WHILE