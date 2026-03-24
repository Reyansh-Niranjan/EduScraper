#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.1"
#endif

namespace AppDefs {

// ESP32 filesystem root (/) corresponds to SD card root (D:\ style on PC).
static constexpr const char *SPLASH_IMAGE_PATH = "/assets/R2_Reyansh-LOGO.jpg";

static constexpr uint8_t SD_CS_PIN = 10;
static constexpr int LOG_LINE_HEIGHT = 18;

// 16-bit RGB565 colors.
static constexpr uint16_t SPLASH_BG = 0x0000;
static constexpr uint16_t SPLASH_BAR_BG = 0x7BEF;
static constexpr uint16_t SPLASH_BAR_FG = 0x07FF;
static constexpr uint16_t SPLASH_LOG_COLOR = 0xFFFF;

static constexpr uint32_t SPLASH_MIN_LOADING_MS = 1500;
static constexpr uint32_t SPLASH_LOG_STEP_MS = 280;
static constexpr size_t SPLASH_LOG_QUEUE_SIZE = 36;
static constexpr size_t SPLASH_LOG_TEXT_MAX = 128;

} // namespace AppDefs
