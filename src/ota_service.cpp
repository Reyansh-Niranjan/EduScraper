#include "ota_service.h"

#include <Arduino.h>
#include <WiFi.h>
#include <SGO_Provisioning.h>
#include "secrets.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifndef FW_VERSION
#define FW_VERSION "0.0.1"
#endif

#ifndef OTA_GITHUB_PAT
#define OTA_GITHUB_PAT ""
#endif

OtaService::OtaService()
    : logCallback(nullptr),
      startupState(STARTUP_IDLE),
      startupSuccess(false),
      wifiAttempt(0),
      wifiAttemptStartMs(0),
      wifiLastProgressMs(0),
      lastProgressPct(0) {}

void OtaService::setLogCallback(OtaLogCallback callback) {
  logCallback = callback;
}

void OtaService::logf(const char *fmt, ...) {
  if (logCallback == nullptr) {
    return;
  }

  char line[160];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logCallback(line);
}

void OtaService::copyBounded(char *dst, size_t dstSize, const char *src) {
  if (dst == nullptr || dstSize == 0) {
    return;
  }

  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }

  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

bool OtaService::credentialsDiffer(const SGO_Credentials &a, const SGO_Credentials &b) {
  return strcmp(a.owner, b.owner) != 0 || strcmp(a.repo, b.repo) != 0 ||
         strcmp(a.pat, b.pat) != 0 || strcmp(a.binFilename, b.binFilename) != 0;
}

bool OtaService::provisionFromSecrets() {
  logf("[BOOT] Loading OTA config");

  SGO_Credentials desired{};
  copyBounded(desired.owner, sizeof(desired.owner), OTA_GITHUB_OWNER);
  copyBounded(desired.repo, sizeof(desired.repo), OTA_GITHUB_REPO);
  copyBounded(desired.pat, sizeof(desired.pat), OTA_GITHUB_PAT);
  copyBounded(desired.binFilename, sizeof(desired.binFilename), OTA_BIN_FILENAME);

  logf("[BOOT] Repo: %s/%s", desired.owner, desired.repo);
  logf("[BOOT] Asset: %s", desired.binFilename);
  logf("[BOOT] PAT: %s", (desired.pat[0] == '\0') ? "empty" : "set");

  if (desired.owner[0] == '\0' || desired.repo[0] == '\0' || desired.binFilename[0] == '\0') {
    logf("[ERR] OTA_GITHUB_* incomplete");
    return false;
  }

  SGO_Credentials current{};
  const bool hasCurrent = SGO_Provisioning::loadCredentials(current);
  if (hasCurrent && !credentialsDiffer(current, desired)) {
    logf("[BOOT] OTA credentials already synced");
    return true;
  }

  if (!SGO_Provisioning::saveCredentials(desired)) {
    logf("[ERR] Failed writing OTA credentials");
    return false;
  }

  logf("[BOOT] OTA credentials saved");
  return true;
}

const char *OtaService::wifiStatusText(wl_status_t status) {
  switch (status) {
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO SSID";
  case WL_SCAN_COMPLETED:
    return "SCAN DONE";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECT FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN";
  }
}

bool OtaService::connectWifiWithTimeout() {
  WiFi.mode(WIFI_STA);

  for (uint8_t attempt = 1; attempt <= WIFI_CONNECT_RETRIES; ++attempt) {
    logf("[WIFI] Attempt %u/%u", attempt, WIFI_CONNECT_RETRIES);
    logf("[WIFI] Connecting to: %s", WIFI_SSID);

    WiFi.disconnect(true);
    delay(150);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t startMs = millis();
    uint32_t lastProgressMs = 0;
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
      delay(250);
      if (millis() - lastProgressMs >= 3000) {
        lastProgressMs = millis();
        logf("[WIFI] waiting... %lus", (millis() - startMs) / 1000UL);
      }
    }

    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }

    logf("[WIFI] Attempt failed: %s (%d)",
         wifiStatusText(WiFi.status()),
         static_cast<int>(WiFi.status()));
  }

  logf("[WIFI] HINT: verify SSID/password and 2.4GHz signal");
  return false;
}

