#pragma once

#include <SafeGithubOTA.h>
#include <WiFi.h>
#include <SGO_Provisioning.h>

typedef void (*OtaLogCallback)(const char *line);

class OtaService {
public:
  OtaService();

  void setLogCallback(OtaLogCallback callback);
  void beginSetupAndCheckAsync();
  bool stepSetupAndCheckAsync();
  bool isSetupAndCheckDone() const;
  bool isSetupAndCheckSuccessful() const;
  bool setupAndCheck();
  void loop();

private:
  static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
  static const uint8_t WIFI_CONNECT_RETRIES = 2;

  enum StartupState {
    STARTUP_IDLE,
    STARTUP_PROVISION,
    STARTUP_WIFI_START,
    STARTUP_WIFI_WAIT,
    STARTUP_OTA_BEGIN,
    STARTUP_OTA_CHECK,
    STARTUP_DONE,
    STARTUP_FAILED
  };

  SafeGithubOTA ota;
  OtaLogCallback logCallback;
  StartupState startupState;
  bool startupSuccess;
  uint8_t wifiAttempt;
  uint32_t wifiAttemptStartMs;
  uint32_t wifiLastProgressMs;
  uint8_t lastProgressPct;

  void logf(const char *fmt, ...);
  static void copyBounded(char *dst, size_t dstSize, const char *src);
  static bool credentialsDiffer(const SGO_Credentials &a, const SGO_Credentials &b);
  bool provisionFromSecrets();
  bool connectWifiWithTimeout();
  static const char *wifiStatusText(wl_status_t status);
};