void OtaService::beginSetupAndCheckAsync() {
  ota.setVersion(FW_VERSION);
  ota.setAutoCheckInterval(6 * 60 * 60);
  logf("[OTA] Auto-check: 6 hours");

  ota.onLog([this](const char *message) {
    logf("[OTA] %s", message);
  });

  lastProgressPct = 0;
  ota.onProgress([this](uint32_t written, uint32_t total) {
    if (total == 0) {
      return;
    }

    const uint8_t pct = static_cast<uint8_t>((written * 100U) / total);
    if (pct >= static_cast<uint8_t>(lastProgressPct + 10U) || pct == 100) {
      lastProgressPct = pct;
      logf("[OTA] Progress: %u%%", pct);
    }
  });

  startupSuccess = false;
  wifiAttempt = 0;
  wifiAttemptStartMs = 0;
  wifiLastProgressMs = 0;
  startupState = STARTUP_PROVISION;
}

bool OtaService::stepSetupAndCheckAsync() {
  switch (startupState) {
  case STARTUP_IDLE:
    return false;

  case STARTUP_PROVISION:
    if (!provisionFromSecrets()) {
      logf("[BOOT] Stop: OTA config error");
      startupSuccess = false;
      startupState = STARTUP_FAILED;
      return true;
    }
    WiFi.mode(WIFI_STA);
    startupState = STARTUP_WIFI_START;
    return false;

  case STARTUP_WIFI_START:
    ++wifiAttempt;
    if (wifiAttempt > WIFI_CONNECT_RETRIES) {
      logf("[WIFI] HINT: verify SSID/password and 2.4GHz signal");
      logf("[ERR] Wi-Fi failed, status: %d", WiFi.status());
      startupSuccess = false;
      startupState = STARTUP_FAILED;
      return true;
    }

    logf("[WIFI] Attempt %u/%u", wifiAttempt, WIFI_CONNECT_RETRIES);
    logf("[WIFI] Connecting to: %s", WIFI_SSID);

    WiFi.disconnect(true);
    delay(150);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    wifiAttemptStartMs = millis();
    wifiLastProgressMs = 0;
    startupState = STARTUP_WIFI_WAIT;
    return false;

  case STARTUP_WIFI_WAIT: {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      logf("[WIFI] Connected");
      logf("[WIFI] IP: %s", WiFi.localIP().toString().c_str());
      logf("[WIFI] RSSI: %d dBm", WiFi.RSSI());
      startupState = STARTUP_OTA_BEGIN;
      return false;
    }

    const uint32_t elapsedMs = millis() - wifiAttemptStartMs;
    if (elapsedMs >= WIFI_CONNECT_TIMEOUT_MS) {
      logf("[WIFI] Attempt failed: %s (%d)",
           wifiStatusText(status),
           static_cast<int>(status));
      startupState = STARTUP_WIFI_START;
      return false;
    }

    if ((millis() - wifiLastProgressMs) >= 3000) {
      wifiLastProgressMs = millis();
      logf("[WIFI] waiting... %lus", elapsedMs / 1000UL);
    }
    return false;
  }

  case STARTUP_OTA_BEGIN: {
    logf("[OTA] Initializing");
    const SGO_Error beginErr = ota.begin();
    logf("[OTA] begin(): %d", static_cast<int>(beginErr));
    logf("[OTA] begin msg: %s", ota.getLastError());

    if (ota.wasRolledBack()) {
      logf("[OTA] Rolled back previous FW");
    }

    startupState = STARTUP_OTA_CHECK;
    return false;
  }

  case STARTUP_OTA_CHECK: {
    logf("[OTA] Checking releases");
    const SGO_Error checkErr = ota.checkAndUpdate();
    logf("[OTA] check/update: %d", static_cast<int>(checkErr));
    logf("[OTA] result msg: %s", ota.getLastError());
    logf("[BOOT] Setup complete");

    startupSuccess = true;
    startupState = STARTUP_DONE;
    return true;
  }

  case STARTUP_DONE:
  case STARTUP_FAILED:
    return true;
  }

  return false;
}

bool OtaService::isSetupAndCheckDone() const {
  return startupState == STARTUP_DONE || startupState == STARTUP_FAILED;
}

bool OtaService::isSetupAndCheckSuccessful() const {
  return startupSuccess;
}

bool OtaService::setupAndCheck() {
  beginSetupAndCheckAsync();
  while (!stepSetupAndCheckAsync()) {
    ota.loop();
    delay(20);
  }
  return startupSuccess;
}

void OtaService::loop() {
  ota.loop();
}
