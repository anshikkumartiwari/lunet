/*
  =============================================================================
  VISTEON LUNET OS 3.0 - Automotive Smart Digital Cluster
  Hardware: Waveshare ESP32-S3 Touch LCD 4.3" (800x480 ST7262 RGB, GT911 Touch)
  Stack:    ESP32 Core 3.3.13 | LVGL 8.4.0 | ESP32_Display_Panel 1.0.0
  =============================================================================
*/

#define LV_CONF_INCLUDE_SIMPLE
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <esp_gap_ble_api.h>
#endif
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace esp_panel::board;
using namespace esp_panel::drivers;

// -----------------------------------------------------------------------------
// Safe Font Fallbacks (Compiles cleanly on any lv_conf.h)
// -----------------------------------------------------------------------------
#if LV_FONT_MONTSERRAT_14
  #define FONT_SM (&lv_font_montserrat_14)
#else
  #define FONT_SM (LV_FONT_DEFAULT)
#endif

#if LV_FONT_MONTSERRAT_12
  #define FONT_XS (&lv_font_montserrat_12)
#else
  #define FONT_XS FONT_SM
#endif

#if LV_FONT_MONTSERRAT_16
  #define FONT_MD (&lv_font_montserrat_16)
#else
  #define FONT_MD FONT_SM
#endif

#if LV_FONT_MONTSERRAT_20
  #define FONT_LG (&lv_font_montserrat_20)
#else
  #define FONT_LG FONT_MD
#endif

#if LV_FONT_MONTSERRAT_28
  #define FONT_XL (&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
  #define FONT_XL (&lv_font_montserrat_24)
#else
  #define FONT_XL FONT_LG
#endif

#if LV_FONT_MONTSERRAT_48
  #define FONT_HERO (&lv_font_montserrat_48)
#elif LV_FONT_MONTSERRAT_40
  #define FONT_HERO (&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_36
  #define FONT_HERO (&lv_font_montserrat_36)
#elif LV_FONT_MONTSERRAT_32
  #define FONT_HERO (&lv_font_montserrat_32)
#else
  #define FONT_HERO FONT_XL
#endif

// -----------------------------------------------------------------------------
// Safe Math, Formatting & Icon Helpers
// -----------------------------------------------------------------------------
static inline int clampInt(int v, int lo, int hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline float clampFloat(float v, float lo, float hi) {
  return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static void formatDistance(char *buf, size_t len, int meters) {
  if (meters >= 1000) {
    snprintf(buf, len, "%.1f km", meters / 1000.0f);
  } else {
    snprintf(buf, len, "%d m", meters);
  }
}

static void formatEta(char *buf, size_t len, int mins) {
  if (mins >= 60) {
    snprintf(buf, len, "%dh %dm", mins / 60, mins % 60);
  } else {
    snprintf(buf, len, "%d min", mins);
  }
}

static const char *batterySymbolForPct(int pct) {
  if (pct >= 85) return LV_SYMBOL_BATTERY_FULL;
  if (pct >= 60) return LV_SYMBOL_BATTERY_3;
  if (pct >= 35) return LV_SYMBOL_BATTERY_2;
  if (pct >= 12) return LV_SYMBOL_BATTERY_1;
  return LV_SYMBOL_BATTERY_EMPTY;
}

static const char *wmoCodeToText(int code) {
  if (code == 0) return "Clear";
  if (code == 1) return "Fair";
  if (code == 2) return "Cloudy";
  if (code == 3) return "Overcast";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow";
  if (code >= 95 && code <= 99) return "Storm";
  return "Clear";
}

static void setLabelIfChanged(lv_obj_t *lbl, const char *text) {
  if (!lbl || !text) return;
  const char *cur = lv_label_get_text(lbl);
  if (!cur || strcmp(cur, text) != 0) {
    lv_label_set_text(lbl, text);
  }
}

// Lightweight JSON extractors (zero external dependencies)
static bool jsonExtractFloat(const char *json, const char *key, float *outVal) {
  if (!json || !key || !outVal) return false;
  const char *p = strstr(json, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  char *endPtr = nullptr;
  float v = strtof(p, &endPtr);
  if (endPtr == p) return false;
  *outVal = v;
  return true;
}

static bool jsonExtractInt(const char *json, const char *key, int *outVal) {
  if (!json || !key || !outVal) return false;
  const char *p = strstr(json, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  char *endPtr = nullptr;
  long v = strtol(p, &endPtr, 10);
  if (endPtr == p) return false;
  *outVal = (int)v;
  return true;
}

static bool jsonExtractString(const char *json, const char *key, char *outBuf, size_t maxLen) {
  if (!json || !key || !outBuf || maxLen < 2) return false;
  const char *p = strstr(json, key);
  if (!p) return false;
  p += strlen(key);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != '"') return false;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i < maxLen - 1) {
    if (*p == '\\' && *(p + 1)) p++;
    outBuf[i++] = *p++;
  }
  outBuf[i] = '\0';
  return (i > 0);
}

// -----------------------------------------------------------------------------
// Shared BLE 5.0 Protocol & Default Network Constants
// -----------------------------------------------------------------------------
static const char *BLE_DEVICE_NAME  = "Visteon-Lunet";
static const char *BLE_SERVICE_UUID = "7b5c0001-7b7c-4f2a-9a6d-2c0c0e4a1000";
static const char *BLE_CHAR_UUID    = "7b5c0002-7b7c-4f2a-9a6d-2c0c0e4a1000";

static const char *SAVED_SSID       = "Anshik";
static const char *SAVED_PASSWORD   = "anshik.ti";

static constexpr float DEFAULT_LAT           = 13.110721f;
static constexpr float DEFAULT_LON           = 80.245900f;
static constexpr int   DEFAULT_UTC_OFFSET_S  = 19800; // IST (+05:30)
static constexpr uint32_t WEATHER_REFRESH_MS = 15UL * 60UL * 1000UL; // 15 min

// -----------------------------------------------------------------------------
// Design System Palette (Obsidian Dark / Ceramic Light + Visteon Orange)
// -----------------------------------------------------------------------------
#define VISTEON_ORANGE 0xF37B20

struct ThemePalette {
  uint32_t bg;
  uint32_t surface;
  uint32_t surfaceAlt;
  uint32_t surfaceDeep;
  uint32_t border;
  uint32_t text;
  uint32_t muted;
  uint32_t cyan;
  uint32_t green;
  uint32_t amber;
  uint32_t red;
  uint32_t road;
};

static const ThemePalette THEME_DARK = {
  0x06090F, 0x0E1521, 0x162133, 0x0A1019, 0x22324A,
  0xF4F7FB, 0x8094AD, 0x00D4FF, 0x10B981, 0xF59E0B, 0xEF4444, 0x131C2B
};

static const ThemePalette THEME_LIGHT = {
  0xEBF0F6, 0xFFFFFF, 0xDCE5F0, 0xE2E9F2, 0xC4D2E3,
  0x0F172A, 0x5B6E84, 0x0284C7, 0x059669, 0xD97706, 0xDC2626, 0xCBD5E1
};

static bool lightTheme = false;
#define pal() (lightTheme ? THEME_LIGHT : THEME_DARK)

// -----------------------------------------------------------------------------
// Hardware & Core Handles
// -----------------------------------------------------------------------------
static Board *board = nullptr;
static LCD   *lcd   = nullptr;
static Touch *touch = nullptr;
static bool   touchReady = false;

static lv_disp_draw_buf_t drawBuffer;
static lv_color_t *drawBuffer1 = nullptr;
static lv_color_t *drawBuffer2 = nullptr;
static esp_timer_handle_t lvglTickTimer = nullptr;

// BLE State & Thread-Safe 24-Slot Ring Queue
static bool bleReady = false;
static volatile bool bleConnected = false;
static volatile bool bleRestartPending = false;
static volatile bool bleSyncReqPending = false;
static volatile uint16_t bleActiveConnId = 0;
static volatile bool blePeerAddrValid = false;
static uint8_t blePeerAddr[6] = {0};
static uint32_t bleDisconnectAt = 0;
static uint32_t bleConnectedAt  = 0;
static BLEServer         *bleServer         = nullptr;
static BLECharacteristic *bleCharacteristic = nullptr;

static constexpr uint8_t BLE_QUEUE_SIZE = 24;
static constexpr size_t  BLE_MSG_MAX    = 160;
static portMUX_TYPE bleQueueMux = portMUX_INITIALIZER_UNLOCKED;
static char bleQueue[BLE_QUEUE_SIZE][BLE_MSG_MAX];
static volatile uint8_t bleQHead  = 0;
static volatile uint8_t bleQTail  = 0;
static volatile uint8_t bleQCount = 0;

// Wi-Fi, NTP & Standalone Open-Meteo / IP-Geo Background Sync State
static bool wifiScanInProgress = false;
static bool ntpConfigured      = false;
static bool bleTimeOverride    = false;
static char activeWifiName[33] = "";

struct WifiEntry {
  char ssid[33];
  int  rssi;
  bool open;
};
static WifiEntry scannedWifi[16];
static uint8_t scannedWifiCount = 0;
static char pendingWifiSsid[33] = "";

struct NetSyncData {
  bool  weatherOk;
  bool  locationOk;
  float lat;
  float lon;
  int   utcOffsetSec;
  float tempC;
  int   weatherCode;
  char  city[28];
  char  weatherStr[32];
  char  isoTime[24];
};

static portMUX_TYPE netSyncMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool netSyncRunning    = false;
static volatile bool netSyncResultReady = false;
static NetSyncData   netSyncResult     = {};
static bool          weatherEverSynced = false;
static uint32_t      lastWeatherSyncMs = 0;
static bool          manualLocOverride = false;

// -----------------------------------------------------------------------------
// Automotive & Infotainment State (Boots in PARK, 0 km/h, DEMO OFF)
// -----------------------------------------------------------------------------
enum PageId : uint8_t {
  PAGE_DRIVE = 0,
  PAGE_NAV,
  PAGE_MEDIA,
  PAGE_VEHICLE,
  PAGE_CONNECT,
  PAGE_SYSTEM,
  PAGE_COUNT
};
static PageId currentPage = PAGE_DRIVE;

enum DriveMode : uint8_t { MODE_ECO = 0, MODE_COMFORT = 1, MODE_SPORT = 2 };
enum NavArrow  : uint8_t { NAV_STRAIGHT = 0, NAV_RIGHT = 1, NAV_LEFT = 2, NAV_UTURN = 3 };

struct NotifItem {
  char app[20];
  char msg[88];
};
static constexpr uint8_t MAX_NOTIFS = 5;

struct VehicleState {
  // Powertrain & Dynamics (Safe Boot Defaults)
  float speedKmh       = 0.0f;
  float targetSpeedKmh = 0.0f;
  float powerKw        = 0.0f;
  int   speedLimitKmh  = 80;
  char  gear           = 'P';   // Default: PARK
  DriveMode mode       = MODE_COMFORT;
  bool  useMph         = false;
  bool  demoDrive      = false; // Default: DEMO OFF

  // EV Energy & Trip
  float socPercent     = 84.0f;
  float tripKm         = 48.6f;
  float odoKm          = 12480.4f;
  float efficiencyKwh  = 14.2f;
  float hvVoltage      = 398.4f;
  float hvTempC        = 31.0f;
  int   regenLevel     = 1;

  // ADAS & Signals
  bool  leftBlinker    = false;
  bool  rightBlinker   = false;
  bool  hazard         = false;
  bool  braking        = false;
  bool  lkaActive      = true;
  bool  accActive      = true;
  float leadDistM      = 42.0f;
  float roadPhase      = 0.0f;
  bool  adasDirty      = true;

  // Body & TPMS (PSI)
  float tpms[4]        = {34.2f, 34.4f, 33.8f, 34.1f};
  bool  headlightsOn   = true;
  bool  doorsLocked    = true;
  bool  trunkOpen      = false;

  // Dual-Zone HVAC
  int   tempDriver     = 21;
  int   tempPassenger  = 22;
  int   fanSpeed       = 3;
  bool  acOn           = true;
  bool  autoClimate    = true;
  bool  defrostOn      = false;
  int   seatHeat       = 0;

  // Location, Time, Weather & Power
  float latitude       = DEFAULT_LAT;
  float longitude      = DEFAULT_LON;
  int   utcOffsetSec   = DEFAULT_UTC_OFFSET_S;
  char  cityStr[28]    = "Chennai";
  char  timeStr[16]    = "--:--";
  char  dateStr[24]    = "Syncing Time";
  char  weatherStr[32] = "Weather --";
  int   supplyBattery  = 100;   // Direct power supply = 100%
  int   phoneBattery   = -1;    // Synced via PBATT:<0-100>
  char  alertText[76]  = "";
  uint32_t alertColor  = VISTEON_ORANGE;
  uint32_t alertUntil  = 0;

  // Live Google Maps Navigation
  bool  navActive      = false;
  NavArrow navDir      = NAV_STRAIGHT;
  int   navDistM       = 0;
  char  navStreet[64]  = "Start a route in Google Maps on phone";
  char  navDest[44]    = "Waiting for Maps";
  int   navEtaMin      = 0;
  float navRemKm       = 0.0f;
  int   navRoutePct    = 0;
  bool  navDirty       = true;

  // Phone Call & 5-Item Recent Notification Feed
  bool  callActive     = false;
  bool  callAnswered   = false;
  bool  callSpeaker    = false;
  char  callerName[48] = "No Active Call";
  bool  callDirty      = true;
  NotifItem notifs[MAX_NOTIFS];
  uint8_t notifCount   = 0;
  bool  notifDirty     = true;

  // Live Phone Media
  char  mediaTitle[56] = "No Media Playing";
  char  mediaArtist[44]= "Play music on phone";
  bool  mediaPlaying   = false;
  bool  liveMediaSynced= false;
  uint32_t lastMediaRxMs = 0;
  int   mediaProgress  = 0;
  int   mediaVolume    = 70;
  bool  mediaMuted     = false;
  int   eqPreset       = 0;
  float eqBars[12]     = {8,8,8,8,8,8,8,8,8,8,8,8};
  float vinylAngle     = 0.0f;

  // Touch Tracker
  int   lastTouchX     = 400;
  int   lastTouchY     = 240;
} vs;

static char bleLogLines[8][82];
static uint8_t bleLogCount = 0;
static bool bleLogDirty = true;

// -----------------------------------------------------------------------------
// Persistent Shell & Page Widget Pointers
// -----------------------------------------------------------------------------
static lv_obj_t *bootOverlay      = nullptr;
static lv_obj_t *bootBar          = nullptr;
static bool      bootActive       = false;
static uint32_t  bootStartMs      = 0;

static lv_obj_t *overspeedOverlay = nullptr;

static lv_obj_t *topBar           = nullptr;
static lv_obj_t *dynamicPill      = nullptr;
static lv_obj_t *dynamicPillLabel = nullptr;
static lv_obj_t *topWifiIcon      = nullptr;
static lv_obj_t *topBleIcon       = nullptr;
static lv_obj_t *topSupplyLabel   = nullptr;
static lv_obj_t *pageContainer    = nullptr;
static lv_obj_t *dockBtns[PAGE_COUNT]       = {nullptr};
static lv_obj_t *dockIndicators[PAGE_COUNT] = {nullptr};

// Wi-Fi Keyboard Modal
static lv_obj_t *kbModal          = nullptr;
static lv_obj_t *kbTextArea       = nullptr;

// Page 1: DRIVE pointers
static lv_obj_t *drvModeBadge     = nullptr;
static lv_obj_t *drvSpeedLimitBox = nullptr;
static lv_obj_t *drvSpeedLimitLbl = nullptr;
static lv_obj_t *drvSpeedArc      = nullptr;
static lv_obj_t *drvPowerArc      = nullptr;
static lv_obj_t *drvSpeedVal      = nullptr;
static lv_obj_t *drvSpeedUnit     = nullptr;
static lv_obj_t *drvPowerVal      = nullptr;
static lv_obj_t *drvThrottleSld   = nullptr;
static lv_obj_t *drvGearBtns[4]   = {nullptr};
static lv_obj_t *drvModeBtns[3]   = {nullptr};
static lv_obj_t *drvAdasCanvas    = nullptr;
static lv_obj_t *drvLeftBtn       = nullptr;
static lv_obj_t *drvRightBtn      = nullptr;
static lv_obj_t *drvHazardBtn     = nullptr;
static lv_obj_t *drvDemoBtn       = nullptr;
static lv_obj_t *drvAdasFooter    = nullptr;
static lv_obj_t *drvNavArrowBox   = nullptr;
static lv_obj_t *drvNavDistLbl    = nullptr;
static lv_obj_t *drvNavStreetLbl  = nullptr;
static lv_obj_t *drvNavEtaLbl     = nullptr;
static lv_obj_t *drvMediaTitle    = nullptr;
static lv_obj_t *drvMediaArtist   = nullptr;
static lv_obj_t *drvMediaProg     = nullptr;
static lv_obj_t *drvMediaPlayLbl  = nullptr;
static lv_obj_t *drvSocLbl        = nullptr;
static lv_obj_t *drvSocBar        = nullptr;
static lv_obj_t *drvEffLbl        = nullptr;

// Page 2: NAV / PHONE pointers
static lv_obj_t *navStatusBadge   = nullptr;
static lv_obj_t *navStatusLbl     = nullptr;
static lv_obj_t *navBigArrowBox   = nullptr;
static lv_obj_t *navBigDistLbl    = nullptr;
static lv_obj_t *navBigActionLbl  = nullptr;
static lv_obj_t *navBigStreetLbl  = nullptr;
static lv_obj_t *navProgPctLbl    = nullptr;
static lv_obj_t *navProgBar       = nullptr;
static lv_obj_t *navEtaValLbl     = nullptr;
static lv_obj_t *navEtaSubLbl     = nullptr;
static lv_obj_t *navRemValLbl     = nullptr;
static lv_obj_t *navRemSubLbl     = nullptr;
static lv_obj_t *navDestValLbl    = nullptr;

static lv_obj_t *navCallCard      = nullptr;
static lv_obj_t *navCallStatusLbl = nullptr;
static lv_obj_t *navCallerLbl     = nullptr;
static lv_obj_t *navCallSubLbl    = nullptr;
static lv_obj_t *navPhoneBattLbl  = nullptr;
static lv_obj_t *navAcceptBtn     = nullptr;
static lv_obj_t *navSpeakerBtn    = nullptr;
static lv_obj_t *navSpeakerBtnLbl = nullptr;
static lv_obj_t *navEndBtn        = nullptr;
static lv_obj_t *navNotifCountLbl = nullptr;
static lv_obj_t *navNotifList     = nullptr;

// Page 3: MEDIA pointers
static lv_obj_t *medDiscCanvas    = nullptr;
static lv_obj_t *medEqCanvas      = nullptr;
static lv_obj_t *medTitleLbl      = nullptr;
static lv_obj_t *medArtistLbl     = nullptr;
static lv_obj_t *medProgSlider    = nullptr;
static lv_obj_t *medTimeLbl       = nullptr;
static lv_obj_t *medPlayBtnLbl    = nullptr;
static lv_obj_t *medVolSlider     = nullptr;
static lv_obj_t *medVolLbl        = nullptr;
static lv_obj_t *medEqBtns[4]     = {nullptr};

// Page 4: VEHICLE pointers
static lv_obj_t *vehChassisCanvas = nullptr;
static lv_obj_t *vehTpmsLbl[4]    = {nullptr};
static lv_obj_t *vehLightBtn      = nullptr;
static lv_obj_t *vehLockBtn       = nullptr;
static lv_obj_t *vehTrunkBtn      = nullptr;
static lv_obj_t *vehDrvTempLbl    = nullptr;
static lv_obj_t *vehPasTempLbl    = nullptr;
static lv_obj_t *vehFanLbl        = nullptr;
static lv_obj_t *vehAcBtn         = nullptr;
static lv_obj_t *vehAutoBtn       = nullptr;
static lv_obj_t *vehDefrostBtn    = nullptr;
static lv_obj_t *vehSeatBtn       = nullptr;
static lv_obj_t *vehHvStatsLbl    = nullptr;
static lv_obj_t *vehRegenBtn      = nullptr;

// Page 5: CONNECT pointers
static lv_obj_t *conWifiStatusLbl = nullptr;
static lv_obj_t *conWifiDetailLbl = nullptr;
static lv_obj_t *conWifiList      = nullptr;
static lv_obj_t *conBleStatusLbl  = nullptr;
static lv_obj_t *conBlePhoneLbl   = nullptr;
static lv_obj_t *conBleLogLbl     = nullptr;

// Page 6: SYSTEM pointers
#if LV_USE_CHART
static lv_obj_t *sysChart         = nullptr;
static lv_chart_series_t *serSpeed = nullptr;
static lv_chart_series_t *serPower = nullptr;
#endif
static lv_obj_t *sysMemLbl        = nullptr;
static lv_obj_t *sysTouchLbl      = nullptr;
static lv_obj_t *sysTouchDot      = nullptr;
static lv_obj_t *sysUnitBtnLbl    = nullptr;

// Timing & UI Diff Trackers
static uint32_t lastPhysicsMs     = 0;
static uint32_t lastSlowUiMs      = 0;
static uint32_t lastChartMs       = 0;
static uint32_t lastVolTxMs       = 0;
static bool     blinkState        = false;
static uint32_t lastBlinkMs       = 0;
static bool     isOverspeed       = false;

static int      lastShownSpeed    = -1;
static int      lastShownPower    = -999;
static bool     lastOverspeedVis  = false;
static bool     lastBlinkVis      = false;
static uint32_t lastPillBgHex     = 0xFFFFFFFF;
static uint32_t lastPillTxtHex    = 0xFFFFFFFF;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
static void buildShell();
static void selectPage(PageId page);
static void showBootSplash();
static void sendBlePacket(const char *packet);
static void appendBleLog(const char *prefix, const char *msg);
static void triggerAlert(const char *text, uint32_t colorHex, uint32_t durationMs = 4000);
static void pushNotification(const char *appName, const char *message, bool flashBanner);
static void clearNotifications();
static void rebuildNotifList();
static void refreshNavHudCard();
static void refreshNavCallCard();
static void startWifiConnection(const char *ssid, const char *password);
static void scanWifiNetworks();
static void updateWifiScanResults();
static void restartBleAdvertising();
static void triggerStandaloneWeatherSync();
static void applyStandaloneNetSyncResult();

static inline uint32_t modeAccentColor() {
  if (vs.mode == MODE_ECO) return pal().green;
  if (vs.mode == MODE_SPORT) return VISTEON_ORANGE;
  return pal().cyan;
}

static inline int displaySpeed() {
  return (int)lroundf(vs.useMph ? (vs.speedKmh * 0.621371f) : vs.speedKmh);
}

static inline int displayLimit() {
  return (int)lroundf(vs.useMph ? (vs.speedLimitKmh * 0.621371f) : (float)vs.speedLimitKmh);
}

// -----------------------------------------------------------------------------
// Standalone Wi-Fi Location + Open-Meteo Weather + Timezone Sync (FreeRTOS)
// -----------------------------------------------------------------------------
static void netWeatherTask(void *param) {
  NetSyncData local = {};
  local.lat          = vs.latitude;
  local.lon          = vs.longitude;
  local.utcOffsetSec = vs.utcOffsetSec;
  strncpy(local.city, vs.cityStr, sizeof(local.city) - 1);

  // Step 1: Auto-detect lat/lon/city/timezone via IP geolocation (unless phone sent exact GPS)
  if (!manualLocOverride && WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false);
    if (http.begin("http://ip-api.com/json/?fields=status,lat,lon,city,offset")) {
      int code = http.GET();
      if (code == HTTP_CODE_OK) {
        String body = http.getString();
        const char *js = body.c_str();
        float latVal = 0.0f, lonVal = 0.0f;
        int offVal = 0;
        if (jsonExtractFloat(js, "\"lat\":", &latVal) &&
            jsonExtractFloat(js, "\"lon\":", &lonVal)) {
          local.lat = latVal;
          local.lon = lonVal;
          local.locationOk = true;
        }
        if (jsonExtractInt(js, "\"offset\":", &offVal)) {
          local.utcOffsetSec = offVal;
        }
        char cityBuf[28] = "";
        if (jsonExtractString(js, "\"city\":", cityBuf, sizeof(cityBuf))) {
          strncpy(local.city, cityBuf, sizeof(local.city) - 1);
          local.city[sizeof(local.city) - 1] = '\0';
        }
      }
      http.end();
    }
  }

  // Step 2: Fetch live weather & timezone from Open-Meteo
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(7);

    char url[200];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
             "&current=temperature_2m,weather_code&timezone=auto",
             local.lat, local.lon);

    HTTPClient https;
    https.setTimeout(7000);
    https.setReuse(false);
    if (https.begin(client, url)) {
      int code = https.GET();
      if (code == HTTP_CODE_OK) {
        String body = https.getString();
        const char *js = body.c_str();

        int omOffset = 0;
        if (jsonExtractInt(js, "\"utc_offset_seconds\":", &omOffset)) {
          local.utcOffsetSec = omOffset;
        }

        // Jump past "current_units" straight into the "current":{...} object
        const char *curObj = strstr(js, "\"current\":");
        if (curObj) {
          curObj = strchr(curObj, '{');
        }
        if (curObj) {
          float tempVal = 0.0f;
          int wmoCode = 0;
          bool hasTemp = jsonExtractFloat(curObj, "\"temperature_2m\":", &tempVal);
          bool hasCode = jsonExtractInt(curObj, "\"weather_code\":", &wmoCode);
          jsonExtractString(curObj, "\"time\":", local.isoTime, sizeof(local.isoTime));

          if (hasTemp) {
            local.tempC       = tempVal;
            local.weatherCode = hasCode ? wmoCode : 0;
            snprintf(local.weatherStr, sizeof(local.weatherStr), "%dC %s",
                     (int)lroundf(tempVal), wmoCodeToText(local.weatherCode));
            local.weatherOk = true;
          }
        }
      }
      https.end();
    }
  }

  portENTER_CRITICAL(&netSyncMux);
  netSyncResult      = local;
  netSyncResultReady = true;
  netSyncRunning     = false;
  portEXIT_CRITICAL(&netSyncMux);

  vTaskDelete(nullptr);
}

static void triggerStandaloneWeatherSync() {
  if (netSyncRunning || WiFi.status() != WL_CONNECTED) return;
  netSyncRunning    = true;
  lastWeatherSyncMs = millis();

  BaseType_t ok = xTaskCreatePinnedToCore(
      netWeatherTask, "net_weather", 8192, nullptr, 1, nullptr, 0);
  if (ok != pdPASS) {
    netSyncRunning = false;
    Serial.println("[NET] Failed to start Open-Meteo sync task");
  }
}

static void applyIsoFallbackClock(const char *iso) {
  if (!iso || !iso[0]) return;
  int yr = 0, mo = 0, dy = 0, hr = 0, mn = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d", &yr, &mo, &dy, &hr, &mn) != 5) return;

  int h12 = hr % 12;
  if (h12 == 0) h12 = 12;
  snprintf(vs.timeStr, sizeof(vs.timeStr), "%02d:%02d %s", h12, mn, (hr >= 12) ? "PM" : "AM");

  static const char *monNames[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  struct tm t = {};
  t.tm_year = yr - 1900;
  t.tm_mon  = clampInt(mo - 1, 0, 11);
  t.tm_mday = clampInt(dy, 1, 31);
  t.tm_hour = clampInt(hr, 0, 23);
  t.tm_min  = clampInt(mn, 0, 59);
  mktime(&t);
  static const char *dowNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  snprintf(vs.dateStr, sizeof(vs.dateStr), "%s, %02d %s",
           dowNames[clampInt(t.tm_wday, 0, 6)],
           t.tm_mday,
           monNames[clampInt(t.tm_mon, 0, 11)]);
}

static void applyStandaloneNetSyncResult() {
  if (!netSyncResultReady) return;

  NetSyncData local = {};
  portENTER_CRITICAL(&netSyncMux);
  local = netSyncResult;
  netSyncResultReady = false;
  portEXIT_CRITICAL(&netSyncMux);

  bool tzChanged = (local.utcOffsetSec != vs.utcOffsetSec);
  vs.latitude     = local.lat;
  vs.longitude    = local.lon;
  vs.utcOffsetSec = local.utcOffsetSec;
  if (local.city[0]) {
    strncpy(vs.cityStr, local.city, sizeof(vs.cityStr) - 1);
    vs.cityStr[sizeof(vs.cityStr) - 1] = '\0';
  }

  if (!ntpConfigured || tzChanged) {
    configTime(vs.utcOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
    ntpConfigured = true;
  }

  if (local.weatherOk) {
    weatherEverSynced = true;
    strncpy(vs.weatherStr, local.weatherStr, sizeof(vs.weatherStr) - 1);
    vs.weatherStr[sizeof(vs.weatherStr) - 1] = '\0';

    // Use Open-Meteo local timestamp immediately if NTP & BLE haven't set the clock yet
    struct tm ti;
    if (!bleTimeOverride && !getLocalTime(&ti, 0) && local.isoTime[0]) {
      applyIsoFallbackClock(local.isoTime);
    }

    char msg[76];
    snprintf(msg, sizeof(msg), "%s: %s (%.2f, %.2f)",
             vs.cityStr, vs.weatherStr, vs.latitude, vs.longitude);
    appendBleLog("NET", msg);
    Serial.printf("[NET] Synced %s\n", msg);
  }
}

// -----------------------------------------------------------------------------
// Custom Vector Renderers (Anti-Aliased LVGL 8.4 — Zero Extra Flash/RAM)
// -----------------------------------------------------------------------------
static void drawVisteonLogoCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *drawCtx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const float x0 = (float)a.x1;
  const float y0 = (float)a.y1;
  const float w  = (float)lv_area_get_width(&a);
  const float h  = (float)lv_area_get_height(&a);

  lv_draw_rect_dsc_t polyDsc;
  lv_draw_rect_dsc_init(&polyDsc);
  polyDsc.bg_color = lv_color_hex(VISTEON_ORANGE);
  polyDsc.bg_opa   = LV_OPA_COVER;

  lv_point_t leftStroke[4] = {
    {(lv_coord_t)(x0 + 0.000f * w), (lv_coord_t)(y0 + 0.000f * h)},
    {(lv_coord_t)(x0 + 0.385f * w), (lv_coord_t)(y0 + 0.000f * h)},
    {(lv_coord_t)(x0 + 0.692f * w), (lv_coord_t)(y0 + 0.616f * h)},
    {(lv_coord_t)(x0 + 0.500f * w), (lv_coord_t)(y0 + 1.000f * h)}
  };
  lv_draw_polygon(drawCtx, &polyDsc, leftStroke, 4);

  lv_point_t rightWedge[3] = {
    {(lv_coord_t)(x0 + 0.690f * w), (lv_coord_t)(y0 + 0.000f * h)},
    {(lv_coord_t)(x0 + 1.000f * w), (lv_coord_t)(y0 + 0.000f * h)},
    {(lv_coord_t)(x0 + 0.692f * w), (lv_coord_t)(y0 + 0.616f * h)}
  };
  lv_draw_polygon(drawCtx, &polyDsc, rightWedge, 3);
}

static void drawThemeIconCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int cy = (a.y1 + a.y2) / 2;

  if (!lightTheme) {
    lv_draw_rect_dsc_t sunCore;
    lv_draw_rect_dsc_init(&sunCore);
    sunCore.bg_color = lv_color_hex(0xFBBF24);
    sunCore.bg_opa   = LV_OPA_COVER;
    sunCore.radius   = LV_RADIUS_CIRCLE;
    lv_area_t core = {(lv_coord_t)(cx - 5), (lv_coord_t)(cy - 5),
                      (lv_coord_t)(cx + 5), (lv_coord_t)(cy + 5)};
    lv_draw_rect(ctx, &sunCore, &core);

    lv_draw_line_dsc_t ray;
    lv_draw_line_dsc_init(&ray);
    ray.color = lv_color_hex(0xFBBF24);
    ray.width = 2;
    ray.round_start = 1;
    ray.round_end   = 1;

    static const float dirs[8][2] = {
      { 1.0f,  0.0f}, {-1.0f,  0.0f}, { 0.0f,  1.0f}, { 0.0f, -1.0f},
      { 0.707f, 0.707f}, {-0.707f, 0.707f}, { 0.707f, -0.707f}, {-0.707f, -0.707f}
    };
    for (int i = 0; i < 8; ++i) {
      lv_point_t p1 = {(lv_coord_t)(cx + (int)lroundf(dirs[i][0] * 7.5f)),
                       (lv_coord_t)(cy + (int)lroundf(dirs[i][1] * 7.5f))};
      lv_point_t p2 = {(lv_coord_t)(cx + (int)lroundf(dirs[i][0] * 10.5f)),
                       (lv_coord_t)(cy + (int)lroundf(dirs[i][1] * 10.5f))};
      lv_draw_line(ctx, &ray, &p1, &p2);
    }
  } else {
    lv_draw_rect_dsc_t moon;
    lv_draw_rect_dsc_init(&moon);
    moon.bg_color = lv_color_hex(0x0F172A);
    moon.bg_opa   = LV_OPA_COVER;
    moon.radius   = LV_RADIUS_CIRCLE;
    lv_area_t outer = {(lv_coord_t)(cx - 8), (lv_coord_t)(cy - 8),
                       (lv_coord_t)(cx + 8), (lv_coord_t)(cy + 8)};
    lv_draw_rect(ctx, &moon, &outer);

    lv_draw_rect_dsc_t cut;
    lv_draw_rect_dsc_init(&cut);
    cut.bg_color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    cut.bg_opa   = LV_OPA_COVER;
    cut.radius   = LV_RADIUS_CIRCLE;
    lv_area_t inner = {(lv_coord_t)(cx - 3), (lv_coord_t)(cy - 11),
                       (lv_coord_t)(cx + 11), (lv_coord_t)(cy + 3)};
    lv_draw_rect(ctx, &cut, &inner);
  }
}

static void drawNavArrowCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int cy = (a.y1 + a.y2) / 2;
  const int w  = lv_area_get_width(&a);
  const int h  = lv_area_get_height(&a);
  const int sw = (w >= 130) ? 14 : ((w > 70) ? 10 : 5);
  const int hs = (w >= 130) ? 24 : ((w > 70) ? 18 : 9);

  uint32_t col = vs.navActive ? pal().green : pal().muted;

  if (!vs.navActive) {
    lv_draw_rect_dsc_t triDsc;
    lv_draw_rect_dsc_init(&triDsc);
    triDsc.bg_color = lv_color_hex(col);
    triDsc.bg_opa   = LV_OPA_COVER;
    int r = (w >= 130) ? 38 : ((w > 70) ? 30 : 14);
    lv_point_t ptr[3] = {
      {(lv_coord_t)cx, (lv_coord_t)(cy - r)},
      {(lv_coord_t)(cx - (r * 3) / 4), (lv_coord_t)(cy + r)},
      {(lv_coord_t)(cx + (r * 3) / 4), (lv_coord_t)(cy + r)}
    };
    lv_draw_polygon(ctx, &triDsc, ptr, 3);

    lv_draw_rect_dsc_t notch;
    lv_draw_rect_dsc_init(&notch);
    notch.bg_color = lv_color_hex(pal().surfaceAlt);
    notch.bg_opa   = LV_OPA_COVER;
    lv_point_t cut[3] = {
      {(lv_coord_t)cx, (lv_coord_t)(cy + r / 4)},
      {(lv_coord_t)(cx - r / 2), (lv_coord_t)(cy + r + 2)},
      {(lv_coord_t)(cx + r / 2), (lv_coord_t)(cy + r + 2)}
    };
    lv_draw_polygon(ctx, &notch, cut, 3);
    return;
  }

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(col);
  ld.width = sw;
  ld.round_start = 1;
  ld.round_end   = 1;

  lv_draw_rect_dsc_t headDsc;
  lv_draw_rect_dsc_init(&headDsc);
  headDsc.bg_color = lv_color_hex(col);
  headDsc.bg_opa   = LV_OPA_COVER;

  if (vs.navDir == NAV_RIGHT) {
    lv_point_t p1 = {(lv_coord_t)(cx - w / 6), (lv_coord_t)(a.y2 - h / 6)};
    lv_point_t p2 = {(lv_coord_t)(cx - w / 6), (lv_coord_t)(cy - h / 10)};
    lv_point_t p3 = {(lv_coord_t)(a.x2 - w / 4), (lv_coord_t)(cy - h / 10)};
    lv_draw_line(ctx, &ld, &p1, &p2);
    lv_draw_line(ctx, &ld, &p2, &p3);
    lv_point_t tri[3] = {
      {(lv_coord_t)(a.x2 - w / 8), (lv_coord_t)(cy - h / 10)},
      {(lv_coord_t)(a.x2 - w / 8 - hs), (lv_coord_t)(cy - h / 10 - hs)},
      {(lv_coord_t)(a.x2 - w / 8 - hs), (lv_coord_t)(cy - h / 10 + hs)}
    };
    lv_draw_polygon(ctx, &headDsc, tri, 3);
  } else if (vs.navDir == NAV_LEFT) {
    lv_point_t p1 = {(lv_coord_t)(cx + w / 6), (lv_coord_t)(a.y2 - h / 6)};
    lv_point_t p2 = {(lv_coord_t)(cx + w / 6), (lv_coord_t)(cy - h / 10)};
    lv_point_t p3 = {(lv_coord_t)(a.x1 + w / 4), (lv_coord_t)(cy - h / 10)};
    lv_draw_line(ctx, &ld, &p1, &p2);
    lv_draw_line(ctx, &ld, &p2, &p3);
    lv_point_t tri[3] = {
      {(lv_coord_t)(a.x1 + w / 8), (lv_coord_t)(cy - h / 10)},
      {(lv_coord_t)(a.x1 + w / 8 + hs), (lv_coord_t)(cy - h / 10 - hs)},
      {(lv_coord_t)(a.x1 + w / 8 + hs), (lv_coord_t)(cy - h / 10 + hs)}
    };
    lv_draw_polygon(ctx, &headDsc, tri, 3);
  } else if (vs.navDir == NAV_UTURN) {
    lv_point_t p1 = {(lv_coord_t)(cx + w / 6), (lv_coord_t)(a.y2 - h / 6)};
    lv_point_t p2 = {(lv_coord_t)(cx + w / 6), (lv_coord_t)(a.y1 + h / 4)};
    lv_point_t p3 = {(lv_coord_t)(cx - w / 6), (lv_coord_t)(a.y1 + h / 4)};
    lv_point_t p4 = {(lv_coord_t)(cx - w / 6), (lv_coord_t)(cy + h / 8)};
    lv_draw_line(ctx, &ld, &p1, &p2);
    lv_draw_line(ctx, &ld, &p2, &p3);
    lv_draw_line(ctx, &ld, &p3, &p4);
    lv_point_t tri[3] = {
      {(lv_coord_t)(cx - w / 6), (lv_coord_t)(cy + h / 4 + hs / 2)},
      {(lv_coord_t)(cx - w / 6 - hs), (lv_coord_t)(cy + h / 4 - hs / 2)},
      {(lv_coord_t)(cx - w / 6 + hs), (lv_coord_t)(cy + h / 4 - hs / 2)}
    };
    lv_draw_polygon(ctx, &headDsc, tri, 3);
  } else {
    lv_point_t p1 = {(lv_coord_t)cx, (lv_coord_t)(a.y2 - h / 6)};
    lv_point_t p2 = {(lv_coord_t)cx, (lv_coord_t)(a.y1 + h / 4)};
    lv_draw_line(ctx, &ld, &p1, &p2);
    lv_point_t tri[3] = {
      {(lv_coord_t)cx, (lv_coord_t)(a.y1 + h / 8)},
      {(lv_coord_t)(cx - hs), (lv_coord_t)(a.y1 + h / 8 + hs)},
      {(lv_coord_t)(cx + hs), (lv_coord_t)(a.y1 + h / 8 + hs)}
    };
    lv_draw_polygon(ctx, &headDsc, tri, 3);
  }
}

static void drawAdasCanvasCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int w  = lv_area_get_width(&a);
  const int cx = a.x1 + w / 2;
  const int vy = a.y1 + 24;
  const int rH = (a.y2 - vy);

  lv_draw_rect_dsc_t roadDsc;
  lv_draw_rect_dsc_init(&roadDsc);
  roadDsc.bg_color = lv_color_hex(pal().road);
  roadDsc.bg_opa   = LV_OPA_COVER;

  lv_point_t roadPoly[4] = {
    {(lv_coord_t)(cx - 28), (lv_coord_t)vy},
    {(lv_coord_t)(cx + 28), (lv_coord_t)vy},
    {(lv_coord_t)(a.x2 - 8), (lv_coord_t)a.y2},
    {(lv_coord_t)(a.x1 + 8), (lv_coord_t)a.y2}
  };
  lv_draw_polygon(ctx, &roadDsc, roadPoly, 4);

  lv_draw_line_dsc_t horzDsc;
  lv_draw_line_dsc_init(&horzDsc);
  horzDsc.color = lv_color_hex(pal().border);
  horzDsc.width = 1;
  lv_point_t h1 = {(lv_coord_t)(a.x1 + 4), (lv_coord_t)vy};
  lv_point_t h2 = {(lv_coord_t)(a.x2 - 4), (lv_coord_t)vy};
  lv_draw_line(ctx, &horzDsc, &h1, &h2);

  lv_draw_line_dsc_t dashDsc;
  lv_draw_line_dsc_init(&dashDsc);
  dashDsc.color = lv_color_hex(pal().muted);
  dashDsc.opa   = LV_OPA_70;
  dashDsc.round_start = 1;
  dashDsc.round_end   = 1;

  for (int i = 0; i < 5; ++i) {
    float t0 = fmodf(vs.roadPhase + i * 0.20f, 1.0f);
    float t1 = t0 + 0.08f;
    if (t1 > 1.0f) t1 = 1.0f;
    int y0 = vy + (int)(t0 * t0 * rH);
    int y1 = vy + (int)(t1 * t1 * rH);
    dashDsc.width = 2 + (int)(t0 * 3.0f);
    lv_point_t p1 = {(lv_coord_t)cx, (lv_coord_t)y0};
    lv_point_t p2 = {(lv_coord_t)cx, (lv_coord_t)y1};
    lv_draw_line(ctx, &dashDsc, &p1, &p2);
  }

  lv_draw_line_dsc_t railDsc;
  lv_draw_line_dsc_init(&railDsc);
  uint32_t railCol = isOverspeed ? pal().red : (vs.lkaActive && vs.gear == 'D' ? modeAccentColor() : pal().muted);
  railDsc.color = lv_color_hex(railCol);
  railDsc.width = 3;
  railDsc.opa   = LV_OPA_90;

  lv_point_t lTop = {(lv_coord_t)(cx - 28), (lv_coord_t)vy};
  lv_point_t lBot = {(lv_coord_t)(a.x1 + 10), (lv_coord_t)a.y2};
  lv_point_t rTop = {(lv_coord_t)(cx + 28), (lv_coord_t)vy};
  lv_point_t rBot = {(lv_coord_t)(a.x2 - 10), (lv_coord_t)a.y2};
  lv_draw_line(ctx, &railDsc, &lTop, &lBot);
  lv_draw_line(ctx, &railDsc, &rTop, &rBot);

  if (vs.gear == 'D') {
    float normDist = clampFloat((vs.leadDistM - 15.0f) / 60.0f, 0.0f, 1.0f);
    float leadT = 0.56f - normDist * 0.34f;
    int leadY = vy + (int)(leadT * rH);
    int leadW = 28 + (int)(leadT * 42.0f);
    int leadH = 16 + (int)(leadT * 22.0f);

    if (vs.accActive && vs.speedKmh > 5.0f) {
      lv_draw_arc_dsc_t radDsc;
      lv_draw_arc_dsc_init(&radDsc);
      radDsc.color = lv_color_hex(vs.leadDistM < 22.0f ? pal().amber : pal().cyan);
      radDsc.width = 2;
      radDsc.opa   = LV_OPA_60;
      lv_point_t rCenter = {(lv_coord_t)cx, (lv_coord_t)(a.y2 - 58)};
      lv_draw_arc(ctx, &radDsc, &rCenter, 28, 225, 315);
      lv_draw_arc(ctx, &radDsc, &rCenter, 44, 232, 308);
    }

    lv_draw_rect_dsc_t leadBody;
    lv_draw_rect_dsc_init(&leadBody);
    leadBody.bg_color = lv_color_hex(lightTheme ? 0x64748B : 0x475569);
    leadBody.bg_opa   = LV_OPA_COVER;
    leadBody.radius   = 5;
    lv_area_t lb = {
      (lv_coord_t)(cx - leadW / 2), (lv_coord_t)(leadY - leadH),
      (lv_coord_t)(cx + leadW / 2), (lv_coord_t)leadY
    };
    lv_draw_rect(ctx, &leadBody, &lb);

    lv_draw_rect_dsc_t tailDsc;
    lv_draw_rect_dsc_init(&tailDsc);
    tailDsc.bg_color = lv_color_hex(pal().red);
    tailDsc.bg_opa   = LV_OPA_COVER;
    tailDsc.radius   = 2;
    lv_area_t ltL = {(lv_coord_t)(lb.x1 + 3), (lv_coord_t)(lb.y2 - 7), (lv_coord_t)(lb.x1 + 11), (lv_coord_t)(lb.y2 - 3)};
    lv_area_t ltR = {(lv_coord_t)(lb.x2 - 11), (lv_coord_t)(lb.y2 - 7), (lv_coord_t)(lb.x2 - 3), (lv_coord_t)(lb.y2 - 3)};
    lv_draw_rect(ctx, &tailDsc, &ltL);
    lv_draw_rect(ctx, &tailDsc, &ltR);
  }

  int egoW = 74, egoH = 92;
  int egoTop = a.y2 - egoH - 6;

  lv_draw_rect_dsc_t glowDsc;
  lv_draw_rect_dsc_init(&glowDsc);
  glowDsc.bg_color = lv_color_hex(isOverspeed ? pal().red : modeAccentColor());
  glowDsc.bg_opa   = LV_OPA_20;
  glowDsc.radius   = 18;
  lv_area_t gBox = {(lv_coord_t)(cx - egoW / 2 - 6), (lv_coord_t)(egoTop - 4),
                    (lv_coord_t)(cx + egoW / 2 + 6), (lv_coord_t)(egoTop + egoH + 4)};
  lv_draw_rect(ctx, &glowDsc, &gBox);

  lv_draw_rect_dsc_t egoDsc;
  lv_draw_rect_dsc_init(&egoDsc);
  egoDsc.bg_color     = lv_color_hex(lightTheme ? 0xF8FAFC : 0xDCE6F2);
  egoDsc.bg_opa       = LV_OPA_COVER;
  egoDsc.radius       = 16;
  egoDsc.border_color = lv_color_hex(pal().muted);
  egoDsc.border_width = 1;
  lv_area_t eBox = {(lv_coord_t)(cx - egoW / 2), (lv_coord_t)egoTop,
                    (lv_coord_t)(cx + egoW / 2), (lv_coord_t)(egoTop + egoH)};
  lv_draw_rect(ctx, &egoDsc, &eBox);

  lv_draw_rect_dsc_t glassDsc;
  lv_draw_rect_dsc_init(&glassDsc);
  glassDsc.bg_color = lv_color_hex(0x0F172A);
  glassDsc.bg_opa   = LV_OPA_COVER;
  glassDsc.radius   = 10;
  lv_area_t glassBox = {(lv_coord_t)(cx - 24), (lv_coord_t)(egoTop + 20),
                        (lv_coord_t)(cx + 24), (lv_coord_t)(egoTop + 68)};
  lv_draw_rect(ctx, &glassDsc, &glassBox);

  bool brakeLit = vs.braking || (vs.gear == 'P');
  lv_draw_rect_dsc_t egoTail;
  lv_draw_rect_dsc_init(&egoTail);
  egoTail.bg_color = lv_color_hex(brakeLit ? 0xFF2222 : 0xB91C1C);
  egoTail.bg_opa   = brakeLit ? LV_OPA_COVER : LV_OPA_80;
  egoTail.radius   = 3;
  lv_area_t etBar = {(lv_coord_t)(cx - 28), (lv_coord_t)(egoTop + egoH - 9),
                     (lv_coord_t)(cx + 28), (lv_coord_t)(egoTop + egoH - 4)};
  lv_draw_rect(ctx, &egoTail, &etBar);

  bool showLeft  = (vs.leftBlinker  || vs.hazard) && blinkState;
  bool showRight = (vs.rightBlinker || vs.hazard) && blinkState;

  lv_draw_rect_dsc_t sigDsc;
  lv_draw_rect_dsc_init(&sigDsc);
  sigDsc.bg_color = lv_color_hex(pal().amber);
  sigDsc.bg_opa   = LV_OPA_COVER;

  if (showLeft) {
    lv_point_t triL[3] = {
      {(lv_coord_t)(cx - egoW / 2 - 22), (lv_coord_t)(egoTop + 46)},
      {(lv_coord_t)(cx - egoW / 2 - 8),  (lv_coord_t)(egoTop + 34)},
      {(lv_coord_t)(cx - egoW / 2 - 8),  (lv_coord_t)(egoTop + 58)}
    };
    lv_draw_polygon(ctx, &sigDsc, triL, 3);
  }
  if (showRight) {
    lv_point_t triR[3] = {
      {(lv_coord_t)(cx + egoW / 2 + 22), (lv_coord_t)(egoTop + 46)},
      {(lv_coord_t)(cx + egoW / 2 + 8),  (lv_coord_t)(egoTop + 34)},
      {(lv_coord_t)(cx + egoW / 2 + 8),  (lv_coord_t)(egoTop + 58)}
    };
    lv_draw_polygon(ctx, &sigDsc, triR, 3);
  }
}

static void drawMediaDiscCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int cy = (a.y1 + a.y2) / 2;
  const int r  = lv_area_get_width(&a) / 2 - 6;

  lv_draw_rect_dsc_t disc;
  lv_draw_rect_dsc_init(&disc);
  disc.bg_color     = lv_color_hex(0x0B111C);
  disc.bg_opa       = LV_OPA_COVER;
  disc.radius       = LV_RADIUS_CIRCLE;
  disc.border_color = lv_color_hex(pal().border);
  disc.border_width = 2;
  lv_area_t dArea = {(lv_coord_t)(cx - r), (lv_coord_t)(cy - r),
                     (lv_coord_t)(cx + r), (lv_coord_t)(cy + r)};
  lv_draw_rect(ctx, &disc, &dArea);

  lv_draw_arc_dsc_t arcDsc;
  lv_draw_arc_dsc_init(&arcDsc);
  arcDsc.color   = lv_color_hex(VISTEON_ORANGE);
  arcDsc.width   = 4;
  arcDsc.rounded = 1;
  uint16_t startA = ((uint16_t)vs.vinylAngle) % 360;
  lv_point_t center = {(lv_coord_t)cx, (lv_coord_t)cy};
  lv_draw_arc(ctx, &arcDsc, &center, (uint16_t)(r - 12), startA, (uint16_t)((startA + 75) % 360));

  arcDsc.color = lv_color_hex(pal().cyan);
  arcDsc.width = 2;
  lv_draw_arc(ctx, &arcDsc, &center, (uint16_t)(r - 24), (uint16_t)((startA + 180) % 360), (uint16_t)((startA + 250) % 360));

  lv_draw_rect_dsc_t hub;
  lv_draw_rect_dsc_init(&hub);
  hub.bg_color = lv_color_hex(VISTEON_ORANGE);
  hub.bg_opa   = LV_OPA_COVER;
  hub.radius   = LV_RADIUS_CIRCLE;
  lv_area_t hArea = {(lv_coord_t)(cx - 24), (lv_coord_t)(cy - 24),
                     (lv_coord_t)(cx + 24), (lv_coord_t)(cy + 24)};
  lv_draw_rect(ctx, &hub, &hArea);

  hub.bg_color = lv_color_hex(pal().bg);
  lv_area_t spindle = {(lv_coord_t)(cx - 7), (lv_coord_t)(cy - 7),
                       (lv_coord_t)(cx + 7), (lv_coord_t)(cy + 7)};
  lv_draw_rect(ctx, &hub, &spindle);
}

static void drawMediaEqCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int w = lv_area_get_width(&a);
  const int h = lv_area_get_height(&a);
  const int barW = (w - 11 * 6) / 12;

  lv_draw_rect_dsc_t bd;
  lv_draw_rect_dsc_init(&bd);
  bd.bg_opa = LV_OPA_COVER;
  bd.radius = 3;

  for (int i = 0; i < 12; ++i) {
    int bh = (int)(vs.eqBars[i] * (float)h / 100.0f);
    if (bh < 6) bh = 6;
    bd.bg_color = lv_color_hex((i % 3 == 0) ? VISTEON_ORANGE : pal().cyan);
    int x = a.x1 + i * (barW + 6);
    lv_area_t r = {(lv_coord_t)x, (lv_coord_t)(a.y2 - bh),
                   (lv_coord_t)(x + barW), (lv_coord_t)a.y2};
    lv_draw_rect(ctx, &bd, &r);
  }
}

static void drawVehicleChassisCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int topY = a.y1 + 32;
  const int bodyW = 92;
  const int bodyH = 196;

  if (vs.headlightsOn) {
    lv_draw_rect_dsc_t beam;
    lv_draw_rect_dsc_init(&beam);
    beam.bg_color = lv_color_hex(0xFFF3B0);
    beam.bg_opa   = LV_OPA_30;
    lv_point_t bL[4] = {
      {(lv_coord_t)(cx - 38), (lv_coord_t)(topY + 4)},
      {(lv_coord_t)(cx - 12), (lv_coord_t)(topY + 4)},
      {(lv_coord_t)(cx - 6),  (lv_coord_t)(a.y1 + 2)},
      {(lv_coord_t)(cx - 56), (lv_coord_t)(a.y1 + 2)}
    };
    lv_point_t bR[4] = {
      {(lv_coord_t)(cx + 12), (lv_coord_t)(topY + 4)},
      {(lv_coord_t)(cx + 38), (lv_coord_t)(topY + 4)},
      {(lv_coord_t)(cx + 56), (lv_coord_t)(a.y1 + 2)},
      {(lv_coord_t)(cx + 6),  (lv_coord_t)(a.y1 + 2)}
    };
    lv_draw_polygon(ctx, &beam, bL, 4);
    lv_draw_polygon(ctx, &beam, bR, 4);
  }

  int wx[4] = {cx - bodyW / 2 - 10, cx + bodyW / 2 - 2, cx - bodyW / 2 - 10, cx + bodyW / 2 - 2};
  int wy[4] = {topY + 26, topY + 26, topY + bodyH - 60, topY + bodyH - 60};
  for (int i = 0; i < 4; ++i) {
    lv_draw_rect_dsc_t wd;
    lv_draw_rect_dsc_init(&wd);
    uint32_t wc = (vs.tpms[i] < 29.0f) ? pal().red : ((vs.tpms[i] < 32.0f) ? pal().amber : pal().green);
    wd.bg_color = lv_color_hex(wc);
    wd.bg_opa   = LV_OPA_COVER;
    wd.radius   = 4;
    lv_area_t wr = {(lv_coord_t)wx[i], (lv_coord_t)wy[i],
                    (lv_coord_t)(wx[i] + 12), (lv_coord_t)(wy[i] + 34)};
    lv_draw_rect(ctx, &wd, &wr);
  }

  lv_draw_rect_dsc_t body;
  lv_draw_rect_dsc_init(&body);
  body.bg_color     = lv_color_hex(lightTheme ? 0xE2E8F0 : 0x1E2D42);
  body.bg_opa       = LV_OPA_COVER;
  body.radius       = 26;
  body.border_color = lv_color_hex(vs.doorsLocked ? pal().cyan : pal().amber);
  body.border_width = 2;
  lv_area_t bArea = {(lv_coord_t)(cx - bodyW / 2), (lv_coord_t)topY,
                     (lv_coord_t)(cx + bodyW / 2), (lv_coord_t)(topY + bodyH)};
  lv_draw_rect(ctx, &body, &bArea);

  lv_draw_rect_dsc_t glass;
  lv_draw_rect_dsc_init(&glass);
  glass.bg_color = lv_color_hex(0x0B1320);
  glass.bg_opa   = LV_OPA_COVER;
  glass.radius   = 14;
  lv_area_t gArea = {(lv_coord_t)(cx - 32), (lv_coord_t)(topY + 42),
                     (lv_coord_t)(cx + 32), (lv_coord_t)(topY + 148)};
  lv_draw_rect(ctx, &glass, &gArea);

  if (vs.trunkOpen) {
    lv_draw_rect_dsc_t trk;
    lv_draw_rect_dsc_init(&trk);
    trk.bg_color = lv_color_hex(VISTEON_ORANGE);
    trk.bg_opa   = LV_OPA_80;
    trk.radius   = 6;
    lv_area_t tArea = {(lv_coord_t)(cx - 28), (lv_coord_t)(topY + bodyH - 22),
                       (lv_coord_t)(cx + 28), (lv_coord_t)(topY + bodyH - 4)};
    lv_draw_rect(ctx, &trk, &tArea);
  }
}

// -----------------------------------------------------------------------------
// LVGL Hardware Drivers
// -----------------------------------------------------------------------------
static void displayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorMap) {
  LCD *activeLcd = static_cast<LCD *>(disp->user_data);
  const int width  = area->x2 - area->x1 + 1;
  const int height = area->y2 - area->y1 + 1;
  activeLcd->drawBitmap(area->x1, area->y1, width, height,
                        reinterpret_cast<const uint8_t *>(colorMap));
  if (activeLcd->getBus()->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
    lv_disp_flush_ready(disp);
  }
}

static void touchReadCallback(lv_indev_drv_t *indevDriver, lv_indev_data_t *data) {
  Touch *activeTouch = static_cast<Touch *>(indevDriver->user_data);
  TouchPoint point;
  int count = 0;
  if (activeTouch && activeTouch->getPanelHandle() != nullptr) {
    count = activeTouch->readPoints(&point, 1, 0);
  }
  if (count > 0) {
    data->point.x = point.x;
    data->point.y = point.y;
    data->state   = LV_INDEV_STATE_PRESSED;
    vs.lastTouchX = point.x;
    vs.lastTouchY = point.y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void lvglTick(void *arg) { lv_tick_inc(2); }

static bool initLvgl() {
  lv_init();
  const uint32_t width  = lcd->getFrameWidth();
  const uint32_t height = lcd->getFrameHeight();
  uint32_t bufferPixels = width * 20;

  drawBuffer1 = static_cast<lv_color_t *>(heap_caps_malloc(
      bufferPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  drawBuffer2 = static_cast<lv_color_t *>(heap_caps_malloc(
      bufferPixels * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

  if (!drawBuffer1 || !drawBuffer2) {
    if (drawBuffer1) { heap_caps_free(drawBuffer1); drawBuffer1 = nullptr; }
    if (drawBuffer2) { heap_caps_free(drawBuffer2); drawBuffer2 = nullptr; }
    bufferPixels = width * 40;
    drawBuffer1 = static_cast<lv_color_t *>(heap_caps_malloc(
        bufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    drawBuffer2 = static_cast<lv_color_t *>(heap_caps_malloc(
        bufferPixels * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (!drawBuffer1 || !drawBuffer2) return false;

  lv_disp_draw_buf_init(&drawBuffer, drawBuffer1, drawBuffer2, bufferPixels);
  static lv_disp_drv_t dispDrv;
  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res   = width;
  dispDrv.ver_res   = height;
  dispDrv.flush_cb  = displayFlush;
  dispDrv.draw_buf  = &drawBuffer;
  dispDrv.user_data = lcd;
  lv_disp_drv_register(&dispDrv);

  if (touch && touch->getPanelHandle() != nullptr) {
    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type      = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb   = touchReadCallback;
    indevDrv.user_data = touch;
    lv_indev_drv_register(&indevDrv);
    touchReady = true;
  }

  esp_timer_create_args_t tickArgs = {};
  tickArgs.callback = &lvglTick;
  tickArgs.name     = "lvgl_tick";
  if (esp_timer_create(&tickArgs, &lvglTickTimer) != ESP_OK) return false;
  return (esp_timer_start_periodic(lvglTickTimer, 2000) == ESP_OK);
}

// -----------------------------------------------------------------------------
// UI Component Factory
// -----------------------------------------------------------------------------
static lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, const lv_font_t *font,
                           uint32_t colorHex, int x, int y, int width = 0) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_font(lbl, font, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(colorHex), 0);
  lv_obj_set_pos(lbl, x, y);
  if (width > 0) {
    lv_obj_set_width(lbl, width);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
  }
  return lbl;
}

static lv_obj_t *makeCard(lv_obj_t *parent, int x, int y, int w, int h,
                          uint32_t bgHex = 0, int radius = 16) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(card, lv_color_hex(bgHex ? bgHex : pal().surface), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(pal().border), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, radius, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  return card;
}

static lv_obj_t *makeBtn(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                         lv_event_cb_t cb, void *userData = nullptr,
                         uint32_t bgHex = 0, uint32_t txtHex = 0, int radius = 10) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex ? bgHex : pal().surfaceAlt), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(VISTEON_ORANGE), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(pal().border), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, radius, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);

  if (text && text[0]) {
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(txtHex ? txtHex : pal().text), 0);
    lv_obj_center(lbl);
  }
  return btn;
}

static void resetPagePointers() {
  drvModeBadge = drvSpeedLimitBox = drvSpeedLimitLbl = drvSpeedArc = drvPowerArc = nullptr;
  drvSpeedVal = drvSpeedUnit = drvPowerVal = drvThrottleSld = drvAdasCanvas = nullptr;
  drvLeftBtn = drvRightBtn = drvHazardBtn = drvDemoBtn = drvAdasFooter = nullptr;
  drvNavArrowBox = drvNavDistLbl = drvNavStreetLbl = drvNavEtaLbl = nullptr;
  drvMediaTitle = drvMediaArtist = drvMediaProg = drvMediaPlayLbl = nullptr;
  drvSocLbl = drvSocBar = drvEffLbl = nullptr;
  for (int i = 0; i < 4; ++i) drvGearBtns[i] = nullptr;
  for (int i = 0; i < 3; ++i) drvModeBtns[i] = nullptr;

  navStatusBadge = navStatusLbl = navBigArrowBox = navBigDistLbl = nullptr;
  navBigActionLbl = navBigStreetLbl = navProgPctLbl = navProgBar = nullptr;
  navEtaValLbl = navEtaSubLbl = navRemValLbl = navRemSubLbl = navDestValLbl = nullptr;
  navCallCard = navCallStatusLbl = navCallerLbl = navCallSubLbl = navPhoneBattLbl = nullptr;
  navAcceptBtn = navSpeakerBtn = navSpeakerBtnLbl = navEndBtn = nullptr;
  navNotifCountLbl = navNotifList = nullptr;

  medDiscCanvas = medEqCanvas = medTitleLbl = medArtistLbl = medProgSlider = nullptr;
  medTimeLbl = medPlayBtnLbl = medVolSlider = medVolLbl = nullptr;
  for (int i = 0; i < 4; ++i) medEqBtns[i] = nullptr;

  vehChassisCanvas = vehLightBtn = vehLockBtn = vehTrunkBtn = nullptr;
  vehDrvTempLbl = vehPasTempLbl = vehFanLbl = vehAcBtn = vehAutoBtn = nullptr;
  vehDefrostBtn = vehSeatBtn = vehHvStatsLbl = vehRegenBtn = nullptr;
  for (int i = 0; i < 4; ++i) vehTpmsLbl[i] = nullptr;

  conWifiStatusLbl = conWifiDetailLbl = conWifiList = conBleStatusLbl = conBlePhoneLbl = conBleLogLbl = nullptr;

#if LV_USE_CHART
  sysChart = nullptr;
  serSpeed = serPower = nullptr;
#endif
  sysMemLbl = sysTouchLbl = sysTouchDot = sysUnitBtnLbl = nullptr;

  lastShownSpeed   = -1;
  lastShownPower   = -999;
  lastOverspeedVis = false;
  lastBlinkVis     = false;
}

static void triggerAlert(const char *text, uint32_t colorHex, uint32_t durationMs) {
  strncpy(vs.alertText, text, sizeof(vs.alertText) - 1);
  vs.alertText[sizeof(vs.alertText) - 1] = '\0';
  vs.alertColor = colorHex;
  vs.alertUntil = millis() + durationMs;
}

// -----------------------------------------------------------------------------
// 5-Item Recent Notification Feed Helpers
// -----------------------------------------------------------------------------
static void pushNotification(const char *appName, const char *message, bool flashBanner) {
  const char *safeApp = (appName && appName[0]) ? appName : "Phone";
  const char *safeMsg = (message && message[0]) ? message : "";
  if (!safeMsg[0]) return;

  if (vs.notifCount > 0 &&
      strncmp(vs.notifs[0].app, safeApp, sizeof(vs.notifs[0].app)) == 0 &&
      strncmp(vs.notifs[0].msg, safeMsg, sizeof(vs.notifs[0].msg)) == 0) {
    if (flashBanner) {
      char banner[76];
      snprintf(banner, sizeof(banner), "%s: %s", safeApp, safeMsg);
      triggerAlert(banner, pal().amber, 4500);
    }
    return;
  }

  uint8_t shiftLimit = (vs.notifCount < MAX_NOTIFS) ? vs.notifCount : (MAX_NOTIFS - 1);
  for (int i = (int)shiftLimit; i > 0; --i) {
    vs.notifs[i] = vs.notifs[i - 1];
  }
  strncpy(vs.notifs[0].app, safeApp, sizeof(vs.notifs[0].app) - 1);
  vs.notifs[0].app[sizeof(vs.notifs[0].app) - 1] = '\0';
  strncpy(vs.notifs[0].msg, safeMsg, sizeof(vs.notifs[0].msg) - 1);
  vs.notifs[0].msg[sizeof(vs.notifs[0].msg) - 1] = '\0';

  if (vs.notifCount < MAX_NOTIFS) vs.notifCount++;
  vs.notifDirty = true;

  if (flashBanner) {
    char banner[76];
    snprintf(banner, sizeof(banner), "%s: %s", safeApp, safeMsg);
    triggerAlert(banner, pal().amber, 4500);
  }
}

static void clearNotifications() {
  vs.notifCount = 0;
  vs.notifDirty = true;
}

static void rebuildNotifList() {
  if (currentPage != PAGE_NAV || !navNotifList) return;
  lv_obj_clean(navNotifList);

  if (navNotifCountLbl) {
    char cntBuf[16];
    snprintf(cntBuf, sizeof(cntBuf), "%u / %u", (unsigned)vs.notifCount, (unsigned)MAX_NOTIFS);
    setLabelIfChanged(navNotifCountLbl, cntBuf);
  }

  if (vs.notifCount == 0) {
    lv_obj_t *emptyLbl = lv_label_create(navNotifList);
    lv_label_set_text(emptyLbl, "No recent notifications.\nPhone messages will appear here.");
    lv_obj_set_style_text_font(emptyLbl, FONT_XS, 0);
    lv_obj_set_style_text_color(emptyLbl, lv_color_hex(pal().muted), 0);
    lv_obj_set_width(emptyLbl, 266);
    lv_label_set_long_mode(emptyLbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(emptyLbl, 4, 6);
    vs.notifDirty = false;
    return;
  }

  for (uint8_t i = 0; i < vs.notifCount; ++i) {
    lv_obj_t *item = lv_obj_create(navNotifList);
    lv_obj_set_width(item, 272);
#if LV_USE_FLEX
    lv_obj_set_height(item, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(item, 2, 0);
#else
    lv_obj_set_pos(item, 0, i * 54);
    lv_obj_set_height(item, 50);
#endif
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(item, lv_color_hex(pal().surfaceAlt), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(item, lv_color_hex(i == 0 ? VISTEON_ORANGE : pal().border), 0);
    lv_obj_set_style_border_width(item, 1, 0);
    lv_obj_set_style_radius(item, 8, 0);
    lv_obj_set_style_pad_hor(item, 8, 0);
    lv_obj_set_style_pad_ver(item, 5, 0);

    lv_obj_t *appLbl = lv_label_create(item);
    lv_label_set_text(appLbl, vs.notifs[i].app);
    lv_obj_set_style_text_font(appLbl, FONT_XS, 0);
    lv_obj_set_style_text_color(appLbl, lv_color_hex(i == 0 ? VISTEON_ORANGE : pal().amber), 0);
    lv_obj_set_width(appLbl, 254);
    lv_label_set_long_mode(appLbl, LV_LABEL_LONG_DOT);

    lv_obj_t *msgLbl = lv_label_create(item);
    lv_label_set_text(msgLbl, vs.notifs[i].msg);
    lv_obj_set_style_text_font(msgLbl, FONT_XS, 0);
    lv_obj_set_style_text_color(msgLbl, lv_color_hex(pal().text), 0);
    lv_obj_set_width(msgLbl, 254);
#if LV_USE_FLEX
    lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_WRAP);
#else
    lv_obj_set_pos(msgLbl, 0, 16);
    lv_label_set_long_mode(msgLbl, LV_LABEL_LONG_DOT);
#endif
  }

  vs.notifDirty = false;
}

// -----------------------------------------------------------------------------
// Persistent Full-Screen Overspeed Red Warning Overlay (Click-Through)
// -----------------------------------------------------------------------------
static void initOverspeedOverlay() {
  if (overspeedOverlay) return;
  overspeedOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(overspeedOverlay, 0, 0);
  lv_obj_set_size(overspeedOverlay, 800, 480);
  lv_obj_clear_flag(overspeedOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(overspeedOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(overspeedOverlay, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_bg_opa(overspeedOverlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(overspeedOverlay, lv_color_hex(0xEF4444), 0);
  lv_obj_set_style_border_width(overspeedOverlay, 12, 0);
  lv_obj_set_style_border_opa(overspeedOverlay, LV_OPA_TRANSP, 0);
  lv_obj_set_style_radius(overspeedOverlay, 0, 0);
  lv_obj_set_style_pad_all(overspeedOverlay, 0, 0);
  lv_obj_add_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN);
}

// -----------------------------------------------------------------------------
// Cinematic Boot Splash (Exact Vector Visteon Logo)
// -----------------------------------------------------------------------------
static void showBootSplash() {
  if (bootOverlay) {
    lv_obj_del(bootOverlay);
    bootOverlay = nullptr;
  }
  bootOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(bootOverlay, 800, 480);
  lv_obj_set_pos(bootOverlay, 0, 0);
  lv_obj_clear_flag(bootOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(bootOverlay, lv_color_hex(0x04070C), 0);
  lv_obj_set_style_bg_opa(bootOverlay, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bootOverlay, 0, 0);
  lv_obj_set_style_radius(bootOverlay, 0, 0);

  lv_obj_t *vLogo = lv_obj_create(bootOverlay);
  lv_obj_remove_style_all(vLogo);
  lv_obj_set_size(vLogo, 140, 114);
  lv_obj_align(vLogo, LV_ALIGN_CENTER, 0, -54);
  lv_obj_add_event_cb(vLogo, drawVisteonLogoCb, LV_EVENT_DRAW_MAIN, nullptr);

  lv_obj_t *title = lv_label_create(bootOverlay);
  lv_label_set_text(title, "V I S T E O N");
  lv_obj_set_style_text_font(title, FONT_XL, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xF4F7FB), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, 32);

  lv_obj_t *sub = lv_label_create(bootOverlay);
  lv_label_set_text(sub, "LUNET OS 3.0  |  DIGITAL COCKPIT");
  lv_obj_set_style_text_font(sub, FONT_SM, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x8094AD), 0);
  lv_obj_align(sub, LV_ALIGN_CENTER, 0, 68);

  bootBar = lv_bar_create(bootOverlay);
  lv_obj_set_size(bootBar, 260, 6);
  lv_obj_align(bootBar, LV_ALIGN_CENTER, 0, 108);
  lv_bar_set_range(bootBar, 0, 100);
  lv_bar_set_value(bootBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bootBar, lv_color_hex(0x162133), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bootBar, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);
  lv_obj_set_style_radius(bootBar, 3, LV_PART_MAIN);
  lv_obj_set_style_radius(bootBar, 3, LV_PART_INDICATOR);

  bootActive  = true;
  bootStartMs = millis();
}

// -----------------------------------------------------------------------------
// Page 1: DRIVE (Cockpit HUD)
// -----------------------------------------------------------------------------
static void refreshDriveStyles() {
  if (currentPage != PAGE_DRIVE) return;
  vs.adasDirty = true;
  uint32_t acc = modeAccentColor();
  if (drvSpeedArc && !isOverspeed) {
    lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(acc), LV_PART_INDICATOR);
  }
  if (drvModeBadge) {
    const char *mNames[3] = {"ECO MODE", "COMFORT", "SPORT+"};
    setLabelIfChanged(drvModeBadge, mNames[vs.mode]);
    lv_obj_set_style_text_color(drvModeBadge, lv_color_hex(acc), 0);
  }
  const char gears[4] = {'P', 'R', 'N', 'D'};
  for (int i = 0; i < 4; ++i) {
    if (!drvGearBtns[i]) continue;
    bool active = (vs.gear == gears[i]);
    uint32_t gc = (active && vs.gear == 'P') ? VISTEON_ORANGE : (active ? acc : pal().surfaceAlt);
    lv_obj_set_style_bg_color(drvGearBtns[i], lv_color_hex(gc), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvGearBtns[i], 0),
                                lv_color_hex(active ? 0x06090F : pal().text), 0);
  }
  for (int i = 0; i < 3; ++i) {
    if (!drvModeBtns[i]) continue;
    bool active = (vs.mode == i);
    uint32_t mc = (i == 0) ? pal().green : ((i == 1) ? pal().cyan : VISTEON_ORANGE);
    lv_obj_set_style_bg_color(drvModeBtns[i], lv_color_hex(active ? mc : pal().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvModeBtns[i], 0),
                                lv_color_hex(active ? 0x06090F : pal().text), 0);
  }
  if (drvDemoBtn) {
    lv_obj_set_style_bg_color(drvDemoBtn, lv_color_hex(vs.demoDrive ? pal().green : pal().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvDemoBtn, 0),
                                lv_color_hex(vs.demoDrive ? 0x06090F : pal().text), 0);
  }
  if (drvHazardBtn) {
    lv_obj_set_style_bg_color(drvHazardBtn, lv_color_hex(vs.hazard ? pal().red : pal().surfaceAlt), 0);
  }
  if (drvLeftBtn) {
    lv_obj_set_style_bg_color(drvLeftBtn, lv_color_hex(vs.leftBlinker ? pal().amber : pal().surfaceAlt), 0);
  }
  if (drvRightBtn) {
    lv_obj_set_style_bg_color(drvRightBtn, lv_color_hex(vs.rightBlinker ? pal().amber : pal().surfaceAlt), 0);
  }
}

static void buildPageDrive() {
  lv_obj_t *leftCard = makeCard(pageContainer, 12, 6, 254, 362);

  drvModeBadge = makeLabel(leftCard, "COMFORT", FONT_SM, modeAccentColor(), 16, 12);

  drvSpeedLimitBox = lv_obj_create(leftCard);
  lv_obj_set_pos(drvSpeedLimitBox, 206, 8);
  lv_obj_set_size(drvSpeedLimitBox, 36, 36);
  lv_obj_clear_flag(drvSpeedLimitBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(drvSpeedLimitBox, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(drvSpeedLimitBox, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(drvSpeedLimitBox, lv_color_hex(pal().red), 0);
  lv_obj_set_style_border_width(drvSpeedLimitBox, 3, 0);
  drvSpeedLimitLbl = lv_label_create(drvSpeedLimitBox);
  lv_label_set_text_fmt(drvSpeedLimitLbl, "%d", displayLimit());
  lv_obj_set_style_text_font(drvSpeedLimitLbl, FONT_XS, 0);
  lv_obj_set_style_text_color(drvSpeedLimitLbl, lv_color_hex(0x0F172A), 0);
  lv_obj_center(drvSpeedLimitLbl);

  drvSpeedArc = lv_arc_create(leftCard);
  lv_obj_set_size(drvSpeedArc, 212, 212);
  lv_obj_set_pos(drvSpeedArc, 21, 38);
  lv_arc_set_rotation(drvSpeedArc, 135);
  lv_arc_set_bg_angles(drvSpeedArc, 0, 270);
  lv_arc_set_range(drvSpeedArc, 0, 220);
  lv_arc_set_value(drvSpeedArc, (int)vs.speedKmh);
  lv_obj_remove_style(drvSpeedArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(drvSpeedArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(drvSpeedArc, 13, LV_PART_MAIN);
  lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_arc_width(drvSpeedArc, 13, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(modeAccentColor()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(drvSpeedArc, true, LV_PART_INDICATOR);

  drvPowerArc = lv_arc_create(leftCard);
  lv_obj_set_size(drvPowerArc, 174, 174);
  lv_obj_set_pos(drvPowerArc, 40, 57);
  lv_arc_set_rotation(drvPowerArc, 135);
  lv_arc_set_bg_angles(drvPowerArc, 0, 270);
  lv_arc_set_mode(drvPowerArc, LV_ARC_MODE_SYMMETRICAL);
  lv_arc_set_range(drvPowerArc, -60, 180);
  lv_arc_set_value(drvPowerArc, (int)vs.powerKw);
  lv_obj_remove_style(drvPowerArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(drvPowerArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(drvPowerArc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(pal().surfaceDeep), LV_PART_MAIN);
  lv_obj_set_style_arc_width(drvPowerArc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(pal().green), LV_PART_INDICATOR);

  drvSpeedVal = lv_label_create(leftCard);
  lv_label_set_text_fmt(drvSpeedVal, "%d", displaySpeed());
  lv_obj_set_style_text_font(drvSpeedVal, FONT_HERO, 0);
  lv_obj_set_style_text_color(drvSpeedVal, lv_color_hex(pal().text), 0);
  lv_obj_set_width(drvSpeedVal, 150);
  lv_obj_set_style_text_align(drvSpeedVal, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvSpeedVal, 52, 108);

  drvSpeedUnit = lv_label_create(leftCard);
  lv_label_set_text(drvSpeedUnit, vs.useMph ? "MPH" : "KM/H");
  lv_obj_set_style_text_font(drvSpeedUnit, FONT_SM, 0);
  lv_obj_set_style_text_color(drvSpeedUnit, lv_color_hex(pal().muted), 0);
  lv_obj_set_width(drvSpeedUnit, 100);
  lv_obj_set_style_text_align(drvSpeedUnit, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvSpeedUnit, 77, 162);

  drvPowerVal = lv_label_create(leftCard);
  lv_label_set_text(drvPowerVal, "0 kW");
  lv_obj_set_style_text_font(drvPowerVal, FONT_XS, 0);
  lv_obj_set_style_text_color(drvPowerVal, lv_color_hex(pal().muted), 0);
  lv_obj_set_width(drvPowerVal, 120);
  lv_obj_set_style_text_align(drvPowerVal, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvPowerVal, 67, 184);

  makeLabel(leftCard, "THROTTLE", FONT_XS, pal().muted, 16, 254);
  drvThrottleSld = lv_slider_create(leftCard);
  lv_obj_set_pos(drvThrottleSld, 16, 274);
  lv_obj_set_size(drvThrottleSld, 222, 10);
  lv_slider_set_range(drvThrottleSld, 0, 200);
  lv_slider_set_value(drvThrottleSld, (int)vs.targetSpeedKmh, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvThrottleSld, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(drvThrottleSld, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(drvThrottleSld, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(drvThrottleSld, [](lv_event_t *e) {
    vs.demoDrive = false;
    float val = (float)lv_slider_get_value(lv_event_get_target(e));
    if (val > 0.0f && vs.gear != 'D') {
      vs.gear = 'D';
      sendBlePacket("EVT:GEAR:D");
    }
    vs.targetSpeedKmh = val;
    refreshDriveStyles();
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  const char *gStr[4] = {"P", "R", "N", "D"};
  for (int i = 0; i < 4; ++i) {
    drvGearBtns[i] = makeBtn(leftCard, gStr[i], 14 + i * 58, 308, 52, 40,
      [](lv_event_t *e) {
        uintptr_t idx = (uintptr_t)lv_event_get_user_data(e);
        const char gMap[4] = {'P', 'R', 'N', 'D'};
        vs.gear = gMap[idx];
        if (vs.gear == 'P' || vs.gear == 'N') {
          vs.demoDrive = false;
          vs.targetSpeedKmh = 0.0f;
          if (drvThrottleSld) lv_slider_set_value(drvThrottleSld, 0, LV_ANIM_OFF);
        } else if (vs.gear == 'R') {
          vs.demoDrive = false;
          vs.targetSpeedKmh = 15.0f;
        }
        refreshDriveStyles();
        char pkt[16];
        snprintf(pkt, sizeof(pkt), "EVT:GEAR:%c", vs.gear);
        sendBlePacket(pkt);
      }, (void *)(uintptr_t)i);
  }

  lv_obj_t *centerCard = makeCard(pageContainer, 274, 6, 252, 362, pal().surfaceDeep);

  drvLeftBtn = makeBtn(centerCard, LV_SYMBOL_LEFT, 10, 10, 44, 34, [](lv_event_t *e) {
    vs.leftBlinker = !vs.leftBlinker;
    if (vs.leftBlinker) vs.rightBlinker = false;
    refreshDriveStyles();
  });
  drvDemoBtn = makeBtn(centerCard, "DEMO", 60, 10, 78, 34, [](lv_event_t *e) {
    vs.demoDrive = !vs.demoDrive;
    if (vs.demoDrive) {
      vs.gear = 'D';
      sendBlePacket("EVT:GEAR:D");
      triggerAlert("AUTOPILOT DEMO ENGAGED", pal().green, 2500);
    } else {
      vs.targetSpeedKmh = 0.0f;
      if (drvThrottleSld) lv_slider_set_value(drvThrottleSld, 0, LV_ANIM_OFF);
      triggerAlert("DEMO MODE PAUSED", pal().muted, 2000);
    }
    refreshDriveStyles();
  });
  drvHazardBtn = makeBtn(centerCard, LV_SYMBOL_WARNING, 144, 10, 44, 34, [](lv_event_t *e) {
    vs.hazard = !vs.hazard;
    if (vs.hazard) triggerAlert("HAZARD LIGHTS ACTIVE", pal().red, 3500);
    refreshDriveStyles();
  });
  drvRightBtn = makeBtn(centerCard, LV_SYMBOL_RIGHT, 194, 10, 48, 34, [](lv_event_t *e) {
    vs.rightBlinker = !vs.rightBlinker;
    if (vs.rightBlinker) vs.leftBlinker = false;
    refreshDriveStyles();
  });

  drvAdasCanvas = lv_obj_create(centerCard);
  lv_obj_remove_style_all(drvAdasCanvas);
  lv_obj_set_pos(drvAdasCanvas, 6, 50);
  lv_obj_set_size(drvAdasCanvas, 240, 266);
  lv_obj_add_event_cb(drvAdasCanvas, drawAdasCanvasCb, LV_EVENT_DRAW_MAIN, nullptr);

  lv_obj_t *adasBar = makeCard(centerCard, 10, 322, 232, 30, pal().surface, 8);
  drvAdasFooter = makeLabel(adasBar, "PARKED  |  SHIFT TO D OR DEMO", FONT_XS, pal().cyan, 14, 7);

  // Right Top: Mini Live Navigation Card
  lv_obj_t *navCard = makeCard(pageContainer, 534, 6, 254, 106);
  lv_obj_add_flag(navCard, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(navCard, [](lv_event_t *e) { selectPage(PAGE_NAV); }, LV_EVENT_CLICKED, nullptr);

  drvNavArrowBox = makeCard(navCard, 12, 16, 48, 48, pal().surfaceAlt, 12);
  lv_obj_add_event_cb(drvNavArrowBox, drawNavArrowCb, LV_EVENT_DRAW_MAIN, nullptr);

  drvNavDistLbl   = makeLabel(navCard, "Navigation Ready", FONT_MD, pal().text, 70, 16, 174);
  drvNavStreetLbl = makeLabel(navCard, vs.navStreet, FONT_XS, pal().muted, 70, 42, 174);
  drvNavEtaLbl    = makeLabel(navCard, "Tap for HUD & Phone Calls", FONT_XS, pal().green, 12, 78, 232);

  // Right Middle: Mini Live Phone Media Card
  lv_obj_t *medCard = makeCard(pageContainer, 534, 120, 254, 124);
  makeLabel(medCard, LV_SYMBOL_AUDIO "  NOW PLAYING", FONT_XS, VISTEON_ORANGE, 14, 10);
  drvMediaTitle  = makeLabel(medCard, vs.mediaTitle, FONT_MD, pal().text, 14, 28, 226);
  drvMediaArtist = makeLabel(medCard, vs.mediaArtist, FONT_XS, pal().muted, 14, 50, 226);

  drvMediaProg = lv_bar_create(medCard);
  lv_obj_set_pos(drvMediaProg, 14, 70);
  lv_obj_set_size(drvMediaProg, 226, 4);
  lv_bar_set_value(drvMediaProg, vs.mediaProgress, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvMediaProg, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(drvMediaProg, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);

  makeBtn(medCard, LV_SYMBOL_PREV, 14, 82, 68, 32, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:PREV");
  });
  lv_obj_t *pBtn = makeBtn(medCard, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY,
                           92, 82, 70, 32, [](lv_event_t *e) {
    vs.mediaPlaying = !vs.mediaPlaying;
    sendBlePacket(vs.mediaPlaying ? "CMD:MEDIA:PLAY" : "CMD:MEDIA:PAUSE");
    if (drvMediaPlayLbl) setLabelIfChanged(drvMediaPlayLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  }, nullptr, VISTEON_ORANGE, 0x06090F);
  drvMediaPlayLbl = lv_obj_get_child(pBtn, 0);
  makeBtn(medCard, LV_SYMBOL_NEXT, 172, 82, 68, 32, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:NEXT");
  });

  // Right Bottom: EV Energy & Drive Modes (Battery Icon)
  lv_obj_t *evCard = makeCard(pageContainer, 534, 252, 254, 116);
  char socInit[40];
  snprintf(socInit, sizeof(socInit), "%s %d%%   |   %d %s",
           batterySymbolForPct((int)vs.socPercent), (int)vs.socPercent,
           vs.useMph ? (int)(vs.socPercent * 4.9f * 0.621f) : (int)(vs.socPercent * 4.9f),
           vs.useMph ? "mi" : "km");
  drvSocLbl = makeLabel(evCard, socInit, FONT_SM, pal().text, 14, 10);

  drvSocBar = lv_bar_create(evCard);
  lv_obj_set_pos(drvSocBar, 14, 34);
  lv_obj_set_size(drvSocBar, 226, 8);
  lv_bar_set_value(drvSocBar, (int)vs.socPercent, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvSocBar, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(drvSocBar, lv_color_hex(pal().green), LV_PART_INDICATOR);

  drvEffLbl = makeLabel(evCard, "14.2 kWh/100km  |  Trip 48.6 km", FONT_XS, pal().muted, 14, 48);

  const char *mLbls[3] = {"ECO", "COMFORT", "SPORT"};
  int mX[3] = {12, 84, 176};
  int mW[3] = {66, 86, 66};
  for (int i = 0; i < 3; ++i) {
    drvModeBtns[i] = makeBtn(evCard, mLbls[i], mX[i], 72, mW[i], 32,
      [](lv_event_t *e) {
        vs.mode = (DriveMode)(uintptr_t)lv_event_get_user_data(e);
        if (vs.demoDrive && vs.gear == 'D') {
          vs.targetSpeedKmh = (vs.mode == MODE_SPORT) ? 132.0f : ((vs.mode == MODE_COMFORT) ? 78.0f : 54.0f);
        }
        refreshDriveStyles();
        const char *mPkt[3] = {"EVT:MODE:ECO", "EVT:MODE:COMFORT", "EVT:MODE:SPORT"};
        sendBlePacket(mPkt[vs.mode]);
        const char *mMsg[3] = {"ECO EFFICIENCY MODE", "COMFORT DRIVE MODE", "SPORT+ PERFORMANCE"};
        triggerAlert(mMsg[vs.mode], modeAccentColor(), 2200);
      }, (void *)(uintptr_t)i);
  }

  refreshDriveStyles();
}

// -----------------------------------------------------------------------------
// Page 2: NAV / HUD (Full-Height) & Reactive Hands-Free Call Companion
// -----------------------------------------------------------------------------
static void refreshNavHudCard() {
  if (currentPage != PAGE_NAV) return;

  if (navBigArrowBox) {
    lv_obj_set_style_border_color(navBigArrowBox, lv_color_hex(vs.navActive ? pal().green : pal().border), 0);
    lv_obj_set_style_border_width(navBigArrowBox, vs.navActive ? 2 : 1, 0);
    lv_obj_invalidate(navBigArrowBox);
  }

  if (navStatusBadge && navStatusLbl) {
    lv_obj_set_style_bg_color(navStatusBadge, lv_color_hex(vs.navActive ? pal().green : pal().surfaceAlt), 0);
    lv_obj_set_style_text_color(navStatusLbl, lv_color_hex(vs.navActive ? 0x06090F : pal().muted), 0);
    setLabelIfChanged(navStatusLbl, vs.navActive ? "LIVE MAPS" : "STANDBY");
  }

  if (navBigDistLbl) {
    if (vs.navActive) {
      char dBuf[24];
      formatDistance(dBuf, sizeof(dBuf), vs.navDistM);
      lv_obj_set_style_text_font(navBigDistLbl, FONT_HERO, 0);
      lv_obj_set_style_text_color(navBigDistLbl, lv_color_hex(pal().text), 0);
      setLabelIfChanged(navBigDistLbl, dBuf);
    } else {
      lv_obj_set_style_text_font(navBigDistLbl, FONT_XL, 0);
      lv_obj_set_style_text_color(navBigDistLbl, lv_color_hex(pal().text), 0);
      setLabelIfChanged(navBigDistLbl, "Nav Ready");
    }
  }

  if (navBigActionLbl) {
    if (vs.navActive) {
      const char *act[4] = {"CONTINUE STRAIGHT", "TURN RIGHT", "TURN LEFT", "MAKE U-TURN"};
      setLabelIfChanged(navBigActionLbl, act[vs.navDir]);
      lv_obj_set_style_text_color(navBigActionLbl, lv_color_hex(pal().green), 0);
    } else {
      setLabelIfChanged(navBigActionLbl, bleConnected ? "WAITING FOR ROUTE" : "CONNECT PHONE APP");
      lv_obj_set_style_text_color(navBigActionLbl, lv_color_hex(pal().muted), 0);
    }
  }

  if (navBigStreetLbl) {
    setLabelIfChanged(navBigStreetLbl, vs.navStreet);
    lv_obj_set_style_text_color(navBigStreetLbl, lv_color_hex(vs.navActive ? pal().text : pal().muted), 0);
  }

  if (navProgBar) {
    lv_bar_set_value(navProgBar, vs.navActive ? vs.navRoutePct : 0, LV_ANIM_OFF);
  }
  if (navProgPctLbl) {
    char pBuf[12];
    if (vs.navActive) snprintf(pBuf, sizeof(pBuf), "%d%%", vs.navRoutePct);
    else              snprintf(pBuf, sizeof(pBuf), "--");
    setLabelIfChanged(navProgPctLbl, pBuf);
  }

  if (navEtaValLbl && navEtaSubLbl) {
    if (vs.navActive) {
      char eBuf[20];
      formatEta(eBuf, sizeof(eBuf), vs.navEtaMin);
      setLabelIfChanged(navEtaValLbl, eBuf);
      lv_obj_set_style_text_color(navEtaValLbl, lv_color_hex(pal().green), 0);
      setLabelIfChanged(navEtaSubLbl, "Est. Arrival");
    } else {
      setLabelIfChanged(navEtaValLbl, "--");
      lv_obj_set_style_text_color(navEtaValLbl, lv_color_hex(pal().muted), 0);
      setLabelIfChanged(navEtaSubLbl, "No active route");
    }
  }

  if (navRemValLbl && navRemSubLbl) {
    if (vs.navActive) {
      char rBuf[20];
      if (vs.navRemKm >= 100.0f) snprintf(rBuf, sizeof(rBuf), "%.0f km", vs.navRemKm);
      else                       snprintf(rBuf, sizeof(rBuf), "%.1f km", vs.navRemKm);
      setLabelIfChanged(navRemValLbl, rBuf);
      lv_obj_set_style_text_color(navRemValLbl, lv_color_hex(pal().cyan), 0);
      setLabelIfChanged(navRemSubLbl, "Distance left");
    } else {
      setLabelIfChanged(navRemValLbl, "--");
      lv_obj_set_style_text_color(navRemValLbl, lv_color_hex(pal().muted), 0);
      setLabelIfChanged(navRemSubLbl, "Start in Maps");
    }
  }

  if (navDestValLbl) {
    setLabelIfChanged(navDestValLbl, vs.navActive ? vs.navDest : "Google Maps\nCompanion");
    lv_obj_set_style_text_color(navDestValLbl, lv_color_hex(vs.navActive ? pal().text : pal().muted), 0);
  }

  vs.navDirty = false;
}

static void refreshNavCallCard() {
  if (currentPage != PAGE_NAV) return;

  const bool ringing = vs.callActive && !vs.callAnswered;
  const bool active  = vs.callActive && vs.callAnswered;

  if (navCallCard) {
    uint32_t bCol = pal().border;
    int bWidth = 1;
    if (ringing) {
      bCol = blinkState ? 0x10B981 : 0x047857;
      bWidth = 2;
    } else if (active) {
      bCol = pal().green;
      bWidth = 2;
    }
    lv_obj_set_style_border_color(navCallCard, lv_color_hex(bCol), 0);
    lv_obj_set_style_border_width(navCallCard, bWidth, 0);
  }

  if (navCallStatusLbl) {
    const char *st = ringing ? "INCOMING CALL..." : (active ? "CALL IN PROGRESS" : "HANDS-FREE CALLS");
    setLabelIfChanged(navCallStatusLbl, st);
    lv_obj_set_style_text_color(navCallStatusLbl, lv_color_hex(vs.callActive ? pal().green : pal().cyan), 0);
  }

  if (navCallerLbl) {
    setLabelIfChanged(navCallerLbl, vs.callActive ? vs.callerName : "Ready for Phone Calls");
  }

  if (navCallSubLbl) {
    if (ringing) {
      setLabelIfChanged(navCallSubLbl, "Tap ACCEPT or HANDS-FREE for loudspeaker");
      lv_obj_set_style_text_color(navCallSubLbl, lv_color_hex(pal().amber), 0);
    } else if (active) {
      setLabelIfChanged(navCallSubLbl, vs.callSpeaker ? "Audio Route: Loudspeaker (Hands-Free)" : "Audio Route: Phone Earpiece");
      lv_obj_set_style_text_color(navCallSubLbl, lv_color_hex(vs.callSpeaker ? pal().cyan : pal().muted), 0);
    } else {
      setLabelIfChanged(navCallSubLbl, bleConnected ? "BLE Call Monitor Active" : "Waiting for Companion App");
      lv_obj_set_style_text_color(navCallSubLbl, lv_color_hex(pal().muted), 0);
    }
  }

  if (navPhoneBattLbl) {
    char bBuf[24];
    if (vs.phoneBattery >= 0) {
      snprintf(bBuf, sizeof(bBuf), "%s %d%%", batterySymbolForPct(vs.phoneBattery), vs.phoneBattery);
    } else {
      snprintf(bBuf, sizeof(bBuf), "Phone: --");
    }
    setLabelIfChanged(navPhoneBattLbl, bBuf);
  }

  if (navSpeakerBtn && navSpeakerBtnLbl) {
    if (ringing) {
      lv_obj_set_style_bg_color(navSpeakerBtn, lv_color_hex(pal().cyan), 0);
      lv_obj_set_style_text_color(navSpeakerBtnLbl, lv_color_hex(0x06090F), 0);
      setLabelIfChanged(navSpeakerBtnLbl, LV_SYMBOL_VOLUME_MAX " HANDSFREE");
    } else if (active && vs.callSpeaker) {
      lv_obj_set_style_bg_color(navSpeakerBtn, lv_color_hex(VISTEON_ORANGE), 0);
      lv_obj_set_style_text_color(navSpeakerBtnLbl, lv_color_hex(0x06090F), 0);
      setLabelIfChanged(navSpeakerBtnLbl, LV_SYMBOL_VOLUME_MAX " SPKR ON");
    } else {
      lv_obj_set_style_bg_color(navSpeakerBtn, lv_color_hex(pal().surfaceAlt), 0);
      lv_obj_set_style_text_color(navSpeakerBtnLbl, lv_color_hex(pal().text), 0);
      setLabelIfChanged(navSpeakerBtnLbl, LV_SYMBOL_VOLUME_MAX " SPEAKER");
    }
  }

  vs.callDirty = false;
}

static void buildPageNav() {
  // Left Full-Height Card: Upgraded Heads-Up Navigation (452 x 362)
  lv_obj_t *hudCard = makeCard(pageContainer, 12, 6, 452, 362);
  makeLabel(hudCard, LV_SYMBOL_GPS "  HEADS-UP NAVIGATION", FONT_XS, pal().green, 18, 14);

  navStatusBadge = makeCard(hudCard, 344, 10, 90, 22, pal().surfaceAlt, 11);
  lv_obj_set_style_border_width(navStatusBadge, 0, 0);
  navStatusLbl = lv_label_create(navStatusBadge);
  lv_label_set_text(navStatusLbl, "STANDBY");
  lv_obj_set_style_text_font(navStatusLbl, FONT_XS, 0);
  lv_obj_set_style_text_color(navStatusLbl, lv_color_hex(pal().muted), 0);
  lv_obj_center(navStatusLbl);

  navBigArrowBox = makeCard(hudCard, 18, 44, 150, 150, pal().surfaceAlt, 22);
  lv_obj_add_event_cb(navBigArrowBox, drawNavArrowCb, LV_EVENT_DRAW_MAIN, nullptr);

  navBigDistLbl   = makeLabel(hudCard, "Nav Ready", FONT_XL, pal().text, 184, 46, 250);
  navBigActionLbl = makeLabel(hudCard, "WAITING FOR ROUTE", FONT_LG, pal().muted, 184, 104, 250);

  navBigStreetLbl = makeLabel(hudCard, vs.navStreet, FONT_MD, pal().muted, 184, 134, 250);
  lv_label_set_long_mode(navBigStreetLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(navBigStreetLbl, 60);

  makeLabel(hudCard, "ROUTE PROGRESS", FONT_XS, pal().muted, 18, 208);
  navProgPctLbl = makeLabel(hudCard, "--", FONT_XS, pal().cyan, 374, 208, 60);
  lv_obj_set_style_text_align(navProgPctLbl, LV_TEXT_ALIGN_RIGHT, 0);

  navProgBar = lv_bar_create(hudCard);
  lv_obj_set_pos(navProgBar, 18, 228);
  lv_obj_set_size(navProgBar, 416, 10);
  lv_bar_set_value(navProgBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(navProgBar, 5, LV_PART_MAIN);
  lv_obj_set_style_radius(navProgBar, 5, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(navProgBar, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(navProgBar, lv_color_hex(pal().green), LV_PART_INDICATOR);

  // Bottom Trio Telemetry Cards
  lv_obj_t *etaBox = makeCard(hudCard, 18, 252, 132, 96, pal().surfaceDeep, 12);
  makeLabel(etaBox, "ETA", FONT_XS, pal().muted, 12, 10);
  navEtaValLbl = makeLabel(etaBox, "--", FONT_XL, pal().muted, 12, 34, 108);
  navEtaSubLbl = makeLabel(etaBox, "No active route", FONT_XS, pal().muted, 12, 72, 108);

  lv_obj_t *remBox = makeCard(hudCard, 160, 252, 132, 96, pal().surfaceDeep, 12);
  makeLabel(remBox, "REMAINING", FONT_XS, pal().muted, 12, 10);
  navRemValLbl = makeLabel(remBox, "--", FONT_XL, pal().muted, 12, 34, 108);
  navRemSubLbl = makeLabel(remBox, "Start in Maps", FONT_XS, pal().muted, 12, 72, 108);

  lv_obj_t *destBox = makeCard(hudCard, 302, 252, 132, 96, pal().surfaceDeep, 12);
  makeLabel(destBox, "DESTINATION", FONT_XS, pal().muted, 12, 10);
  navDestValLbl = makeLabel(destBox, "Google Maps\nCompanion", FONT_SM, pal().muted, 12, 32, 108);
  lv_label_set_long_mode(navDestValLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_height(navDestValLbl, 56);

  // Right Top: Reactive Incoming Call & Hands-Free Loudspeaker Card
  navCallCard = makeCard(pageContainer, 474, 6, 314, 152);
  navCallStatusLbl = makeLabel(navCallCard, "HANDS-FREE CALLS", FONT_XS, pal().cyan, 14, 12);
  navPhoneBattLbl  = makeLabel(navCallCard, "Phone: --", FONT_XS, pal().muted, 196, 12, 104);
  lv_obj_set_style_text_align(navPhoneBattLbl, LV_TEXT_ALIGN_RIGHT, 0);

  navCallerLbl  = makeLabel(navCallCard, "Ready for Phone Calls", FONT_LG, pal().text, 14, 36, 286);
  navCallSubLbl = makeLabel(navCallCard, "BLE Call Monitor Active", FONT_XS, pal().muted, 14, 66, 286);

  navAcceptBtn = makeBtn(navCallCard, LV_SYMBOL_CALL " ACCEPT", 12, 94, 88, 44, [](lv_event_t *e) {
    vs.callActive   = true;
    vs.callAnswered = true;
    vs.callSpeaker  = false;
    vs.callDirty    = true;
    sendBlePacket("CMD:CALL:ACCEPT");
    triggerAlert("ACCEPTING CALL...", pal().green, 2500);
    refreshNavCallCard();
  }, nullptr, pal().green, 0x06090F);

  navSpeakerBtn = makeBtn(navCallCard, LV_SYMBOL_VOLUME_MAX " SPEAKER", 106, 94, 108, 44, [](lv_event_t *e) {
    if (!vs.callAnswered) {
      // Answer incoming call directly on loudspeaker
      vs.callActive   = true;
      vs.callAnswered = true;
      vs.callSpeaker  = true;
      vs.callDirty    = true;
      sendBlePacket("CMD:CALL:ACCEPT_SPEAKER");
      triggerAlert("ANSWERING ON LOUDSPEAKER...", pal().cyan, 2800);
    } else {
      // Toggle loudspeaker mid-call
      vs.callSpeaker = !vs.callSpeaker;
      vs.callDirty   = true;
      sendBlePacket(vs.callSpeaker ? "CMD:CALL:SPEAKER:ON" : "CMD:CALL:SPEAKER:OFF");
      triggerAlert(vs.callSpeaker ? "LOUDSPEAKER ON" : "LOUDSPEAKER OFF",
                   vs.callSpeaker ? VISTEON_ORANGE : pal().muted, 2200);
    }
    refreshNavCallCard();
  }, nullptr, pal().surfaceAlt, pal().text);
  navSpeakerBtnLbl = lv_obj_get_child(navSpeakerBtn, 0);

  navEndBtn = makeBtn(navCallCard, LV_SYMBOL_CLOSE " END", 220, 94, 82, 44, [](lv_event_t *e) {
    vs.callActive   = false;
    vs.callAnswered = false;
    vs.callSpeaker  = false;
    vs.callDirty    = true;
    strncpy(vs.callerName, "No Active Call", sizeof(vs.callerName) - 1);
    vs.callerName[sizeof(vs.callerName) - 1] = '\0';
    sendBlePacket("CMD:CALL:DECLINE");
    triggerAlert("CALL ENDED", pal().muted, 2000);
    refreshNavCallCard();
  }, nullptr, pal().red, 0xFFFFFF);

  // Right Bottom: Scrollable 5-Item Recent Phone Notification Stream
  lv_obj_t *notifCard = makeCard(pageContainer, 474, 166, 314, 202);
  makeLabel(notifCard, LV_SYMBOL_BELL "  RECENT NOTIFICATIONS", FONT_XS, pal().amber, 14, 10);
  navNotifCountLbl = makeLabel(notifCard, "0 / 5", FONT_XS, pal().muted, 258, 10);

  navNotifList = lv_obj_create(notifCard);
  lv_obj_set_pos(navNotifList, 10, 30);
  lv_obj_set_size(navNotifList, 294, 116);
  lv_obj_set_scroll_dir(navNotifList, LV_DIR_VER);
  lv_obj_set_style_bg_color(navNotifList, lv_color_hex(pal().surfaceDeep), 0);
  lv_obj_set_style_border_color(navNotifList, lv_color_hex(pal().border), 0);
  lv_obj_set_style_border_width(navNotifList, 1, 0);
  lv_obj_set_style_radius(navNotifList, 10, 0);
  lv_obj_set_style_pad_all(navNotifList, 6, 0);
#if LV_USE_FLEX
  lv_obj_set_flex_flow(navNotifList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(navNotifList, 6, 0);
#endif

  makeBtn(notifCard, "DISMISS", 10, 154, 138, 36, [](lv_event_t *e) {
    clearNotifications();
    rebuildNotifList();
  });
  makeBtn(notifCard, "PING PHONE", 156, 154, 148, 36, [](lv_event_t *e) {
    sendBlePacket("CMD:PHONE:PING");
    triggerAlert("PINGING PAIRED PHONE...", VISTEON_ORANGE, 3000);
  }, nullptr, VISTEON_ORANGE, 0x06090F);

  refreshNavHudCard();
  refreshNavCallCard();
  rebuildNotifList();
}

// -----------------------------------------------------------------------------
// Page 3: MEDIA (Infotainment Audio Hub)
// -----------------------------------------------------------------------------
static void refreshMediaEqBtns() {
  for (int i = 0; i < 4; ++i) {
    if (!medEqBtns[i]) continue;
    bool act = (vs.eqPreset == i);
    lv_obj_set_style_bg_color(medEqBtns[i], lv_color_hex(act ? VISTEON_ORANGE : pal().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(medEqBtns[i], 0),
                                lv_color_hex(act ? 0x06090F : pal().text), 0);
  }
}

static void buildPageMedia() {
  lv_obj_t *artCard = makeCard(pageContainer, 12, 6, 278, 362);
  makeLabel(artCard, "SPATIAL AUDIO ENGINE", FONT_XS, pal().muted, 18, 12);

  medDiscCanvas = lv_obj_create(artCard);
  lv_obj_remove_style_all(medDiscCanvas);
  lv_obj_set_pos(medDiscCanvas, 49, 40);
  lv_obj_set_size(medDiscCanvas, 180, 180);
  lv_obj_add_event_cb(medDiscCanvas, drawMediaDiscCb, LV_EVENT_DRAW_MAIN, nullptr);

  makeLabel(artCard, "LIVE SPECTRUM", FONT_XS, pal().cyan, 18, 232);
  medEqCanvas = lv_obj_create(artCard);
  lv_obj_remove_style_all(medEqCanvas);
  lv_obj_set_pos(medEqCanvas, 18, 254);
  lv_obj_set_size(medEqCanvas, 242, 92);
  lv_obj_add_event_cb(medEqCanvas, drawMediaEqCb, LV_EVENT_DRAW_MAIN, nullptr);

  lv_obj_t *ctrlCard = makeCard(pageContainer, 300, 6, 488, 362);
  makeLabel(ctrlCard, LV_SYMBOL_BLUETOOTH "  COMPANION LIVE AUDIO", FONT_XS, pal().cyan, 24, 16);

  medTitleLbl  = makeLabel(ctrlCard, vs.mediaTitle, FONT_XL, pal().text, 24, 42, 440);
  medArtistLbl = makeLabel(ctrlCard, vs.mediaArtist, FONT_LG, pal().muted, 24, 80, 440);

  medProgSlider = lv_slider_create(ctrlCard);
  lv_obj_set_pos(medProgSlider, 24, 128);
  lv_obj_set_size(medProgSlider, 440, 10);
  lv_slider_set_range(medProgSlider, 0, 100);
  lv_slider_set_value(medProgSlider, vs.mediaProgress, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(medProgSlider, [](lv_event_t *e) {
    vs.mediaProgress = (int)lv_slider_get_value(lv_event_get_target(e));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(medProgSlider, [](lv_event_t *e) {
    vs.mediaProgress = (int)lv_slider_get_value(lv_event_get_target(e));
    char pkt[28];
    snprintf(pkt, sizeof(pkt), "CMD:MEDIA:SEEK:%d", vs.mediaProgress);
    sendBlePacket(pkt);
  }, LV_EVENT_RELEASED, nullptr);

  medTimeLbl = makeLabel(ctrlCard, "0%  |  Waiting for phone playback", FONT_XS, pal().muted, 24, 146);

  makeBtn(ctrlCard, LV_SYMBOL_PREV "  PREV", 24, 174, 124, 52, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:PREV");
  });
  lv_obj_t *playBtn = makeBtn(ctrlCard, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY",
                              162, 170, 164, 60, [](lv_event_t *e) {
    vs.mediaPlaying = !vs.mediaPlaying;
    sendBlePacket(vs.mediaPlaying ? "CMD:MEDIA:PLAY" : "CMD:MEDIA:PAUSE");
    if (medPlayBtnLbl) setLabelIfChanged(medPlayBtnLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY");
  }, nullptr, VISTEON_ORANGE, 0x06090F, 14);
  medPlayBtnLbl = lv_obj_get_child(playBtn, 0);
  makeBtn(ctrlCard, "NEXT  " LV_SYMBOL_NEXT, 340, 174, 124, 52, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:NEXT");
  });

  medVolLbl = makeLabel(ctrlCard, LV_SYMBOL_VOLUME_MAX "  70%", FONT_SM, pal().text, 24, 250);
  medVolSlider = lv_slider_create(ctrlCard);
  lv_obj_set_pos(medVolSlider, 114, 254);
  lv_obj_set_size(medVolSlider, 270, 12);
  lv_slider_set_range(medVolSlider, 0, 100);
  lv_slider_set_value(medVolSlider, vs.mediaVolume, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(pal().cyan), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(medVolSlider, [](lv_event_t *e) {
    vs.mediaMuted = false;
    vs.mediaVolume = (int)lv_slider_get_value(lv_event_get_target(e));
    uint32_t now = millis();
    if (now - lastVolTxMs >= 100) {
      lastVolTxMs = now;
      char pkt[24];
      snprintf(pkt, sizeof(pkt), "CMD:VOL:%d", vs.mediaVolume);
      sendBlePacket(pkt);
    }
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(medVolSlider, [](lv_event_t *e) {
    vs.mediaMuted = false;
    vs.mediaVolume = (int)lv_slider_get_value(lv_event_get_target(e));
    char pkt[24];
    snprintf(pkt, sizeof(pkt), "CMD:VOL:%d", vs.mediaVolume);
    sendBlePacket(pkt);
  }, LV_EVENT_RELEASED, nullptr);

  makeBtn(ctrlCard, LV_SYMBOL_MUTE, 400, 242, 64, 34, [](lv_event_t *e) {
    vs.mediaMuted = !vs.mediaMuted;
    sendBlePacket(vs.mediaMuted ? "CMD:VOL:MUTE" : "CMD:VOL:UNMUTE");
  });

  makeLabel(ctrlCard, "CABIN ACOUSTICS", FONT_XS, pal().muted, 24, 290);
  const char *eqNames[4] = {"STUDIO", "BASS+", "VOCAL", "SPATIAL"};
  for (int i = 0; i < 4; ++i) {
    medEqBtns[i] = makeBtn(ctrlCard, eqNames[i], 24 + i * 112, 312, 104, 34,
      [](lv_event_t *e) {
        vs.eqPreset = (int)(uintptr_t)lv_event_get_user_data(e);
        refreshMediaEqBtns();
        const char *p[4] = {"CMD:EQ:STUDIO", "CMD:EQ:BASS", "CMD:EQ:VOCAL", "CMD:EQ:SPATIAL"};
        sendBlePacket(p[vs.eqPreset]);
      }, (void *)(uintptr_t)i);
  }
  refreshMediaEqBtns();
}

// -----------------------------------------------------------------------------
// Page 4: VEHICLE (TPMS Chassis, Body Controls, Dual-Zone HVAC, HV Pack)
// -----------------------------------------------------------------------------
static void refreshVehicleWidgets() {
  if (currentPage != PAGE_VEHICLE) return;
  if (vehChassisCanvas) lv_obj_invalidate(vehChassisCanvas);

  const char *posTag[4] = {"FL", "FR", "RL", "RR"};
  for (int i = 0; i < 4; ++i) {
    if (!vehTpmsLbl[i]) continue;
    char buf[24];
    snprintf(buf, sizeof(buf), "%s\n%.1f PSI", posTag[i], vs.tpms[i]);
    setLabelIfChanged(vehTpmsLbl[i], buf);
    uint32_t c = (vs.tpms[i] < 29.0f) ? pal().red : ((vs.tpms[i] < 32.0f) ? pal().amber : pal().green);
    lv_obj_set_style_text_color(vehTpmsLbl[i], lv_color_hex(c), 0);
  }
  if (vehLightBtn) lv_obj_set_style_bg_color(vehLightBtn, lv_color_hex(vs.headlightsOn ? pal().amber : pal().surfaceAlt), 0);
  if (vehLockBtn)  lv_obj_set_style_bg_color(vehLockBtn,  lv_color_hex(vs.doorsLocked ? pal().cyan : pal().surfaceAlt), 0);
  if (vehTrunkBtn) lv_obj_set_style_bg_color(vehTrunkBtn, lv_color_hex(vs.trunkOpen ? VISTEON_ORANGE : pal().surfaceAlt), 0);

  if (vehDrvTempLbl) {
    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%d C", vs.tempDriver);
    setLabelIfChanged(vehDrvTempLbl, tBuf);
    lv_obj_set_style_text_color(vehDrvTempLbl, lv_color_hex(vs.tempDriver >= 24 ? VISTEON_ORANGE : pal().cyan), 0);
  }
  if (vehPasTempLbl) {
    char tBuf[12];
    snprintf(tBuf, sizeof(tBuf), "%d C", vs.tempPassenger);
    setLabelIfChanged(vehPasTempLbl, tBuf);
    lv_obj_set_style_text_color(vehPasTempLbl, lv_color_hex(vs.tempPassenger >= 24 ? VISTEON_ORANGE : pal().cyan), 0);
  }
  if (vehFanLbl) {
    char fBuf[16];
    snprintf(fBuf, sizeof(fBuf), "FAN LVL %d", vs.fanSpeed);
    setLabelIfChanged(vehFanLbl, fBuf);
  }
  if (vehAcBtn)      lv_obj_set_style_bg_color(vehAcBtn, lv_color_hex(vs.acOn ? pal().cyan : pal().surfaceAlt), 0);
  if (vehAutoBtn)    lv_obj_set_style_bg_color(vehAutoBtn, lv_color_hex(vs.autoClimate ? pal().green : pal().surfaceAlt), 0);
  if (vehDefrostBtn) lv_obj_set_style_bg_color(vehDefrostBtn, lv_color_hex(vs.defrostOn ? VISTEON_ORANGE : pal().surfaceAlt), 0);
  if (vehSeatBtn) {
    char sBuf[12];
    snprintf(sBuf, sizeof(sBuf), "SEAT %d", vs.seatHeat);
    lv_obj_set_style_bg_color(vehSeatBtn, lv_color_hex(vs.seatHeat > 0 ? pal().red : pal().surfaceAlt), 0);
    setLabelIfChanged(lv_obj_get_child(vehSeatBtn, 0), sBuf);
  }
  if (vehRegenBtn) {
    const char *rTxt[3] = {"REGEN: LOW", "REGEN: STD", "ONE-PEDAL"};
    setLabelIfChanged(lv_obj_get_child(vehRegenBtn, 0), rTxt[vs.regenLevel]);
  }
}

static void buildPageVehicle() {
  lv_obj_t *carCard = makeCard(pageContainer, 12, 6, 386, 362);
  makeLabel(carCard, "CHASSIS & TIRE PRESSURE (TPMS)", FONT_XS, pal().muted, 16, 12);

  vehChassisCanvas = lv_obj_create(carCard);
  lv_obj_remove_style_all(vehChassisCanvas);
  lv_obj_set_pos(vehChassisCanvas, 108, 34);
  lv_obj_set_size(vehChassisCanvas, 170, 250);
  lv_obj_add_event_cb(vehChassisCanvas, drawVehicleChassisCb, LV_EVENT_DRAW_MAIN, nullptr);

  int tx[4] = {16, 294, 16, 294};
  int ty[4] = {76, 76, 198, 198};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *box = makeCard(carCard, tx[i], ty[i], 78, 52, pal().surfaceAlt, 10);
    vehTpmsLbl[i] = lv_label_create(box);
    lv_obj_set_style_text_font(vehTpmsLbl[i], FONT_XS, 0);
    lv_obj_set_style_text_align(vehTpmsLbl[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(vehTpmsLbl[i]);
  }

  vehLightBtn = makeBtn(carCard, "LIGHTS", 14, 304, 84, 42, [](lv_event_t *e) {
    vs.headlightsOn = !vs.headlightsOn;
    refreshVehicleWidgets();
  });
  vehLockBtn = makeBtn(carCard, "LOCK", 104, 304, 84, 42, [](lv_event_t *e) {
    vs.doorsLocked = !vs.doorsLocked;
    triggerAlert(vs.doorsLocked ? "DOORS LOCKED" : "DOORS UNLOCKED", pal().cyan, 2000);
    refreshVehicleWidgets();
  });
  vehTrunkBtn = makeBtn(carCard, "TRUNK", 194, 304, 84, 42, [](lv_event_t *e) {
    vs.trunkOpen = !vs.trunkOpen;
    if (vs.trunkOpen) triggerAlert("REAR TRUNK OPEN", VISTEON_ORANGE, 3000);
    refreshVehicleWidgets();
  });
  makeBtn(carCard, "TEST PSI", 284, 304, 88, 42, [](lv_event_t *e) {
    if (vs.tpms[3] > 30.0f) {
      vs.tpms[3] = 24.5f;
      triggerAlert("LOW TIRE PRESSURE: REAR RIGHT 24.5 PSI", pal().red, 5000);
    } else {
      vs.tpms[3] = 34.1f;
      triggerAlert("ALL TIRES NOMINAL", pal().green, 2500);
    }
    refreshVehicleWidgets();
  });

  lv_obj_t *hvacCard = makeCard(pageContainer, 408, 6, 380, 224);
  makeLabel(hvacCard, "DUAL-ZONE CABIN CLIMATE", FONT_XS, pal().muted, 16, 12);

  makeLabel(hvacCard, "DRIVER", FONT_XS, pal().muted, 58, 36);
  makeBtn(hvacCard, "-", 16, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempDriver > 16) vs.tempDriver--;
    refreshVehicleWidgets();
  });
  vehDrvTempLbl = makeLabel(hvacCard, "21 C", FONT_LG, pal().cyan, 62, 66);
  makeBtn(hvacCard, "+", 126, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempDriver < 30) vs.tempDriver++;
    refreshVehicleWidgets();
  });

  makeLabel(hvacCard, "PASSENGER", FONT_XS, pal().muted, 246, 36);
  makeBtn(hvacCard, "-", 214, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempPassenger > 16) vs.tempPassenger--;
    refreshVehicleWidgets();
  });
  vehPasTempLbl = makeLabel(hvacCard, "22 C", FONT_LG, pal().cyan, 260, 66);
  makeBtn(hvacCard, "+", 324, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempPassenger < 30) vs.tempPassenger++;
    refreshVehicleWidgets();
  });

  vehFanLbl = makeLabel(hvacCard, "FAN LVL 3", FONT_XS, pal().text, 16, 120);
  lv_obj_t *fanSld = lv_slider_create(hvacCard);
  lv_obj_set_pos(fanSld, 110, 122);
  lv_obj_set_size(fanSld, 250, 12);
  lv_slider_set_range(fanSld, 0, 5);
  lv_slider_set_value(fanSld, vs.fanSpeed, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(fanSld, lv_color_hex(pal().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(fanSld, lv_color_hex(pal().cyan), LV_PART_INDICATOR);
  lv_obj_add_event_cb(fanSld, [](lv_event_t *e) {
    vs.fanSpeed = (int)lv_slider_get_value(lv_event_get_target(e));
    refreshVehicleWidgets();
  }, LV_EVENT_VALUE_CHANGED, nullptr);

  vehAcBtn = makeBtn(hvacCard, "A/C", 16, 162, 82, 42, [](lv_event_t *e) {
    vs.acOn = !vs.acOn; refreshVehicleWidgets();
  });
  vehAutoBtn = makeBtn(hvacCard, "AUTO", 104, 162, 82, 42, [](lv_event_t *e) {
    vs.autoClimate = !vs.autoClimate; refreshVehicleWidgets();
  });
  vehDefrostBtn = makeBtn(hvacCard, "DEFROST", 192, 162, 84, 42, [](lv_event_t *e) {
    vs.defrostOn = !vs.defrostOn; refreshVehicleWidgets();
  });
  vehSeatBtn = makeBtn(hvacCard, "SEAT 0", 282, 162, 82, 42, [](lv_event_t *e) {
    vs.seatHeat = (vs.seatHeat + 1) % 4; refreshVehicleWidgets();
  });

  lv_obj_t *hvCard = makeCard(pageContainer, 408, 238, 380, 130);
  makeLabel(hvCard, LV_SYMBOL_BATTERY_FULL "  HV POWERTRAIN & THERMAL", FONT_XS, pal().green, 16, 12);
  vehHvStatsLbl = makeLabel(hvCard, "Pack: 398.4 V   |   Temp: 31 C   |   SOH: 99%\nOdometer: 12480.4 km   |   Trip A: 48.6 km",
                            FONT_SM, pal().text, 16, 36);
  vehRegenBtn = makeBtn(hvCard, "REGEN: STD", 16, 82, 170, 36, [](lv_event_t *e) {
    vs.regenLevel = (vs.regenLevel + 1) % 3;
    refreshVehicleWidgets();
  }, nullptr, pal().surfaceAlt);
  makeBtn(hvCard, "RESET TRIP A", 198, 82, 166, 36, [](lv_event_t *e) {
    vs.tripKm = 0.0f;
    triggerAlert("TRIP A ODOMETER RESET", pal().cyan, 2000);
  });

  refreshVehicleWidgets();
}

// -----------------------------------------------------------------------------
// Page 5: CONNECT (Wi-Fi + On-Screen Keyboard + BLE 5.0 Console)
// -----------------------------------------------------------------------------
static void openWifiPasswordModal(const char *ssid) {
  strncpy(pendingWifiSsid, ssid, sizeof(pendingWifiSsid) - 1);
  pendingWifiSsid[sizeof(pendingWifiSsid) - 1] = '\0';

#if LV_USE_KEYBOARD && LV_USE_TEXTAREA
  if (kbModal) { lv_obj_del(kbModal); kbModal = nullptr; }

  kbModal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(kbModal, 800, 480);
  lv_obj_set_pos(kbModal, 0, 0);
  lv_obj_clear_flag(kbModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(kbModal, lv_color_hex(0x04070C), 0);
  lv_obj_set_style_bg_opa(kbModal, LV_OPA_90, 0);
  lv_obj_set_style_border_width(kbModal, 0, 0);
  lv_obj_set_style_radius(kbModal, 0, 0);

  char hdr[64];
  snprintf(hdr, sizeof(hdr), "Enter Password for: %s", pendingWifiSsid);
  makeLabel(kbModal, hdr, FONT_LG, 0xF4F7FB, 40, 24);

  kbTextArea = lv_textarea_create(kbModal);
  lv_obj_set_pos(kbTextArea, 40, 64);
  lv_obj_set_size(kbTextArea, 720, 52);
  lv_textarea_set_one_line(kbTextArea, true);
  lv_textarea_set_placeholder_text(kbTextArea, "Wi-Fi Password...");
  lv_obj_set_style_text_font(kbTextArea, FONT_LG, 0);

  lv_obj_t *kb = lv_keyboard_create(kbModal);
  lv_obj_set_size(kb, 780, 320);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_keyboard_set_textarea(kb, kbTextArea);

  lv_obj_add_event_cb(kb, [](lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
      const char *pw = lv_textarea_get_text(kbTextArea);
      startWifiConnection(pendingWifiSsid, pw);
      lv_obj_del(kbModal);
      kbModal = nullptr;
    } else if (code == LV_EVENT_CANCEL) {
      lv_obj_del(kbModal);
      kbModal = nullptr;
    }
  }, LV_EVENT_ALL, nullptr);
#else
  triggerAlert("ENABLE LV_USE_KEYBOARD IN lv_conf.h", pal().amber, 3000);
#endif
}

static void buildPageConnect() {
  lv_obj_t *wifiCard = makeCard(pageContainer, 12, 6, 384, 362);
  conWifiStatusLbl = makeLabel(wifiCard, LV_SYMBOL_WIFI "  WI-FI MANAGER", FONT_MD, pal().cyan, 16, 12);
  conWifiDetailLbl = makeLabel(wifiCard, "Checking radio...", FONT_XS, pal().muted, 16, 38, 352);

  makeBtn(wifiCard, "CONNECT ANSHIK", 16, 66, 138, 38, [](lv_event_t *e) {
    startWifiConnection(SAVED_SSID, SAVED_PASSWORD);
  }, nullptr, VISTEON_ORANGE, 0x06090F);

  makeBtn(wifiCard, LV_SYMBOL_REFRESH " SCAN", 160, 66, 98, 38, [](lv_event_t *e) {
    scanWifiNetworks();
  });

  makeBtn(wifiCard, "SYNC METEO", 264, 66, 104, 38, [](lv_event_t *e) {
    if (WiFi.status() == WL_CONNECTED) {
      triggerStandaloneWeatherSync();
      triggerAlert("SYNCING LOCATION & WEATHER...", pal().cyan, 2500);
    } else {
      triggerAlert("CONNECT WI-FI FIRST", pal().amber, 2200);
    }
  }, nullptr, pal().cyan, 0x06090F);

  conWifiList = lv_obj_create(wifiCard);
  lv_obj_set_pos(conWifiList, 16, 114);
  lv_obj_set_size(conWifiList, 352, 234);
  lv_obj_set_scroll_dir(conWifiList, LV_DIR_VER);
  lv_obj_set_style_bg_color(conWifiList, lv_color_hex(pal().surfaceDeep), 0);
  lv_obj_set_style_border_color(conWifiList, lv_color_hex(pal().border), 0);
  lv_obj_set_style_radius(conWifiList, 10, 0);
  lv_obj_set_style_pad_all(conWifiList, 6, 0);
  makeLabel(conWifiList, "Tap SCAN to discover 2.4 GHz networks.\nTap SYNC METEO to refresh location & weather.", FONT_XS, pal().muted, 8, 8);

  lv_obj_t *bleCard = makeCard(pageContainer, 406, 6, 382, 362);
  conBleStatusLbl = makeLabel(bleCard, bleConnected ? LV_SYMBOL_BLUETOOTH "  PHONE CONNECTED" : LV_SYMBOL_BLUETOOTH "  ADVERTISING: Visteon-Lunet",
                              FONT_MD, bleConnected ? pal().green : pal().cyan, 16, 12);
  conBlePhoneLbl  = makeLabel(bleCard, "MTU 247  |  Phone Batt: --", FONT_XS, pal().muted, 16, 38);

  makeBtn(bleCard, "RESTART ADV", 16, 66, 114, 38, [](lv_event_t *e) {
    restartBleAdvertising();
  });
  makeBtn(bleCard, "SYNC PHONE", 136, 66, 114, 38, [](lv_event_t *e) {
    sendBlePacket("CMD:SYNC:REQ");
    triggerAlert(bleConnected ? "SYNC REQUESTED" : "WAITING FOR APP", bleConnected ? pal().green : pal().amber, 2200);
  }, nullptr, pal().cyan, 0x06090F);
  makeBtn(bleCard, "PING PHONE", 256, 66, 110, 38, [](lv_event_t *e) {
    sendBlePacket("CMD:PHONE:PING");
    triggerAlert("PING PACKET SENT", VISTEON_ORANGE, 2200);
  });

  lv_obj_t *termBox = makeCard(bleCard, 16, 114, 350, 234, pal().surfaceDeep, 10);
  lv_obj_add_flag(termBox, LV_OBJ_FLAG_SCROLLABLE);
  conBleLogLbl = makeLabel(termBox, "", FONT_XS, pal().text, 10, 8, 326);
  lv_label_set_long_mode(conBleLogLbl, LV_LABEL_LONG_WRAP);
  bleLogDirty = true;
}

// -----------------------------------------------------------------------------
// Page 6: SYSTEM (Telemetry Chart, Memory, Units, Touch Pad, Boot Replay)
// -----------------------------------------------------------------------------
static void buildPageSystem() {
  lv_obj_t *chartCard = makeCard(pageContainer, 12, 6, 434, 230);
  makeLabel(chartCard, "REAL-TIME TELEMETRY  (ORANGE: SPEED  |  GREEN: POWER)", FONT_XS, pal().muted, 16, 10);

#if LV_USE_CHART
  sysChart = lv_chart_create(chartCard);
  lv_obj_set_pos(sysChart, 14, 34);
  lv_obj_set_size(sysChart, 406, 184);
  lv_chart_set_type(sysChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(sysChart, 36);
  lv_chart_set_range(sysChart, LV_CHART_AXIS_PRIMARY_Y, 0, 200);
  lv_chart_set_div_line_count(sysChart, 4, 6);
  lv_obj_set_style_bg_color(sysChart, lv_color_hex(pal().surfaceDeep), 0);
  lv_obj_set_style_border_color(sysChart, lv_color_hex(pal().border), 0);
  lv_obj_set_style_line_width(sysChart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(sysChart, 0, LV_PART_INDICATOR);

  serSpeed = lv_chart_add_series(sysChart, lv_color_hex(VISTEON_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
  serPower = lv_chart_add_series(sysChart, lv_color_hex(pal().green), LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 36; ++i) {
    lv_chart_set_next_value(sysChart, serSpeed, (lv_coord_t)vs.speedKmh);
    lv_chart_set_next_value(sysChart, serPower, (lv_coord_t)(vs.powerKw + 60));
  }
#else
  makeLabel(chartCard, "Enable LV_USE_CHART in lv_conf.h for live graph.", FONT_SM, pal().muted, 20, 100);
#endif

  lv_obj_t *hwCard = makeCard(pageContainer, 12, 244, 434, 124);
  makeLabel(hwCard, "ESP32-S3 HARDWARE DIAGNOSTICS", FONT_XS, pal().cyan, 16, 12);
  sysMemLbl = makeLabel(hwCard, "CPU: 240 MHz Dual-Core LX7  |  ST7262 800x480\nHeap: -- KB  |  OPI PSRAM: -- KB\nUptime: -- s",
                        FONT_SM, pal().text, 16, 36);

  lv_obj_t *setCard = makeCard(pageContainer, 456, 6, 332, 362);
  makeLabel(setCard, "CLUSTER SETTINGS & DEMO TOOLS", FONT_XS, pal().muted, 16, 12);

  lv_obj_t *uBtn = makeBtn(setCard, vs.useMph ? "UNITS: MPH (IMPERIAL)" : "UNITS: KM/H (METRIC)",
                           16, 40, 300, 42, [](lv_event_t *e) {
    vs.useMph = !vs.useMph;
    if (sysUnitBtnLbl) setLabelIfChanged(sysUnitBtnLbl, vs.useMph ? "UNITS: MPH (IMPERIAL)" : "UNITS: KM/H (METRIC)");
    triggerAlert(vs.useMph ? "SWITCHED TO MPH" : "SWITCHED TO KM/H", pal().cyan, 2000);
  });
  sysUnitBtnLbl = lv_obj_get_child(uBtn, 0);

  makeBtn(setCard, lightTheme ? "SWITCH TO OBSIDIAN DARK" : "SWITCH TO CERAMIC LIGHT",
          16, 90, 300, 42, [](lv_event_t *e) {
    lightTheme = !lightTheme;
    buildShell();
  });

  makeBtn(setCard, "REPLAY VISTEON BOOT INTRO", 16, 140, 300, 42, [](lv_event_t *e) {
    showBootSplash();
  }, nullptr, VISTEON_ORANGE, 0x06090F);

  makeLabel(setCard, "GT911 CAPACITIVE TOUCH TEST PAD", FONT_XS, pal().green, 16, 198);
  lv_obj_t *pad = makeCard(setCard, 16, 218, 300, 128, pal().surfaceDeep, 12);
  sysTouchLbl = makeLabel(pad, "Touch screen to verify GT911 (X, Y)", FONT_XS, pal().muted, 12, 10);

  sysTouchDot = lv_obj_create(pad);
  lv_obj_set_size(sysTouchDot, 16, 16);
  lv_obj_clear_flag(sysTouchDot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(sysTouchDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sysTouchDot, lv_color_hex(VISTEON_ORANGE), 0);
  lv_obj_set_style_border_width(sysTouchDot, 0, 0);
  lv_obj_center(sysTouchDot);
}

// -----------------------------------------------------------------------------
// Page Router & Persistent Chrome Shell
// -----------------------------------------------------------------------------
static void selectPage(PageId page) {
  currentPage = page;
  resetPagePointers();
  if (pageContainer) lv_obj_clean(pageContainer);

  for (int i = 0; i < PAGE_COUNT; ++i) {
    if (!dockBtns[i]) continue;
    bool active = (i == (int)page);
    lv_obj_set_style_bg_color(dockBtns[i], lv_color_hex(active ? pal().surfaceAlt : pal().surface), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(dockBtns[i], 0),
                                lv_color_hex(active ? VISTEON_ORANGE : pal().muted), 0);
    if (dockIndicators[i]) {
      lv_obj_set_style_bg_opa(dockIndicators[i], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
  }

  switch (page) {
    case PAGE_DRIVE:   buildPageDrive();   break;
    case PAGE_NAV:     buildPageNav();     break;
    case PAGE_MEDIA:   buildPageMedia();   break;
    case PAGE_VEHICLE: buildPageVehicle(); break;
    case PAGE_CONNECT: buildPageConnect(); break;
    case PAGE_SYSTEM:  buildPageSystem();  break;
    default: break;
  }
}

static void buildShell() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(scr, lv_color_hex(pal().bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  lastPillBgHex  = 0xFFFFFFFF;
  lastPillTxtHex = 0xFFFFFFFF;

  topBar = lv_obj_create(scr);
  lv_obj_set_pos(topBar, 0, 0);
  lv_obj_set_size(topBar, 800, 46);
  lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(topBar, lv_color_hex(pal().bg), 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_style_pad_all(topBar, 0, 0);

  lv_obj_t *logoHit = lv_obj_create(topBar);
  lv_obj_remove_style_all(logoHit);
  lv_obj_set_pos(logoHit, 14, 10);
  lv_obj_set_size(logoHit, 32, 26);
  lv_obj_add_flag(logoHit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoHit, drawVisteonLogoCb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(logoHit, [](lv_event_t *e) { showBootSplash(); }, LV_EVENT_CLICKED, nullptr);

  makeLabel(topBar, "VISTEON", FONT_MD, pal().text, 52, 13);
  makeLabel(topBar, "LUNET", FONT_XS, VISTEON_ORANGE, 128, 16);

  // Interactive Center Dynamic Pill (Tapping opens NAV / PHONE for Calls & Alerts)
  dynamicPill = makeCard(topBar, 200, 6, 396, 34, pal().surface, 17);
  lv_obj_add_flag(dynamicPill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dynamicPill, [](lv_event_t *e) {
    if (currentPage != PAGE_NAV) selectPage(PAGE_NAV);
  }, LV_EVENT_CLICKED, nullptr);

  dynamicPillLabel = lv_label_create(dynamicPill);
  lv_label_set_text(dynamicPillLabel, "--:--   |   Syncing Time   |   Weather --");
  lv_obj_set_style_text_font(dynamicPillLabel, FONT_SM, 0);
  lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(pal().text), 0);
  lv_obj_set_width(dynamicPillLabel, 376);
  lv_obj_set_style_text_align(dynamicPillLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(dynamicPillLabel, LV_LABEL_LONG_DOT);
  lv_obj_center(dynamicPillLabel);

  topWifiIcon    = makeLabel(topBar, LV_SYMBOL_WIFI, FONT_SM, pal().muted, 608, 15);
  topBleIcon     = makeLabel(topBar, LV_SYMBOL_BLUETOOTH, FONT_SM, pal().cyan, 634, 15);
  topSupplyLabel = makeLabel(topBar, LV_SYMBOL_BATTERY_FULL " 100%", FONT_XS, pal().green, 656, 16);

  lv_obj_t *themeIconBtn = makeBtn(topBar, "", 744, 7, 42, 32, [](lv_event_t *e) {
    lightTheme = !lightTheme;
    buildShell();
  }, nullptr, pal().surfaceAlt, pal().text, 16);
  lv_obj_add_event_cb(themeIconBtn, drawThemeIconCb, LV_EVENT_DRAW_MAIN_END, nullptr);

  pageContainer = lv_obj_create(scr);
  lv_obj_remove_style_all(pageContainer);
  lv_obj_set_pos(pageContainer, 0, 46);
  lv_obj_set_size(pageContainer, 800, 376);
  lv_obj_clear_flag(pageContainer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *dock = makeCard(scr, 44, 424, 712, 50, pal().surface, 18);
  const char *dockNames[PAGE_COUNT] = {
    LV_SYMBOL_HOME " DRIVE",
    LV_SYMBOL_GPS " NAV",
    LV_SYMBOL_AUDIO " MEDIA",
    LV_SYMBOL_BATTERY_FULL " VEHICLE",
    LV_SYMBOL_WIFI " CONNECT",
    LV_SYMBOL_SETTINGS " SYSTEM"
  };

  for (int i = 0; i < PAGE_COUNT; ++i) {
    int bx = 8 + i * 116;
    dockBtns[i] = makeBtn(dock, dockNames[i], bx, 5, 112, 40,
      [](lv_event_t *e) {
        selectPage((PageId)(uintptr_t)lv_event_get_user_data(e));
      }, (void *)(uintptr_t)i, pal().surface, pal().muted, 12);
    lv_obj_set_style_border_width(dockBtns[i], 0, 0);

    dockIndicators[i] = lv_obj_create(dockBtns[i]);
    lv_obj_set_size(dockIndicators[i], 36, 3);
    lv_obj_align(dockIndicators[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_color(dockIndicators[i], lv_color_hex(VISTEON_ORANGE), 0);
    lv_obj_set_style_bg_opa(dockIndicators[i], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dockIndicators[i], 0, 0);
    lv_obj_set_style_radius(dockIndicators[i], 2, 0);
  }

  selectPage(currentPage);
}

// -----------------------------------------------------------------------------
// BLE 5.0 GATT Server, Ring Queue & Bidirectional Protocol Parser
// -----------------------------------------------------------------------------
static void appendBleLog(const char *prefix, const char *msg) {
  if (bleLogCount >= 8) {
    for (int i = 0; i < 7; ++i) strcpy(bleLogLines[i], bleLogLines[i + 1]);
    bleLogCount = 7;
  }
  snprintf(bleLogLines[bleLogCount++], sizeof(bleLogLines[0]), "[%s] %.72s", prefix, msg);
  bleLogDirty = true;
}

static void enqueueBleRx(const char *msg) {
  portENTER_CRITICAL(&bleQueueMux);
  if (bleQCount < BLE_QUEUE_SIZE) {
    strncpy(bleQueue[bleQHead], msg, BLE_MSG_MAX - 1);
    bleQueue[bleQHead][BLE_MSG_MAX - 1] = '\0';
    bleQHead = (bleQHead + 1) % BLE_QUEUE_SIZE;
    bleQCount++;
  }
  portEXIT_CRITICAL(&bleQueueMux);
}

static bool dequeueBleRx(char *outBuf, size_t maxLen) {
  bool ok = false;
  portENTER_CRITICAL(&bleQueueMux);
  if (bleQCount > 0) {
    strncpy(outBuf, bleQueue[bleQTail], maxLen - 1);
    outBuf[maxLen - 1] = '\0';
    bleQTail = (bleQTail + 1) % BLE_QUEUE_SIZE;
    bleQCount--;
    ok = true;
  }
  portEXIT_CRITICAL(&bleQueueMux);
  return ok;
}

class LunetServerCallbacks : public BLEServerCallbacks {
#if defined(CONFIG_BLUEDROID_ENABLED)
  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {
    bleConnected      = true;
    bleRestartPending = false;
    bleSyncReqPending = true;
    bleConnectedAt    = millis();
    bleActiveConnId   = param ? param->connect.conn_id : server->getConnId();
    if (param) {
      memcpy(blePeerAddr, param->connect.remote_bda, 6);
      blePeerAddrValid = true;
    }
    Serial.printf("[BLE] Companion app connected (connId=%u)\n", (unsigned)bleActiveConnId);
  }
#else
  void onConnect(BLEServer *server) override {
    bleConnected      = true;
    bleRestartPending = false;
    bleSyncReqPending = true;
    bleConnectedAt    = millis();
    bleActiveConnId   = server->getConnId();
    Serial.printf("[BLE] Companion app connected (connId=%u)\n", (unsigned)bleActiveConnId);
  }
#endif
  void onDisconnect(BLEServer *server) override {
    bleConnected      = false;
    bleSyncReqPending = false;
    bleRestartPending = true;
    blePeerAddrValid  = false;
    bleDisconnectAt   = millis();
    Serial.println("[BLE] Companion app disconnected");
  }
};

class LunetCharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    value.trim();
    if (value.length() == 0) return;
    enqueueBleRx(value.c_str());
  }
};

static void sendBlePacket(const char *packet) {
  appendBleLog("TX", packet);
  if (!bleCharacteristic) return;
  bleCharacteristic->setValue(String(packet));
  if (bleConnected) {
    bleCharacteristic->notify();
    Serial.printf("[BLE TX] %s\n", packet);
  }
}

static void startBle() {
  if (bleReady) return;
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(247);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new LunetServerCallbacks());

  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
  bleCharacteristic = service->createCharacteristic(
      BLE_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ     |
      BLECharacteristic::PROPERTY_WRITE    |
      BLECharacteristic::PROPERTY_WRITE_NR |
      BLECharacteristic::PROPERTY_NOTIFY   |
      BLECharacteristic::PROPERTY_INDICATE);
  bleCharacteristic->setValue("Visteon Lunet OS 3.0 Ready");
  bleCharacteristic->setCallbacks(new LunetCharCallbacks());
  bleCharacteristic->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  bleReady = true;
  appendBleLog("SYS", "BLE 5.0 Active (MTU 247)");
}

// Force-disconnects any active phone session and cleanly restarts BLE advertising
static void restartBleAdvertising() {
  if (!bleReady) {
    startBle();
    return;
  }

  BLEDevice::getAdvertising()->stop();

  const bool hasActiveLink = bleConnected || (bleServer && bleServer->getConnectedCount() > 0);
  if (bleServer && hasActiveLink) {
    appendBleLog("SYS", "Disconnecting active phone...");
    triggerAlert("DISCONNECTING & RESTARTING ADV...", pal().amber, 2500);

    uint16_t curConnId = bleServer->getConnId();
    bleServer->disconnect(bleActiveConnId);
    if (curConnId != bleActiveConnId) {
      bleServer->disconnect(curConnId);
    }

#if defined(CONFIG_BLUEDROID_ENABLED)
    if (blePeerAddrValid) {
      esp_ble_gap_disconnect(blePeerAddr);
    }
#endif

    bleConnected      = false;
    bleSyncReqPending = false;
    blePeerAddrValid  = false;
    bleRestartPending = true;
    bleDisconnectAt   = millis();
  } else {
    delay(60);
    BLEDevice::startAdvertising();
    bleRestartPending = false;
    appendBleLog("SYS", "Advertising restarted");
    triggerAlert("BLE ADVERTISING RESTARTED", pal().cyan, 2000);
  }
}

static int splitPipes(const String &src, String *outParts, int maxParts) {
  int count = 0;
  int start = 0;
  while (count < maxParts - 1) {
    int idx = src.indexOf('|', start);
    if (idx < 0) break;
    outParts[count++] = src.substring(start, idx);
    start = idx + 1;
  }
  outParts[count++] = src.substring(start);
  return count;
}

static void processBleCommand(const char *command) {
  appendBleLog("RX", command);
  String cmd(command);
  cmd.trim();

  if (cmd.startsWith("TIME:")) {
    strncpy(vs.timeStr, cmd.substring(5).c_str(), sizeof(vs.timeStr) - 1);
    vs.timeStr[sizeof(vs.timeStr) - 1] = '\0';
    bleTimeOverride = true;
  } else if (cmd.startsWith("DATE:")) {
    strncpy(vs.dateStr, cmd.substring(5).c_str(), sizeof(vs.dateStr) - 1);
    vs.dateStr[sizeof(vs.dateStr) - 1] = '\0';
    bleTimeOverride = true;
  } else if (cmd.startsWith("LOC:")) {
    // Optional phone GPS push: LOC:<lat>|<lon>|<optionalCity>
    String parts[3];
    int n = splitPipes(cmd.substring(4), parts, 3);
    if (n >= 2) {
      float lat = parts[0].toFloat();
      float lon = parts[1].toFloat();
      if (fabsf(lat) > 0.001f || fabsf(lon) > 0.001f) {
        vs.latitude  = lat;
        vs.longitude = lon;
        manualLocOverride = true;
        if (n >= 3 && parts[2].length() > 0) {
          strncpy(vs.cityStr, parts[2].c_str(), sizeof(vs.cityStr) - 1);
          vs.cityStr[sizeof(vs.cityStr) - 1] = '\0';
        }
        if (WiFi.status() == WL_CONNECTED) triggerStandaloneWeatherSync();
      }
    }
  } else if (cmd.startsWith("WEATHER:")) {
    // Supports "WEATHER:28C Clear" or "WEATHER:28|Clear"
    String payload = cmd.substring(8);
    payload.trim();
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (n == 2) {
      snprintf(vs.weatherStr, sizeof(vs.weatherStr), "%sC %s", parts[0].c_str(), parts[1].c_str());
    } else if (payload.length() > 0) {
      strncpy(vs.weatherStr, payload.c_str(), sizeof(vs.weatherStr) - 1);
      vs.weatherStr[sizeof(vs.weatherStr) - 1] = '\0';
    }
  } else if (cmd.startsWith("PBATT:")) {
    vs.phoneBattery = clampInt((int)cmd.substring(6).toInt(), 0, 100);
    vs.callDirty = true;
  } else if (cmd.startsWith("BATTERY:")) {
    vs.phoneBattery = clampInt((int)cmd.substring(8).toInt(), 0, 100);
    vs.callDirty = true;
  } else if (cmd.startsWith("SPEED:")) {
    vs.demoDrive = false;
    float spd = clampFloat(cmd.substring(6).toFloat(), 0.0f, 220.0f);
    if (spd > 0.0f && vs.gear == 'P') vs.gear = 'D';
    vs.targetSpeedKmh = spd;
    if (drvThrottleSld) lv_slider_set_value(drvThrottleSld, clampInt((int)spd, 0, 200), LV_ANIM_OFF);
    refreshDriveStyles();
  } else if (cmd.startsWith("LIMIT:")) {
    vs.speedLimitKmh = clampInt((int)cmd.substring(6).toInt(), 20, 200);
    if (drvSpeedLimitLbl) {
      char lBuf[12];
      snprintf(lBuf, sizeof(lBuf), "%d", displayLimit());
      setLabelIfChanged(drvSpeedLimitLbl, lBuf);
    }
  } else if (cmd.startsWith("GEAR:")) {
    String g = cmd.substring(5);
    g.toUpperCase();
    if (g.length() > 0) {
      char c = g[0];
      if (c == 'P' || c == 'R' || c == 'N' || c == 'D') {
        vs.gear = c;
        if (c == 'P' || c == 'N') { vs.demoDrive = false; vs.targetSpeedKmh = 0.0f; }
        refreshDriveStyles();
      }
    }
  } else if (cmd.startsWith("MODE:")) {
    String m = cmd.substring(5);
    if (m.equalsIgnoreCase("ECO")) vs.mode = MODE_ECO;
    else if (m.equalsIgnoreCase("SPORT")) vs.mode = MODE_SPORT;
    else vs.mode = MODE_COMFORT;
    refreshDriveStyles();
  } else if (cmd.startsWith("SOC:")) {
    vs.socPercent = clampFloat(cmd.substring(4).toFloat(), 0.0f, 100.0f);
  } else if (cmd.equalsIgnoreCase("NOTIF_CLEAR")) {
    clearNotifications();
  } else if (cmd.startsWith("NOTIF_HIST:")) {
    String payload = cmd.substring(11);
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (n == 2) pushNotification(parts[0].c_str(), parts[1].c_str(), false);
    else        pushNotification("Phone", payload.c_str(), false);
  } else if (cmd.startsWith("NOTIF:")) {
    String payload = cmd.substring(6);
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (n == 2) pushNotification(parts[0].c_str(), parts[1].c_str(), true);
    else        pushNotification("Phone", payload.c_str(), true);
  } else if (cmd.startsWith("CALL:")) {
    String payload = cmd.substring(5);
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (parts[0].equalsIgnoreCase("SPEAKER:ON")) {
      vs.callSpeaker = true;
      vs.callDirty   = true;
    } else if (parts[0].equalsIgnoreCase("SPEAKER:OFF")) {
      vs.callSpeaker = false;
      vs.callDirty   = true;
    } else if (parts[0].equalsIgnoreCase("INCOMING") || parts[0].equalsIgnoreCase("RINGING")) {
      vs.callActive   = true;
      vs.callAnswered = false;
      vs.callSpeaker  = false;
      vs.callDirty    = true;
      strncpy(vs.callerName, (n == 2 && parts[1].length() > 0) ? parts[1].c_str() : "Unknown Caller",
              sizeof(vs.callerName) - 1);
      vs.callerName[sizeof(vs.callerName) - 1] = '\0';
    } else if (parts[0].equalsIgnoreCase("ACTIVE") || parts[0].equalsIgnoreCase("OFFHOOK")) {
      vs.callActive   = true;
      vs.callAnswered = true;
      vs.callDirty    = true;
      if (n == 2 && parts[1].length() > 0) {
        strncpy(vs.callerName, parts[1].c_str(), sizeof(vs.callerName) - 1);
        vs.callerName[sizeof(vs.callerName) - 1] = '\0';
      }
      triggerAlert("CALL CONNECTED", pal().green, 2500);
    } else if (parts[0].equalsIgnoreCase("ENDED") || parts[0].equalsIgnoreCase("IDLE")) {
      vs.callActive   = false;
      vs.callAnswered = false;
      vs.callSpeaker  = false;
      vs.callDirty    = true;
      strncpy(vs.callerName, "No Active Call", sizeof(vs.callerName) - 1);
      vs.callerName[sizeof(vs.callerName) - 1] = '\0';
      triggerAlert("CALL ENDED", pal().muted, 2000);
    } else if (payload.length() > 0) {
      vs.callActive   = true;
      vs.callAnswered = false;
      vs.callSpeaker  = false;
      vs.callDirty    = true;
      strncpy(vs.callerName, payload.c_str(), sizeof(vs.callerName) - 1);
      vs.callerName[sizeof(vs.callerName) - 1] = '\0';
    }
    if (currentPage == PAGE_NAV) refreshNavCallCard();
  } else if (cmd.startsWith("MEDIA:")) {
    // Format: MEDIA:<title>|<artist>|<PLAY|PAUSE>|<progress_0_100>|<vol_0_100>
    String parts[5];
    int n = splitPipes(cmd.substring(6), parts, 5);
    vs.liveMediaSynced = true;
    vs.lastMediaRxMs   = millis();
    if (n >= 1 && parts[0].length() > 0) {
      strncpy(vs.mediaTitle, parts[0].c_str(), sizeof(vs.mediaTitle) - 1);
      vs.mediaTitle[sizeof(vs.mediaTitle) - 1] = '\0';
    }
    if (n >= 2) {
      strncpy(vs.mediaArtist, parts[1].c_str(), sizeof(vs.mediaArtist) - 1);
      vs.mediaArtist[sizeof(vs.mediaArtist) - 1] = '\0';
    }
    if (n >= 3) {
      vs.mediaPlaying = parts[2].equalsIgnoreCase("PLAY");
    }
    if (n >= 4) {
      vs.mediaProgress = clampInt((int)parts[3].toInt(), 0, 100);
      if (drvMediaProg) lv_bar_set_value(drvMediaProg, vs.mediaProgress, LV_ANIM_OFF);
      if (medProgSlider && !lv_slider_is_dragged(medProgSlider)) {
        lv_slider_set_value(medProgSlider, vs.mediaProgress, LV_ANIM_OFF);
      }
    }
    if (n >= 5) {
      vs.mediaVolume = clampInt((int)parts[4].toInt(), 0, 100);
      if (medVolSlider && !lv_slider_is_dragged(medVolSlider)) {
        lv_slider_set_value(medVolSlider, vs.mediaVolume, LV_ANIM_OFF);
      }
    }
    setLabelIfChanged(drvMediaTitle, vs.mediaTitle);
    setLabelIfChanged(drvMediaArtist, vs.mediaArtist);
    setLabelIfChanged(medTitleLbl, vs.mediaTitle);
    setLabelIfChanged(medArtistLbl, vs.mediaArtist);
    setLabelIfChanged(drvMediaPlayLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    setLabelIfChanged(medPlayBtnLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY");
  } else if (cmd.equalsIgnoreCase("NAV:IDLE")) {
    vs.navActive   = false;
    vs.navDir      = NAV_STRAIGHT;
    vs.navDistM    = 0;
    vs.navEtaMin   = 0;
    vs.navRemKm    = 0.0f;
    vs.navRoutePct = 0;
    vs.navDirty    = true;
    strncpy(vs.navStreet, "Start a route in Google Maps on phone", sizeof(vs.navStreet) - 1);
    vs.navStreet[sizeof(vs.navStreet) - 1] = '\0';
    strncpy(vs.navDest, "Waiting for Maps", sizeof(vs.navDest) - 1);
    vs.navDest[sizeof(vs.navDest) - 1] = '\0';
    if (drvNavArrowBox) lv_obj_invalidate(drvNavArrowBox);
    if (currentPage == PAGE_NAV) refreshNavHudCard();
  } else if (cmd.startsWith("NAV:")) {
    // Format: NAV:<STRAIGHT|LEFT|RIGHT|UTURN>|<distM>|<street>|<etaMin>|<remKm>|<optionalDest>
    String payload = cmd.substring(4);
    String parts[6];
    int n = splitPipes(payload, parts, 6);
    vs.navActive = true;
    vs.navDirty  = true;
    if (n >= 6 && parts[5].length() > 0) {
      strncpy(vs.navDest, parts[5].c_str(), sizeof(vs.navDest) - 1);
    } else {
      strncpy(vs.navDest, "Google Maps Live", sizeof(vs.navDest) - 1);
    }
    vs.navDest[sizeof(vs.navDest) - 1] = '\0';

    if (n >= 3) {
      if (parts[0].equalsIgnoreCase("LEFT"))       vs.navDir = NAV_LEFT;
      else if (parts[0].equalsIgnoreCase("RIGHT")) vs.navDir = NAV_RIGHT;
      else if (parts[0].equalsIgnoreCase("UTURN")) vs.navDir = NAV_UTURN;
      else                                         vs.navDir = NAV_STRAIGHT;

      vs.navDistM = clampInt((int)parts[1].toInt(), 0, 999999);
      strncpy(vs.navStreet, parts[2].c_str(), sizeof(vs.navStreet) - 1);
      vs.navStreet[sizeof(vs.navStreet) - 1] = '\0';
      if (n >= 4) vs.navEtaMin = clampInt((int)parts[3].toInt(), 0, 9999);
      if (n >= 5) vs.navRemKm  = clampFloat(parts[4].toFloat(), 0.0f, 9999.0f);
      vs.navRoutePct = (vs.navRemKm > 0.1f) ? clampInt(100 - (int)(vs.navRemKm * 3.0f), 10, 95) : 50;
    } else {
      strncpy(vs.navStreet, payload.c_str(), sizeof(vs.navStreet) - 1);
      vs.navStreet[sizeof(vs.navStreet) - 1] = '\0';
    }
    if (drvNavArrowBox) lv_obj_invalidate(drvNavArrowBox);
    if (currentPage == PAGE_NAV) refreshNavHudCard();
  }
}

// -----------------------------------------------------------------------------
// Wi-Fi Engine & Scan Handling
// -----------------------------------------------------------------------------
static void startWifiConnection(const char *ssid, const char *password) {
  if (!ssid || !ssid[0]) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  delay(40);
  WiFi.begin(ssid, password ? password : "");
  strncpy(activeWifiName, ssid, sizeof(activeWifiName) - 1);
  activeWifiName[sizeof(activeWifiName) - 1] = '\0';
  weatherEverSynced = false;
  triggerAlert("CONNECTING WI-FI...", pal().cyan, 3000);
}

static void scanWifiNetworks() {
  if (wifiScanInProgress) return;
  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(false, false);
    delay(20);
  }
  WiFi.scanNetworks(true, false);
  wifiScanInProgress = true;
  if (conWifiList) {
    lv_obj_clean(conWifiList);
    makeLabel(conWifiList, "Scanning 2.4 GHz networks...", FONT_XS, pal().cyan, 8, 8);
  }
}

static void updateWifiScanResults() {
  if (!wifiScanInProgress) return;
  int16_t count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  wifiScanInProgress = false;
  scannedWifiCount = 0;

  if (count <= 0) {
    WiFi.scanDelete();
    if (conWifiList) {
      lv_obj_clean(conWifiList);
      makeLabel(conWifiList, "No 2.4 GHz networks found.", FONT_XS, pal().amber, 8, 8);
    }
    return;
  }

  for (int i = 0; i < count && scannedWifiCount < 16; ++i) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    strncpy(scannedWifi[scannedWifiCount].ssid, s.c_str(), 32);
    scannedWifi[scannedWifiCount].ssid[32] = '\0';
    scannedWifi[scannedWifiCount].rssi = WiFi.RSSI(i);
    scannedWifi[scannedWifiCount].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    scannedWifiCount++;
  }
  WiFi.scanDelete();

  if (!conWifiList || currentPage != PAGE_CONNECT) return;
  lv_obj_clean(conWifiList);

  for (int i = 0; i < scannedWifiCount; ++i) {
    char rowTxt[64];
    snprintf(rowTxt, sizeof(rowTxt), "%s  (%d dBm)%s",
             scannedWifi[i].ssid, scannedWifi[i].rssi, scannedWifi[i].open ? " [OPEN]" : "");
    lv_obj_t *row = makeBtn(conWifiList, rowTxt, 2, 2 + i * 38, 332, 34,
      [](lv_event_t *e) {
        uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
        if (idx >= scannedWifiCount) return;
        if (scannedWifi[idx].open) startWifiConnection(scannedWifi[idx].ssid, "");
        else openWifiPasswordModal(scannedWifi[idx].ssid);
      }, (void *)(uintptr_t)i, pal().surfaceAlt, pal().text, 8);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 10, 0);
  }
}

// -----------------------------------------------------------------------------
// 30 Hz Physics, Overspeed Red Flash & Reactive In-Place UI Updates
// -----------------------------------------------------------------------------
static void stepPhysicsAndAnimations(uint32_t now) {
  if (bootActive) {
    uint32_t el = now - bootStartMs;
    int pct = clampInt((int)(el * 100 / 2100), 0, 100);
    if (bootBar) lv_bar_set_value(bootBar, pct, LV_ANIM_OFF);
    if (el >= 2200) {
      bootActive = false;
      if (bootOverlay) { lv_obj_del(bootOverlay); bootOverlay = nullptr; bootBar = nullptr; }
    }
  }

  bool blinkToggled = false;
  if (now - lastBlinkMs >= 450) {
    lastBlinkMs = now;
    blinkState = !blinkState;
    blinkToggled = true;
  }

  float dt = 0.033f;
  if (vs.demoDrive && vs.gear == 'D') {
    float t = now * 0.001f;
    float base = (vs.mode == MODE_SPORT) ? 112.0f : ((vs.mode == MODE_COMFORT) ? 74.0f : 52.0f);
    float wave = 26.0f * sinf(t * 0.28f) + 12.0f * cosf(t * 0.65f);
    vs.targetSpeedKmh = clampFloat(base + wave, 18.0f, 185.0f);
    vs.leadDistM = 36.0f + 16.0f * sinf(t * 0.42f + 1.0f);
  } else if (vs.gear == 'P' || vs.gear == 'N') {
    vs.targetSpeedKmh = 0.0f;
  }

  float diff = vs.targetSpeedKmh - vs.speedKmh;
  float accelRate = (vs.mode == MODE_SPORT) ? 2.6f : ((vs.mode == MODE_COMFORT) ? 1.8f : 1.2f);
  if (fabsf(diff) > 0.3f) {
    vs.speedKmh += clampFloat(diff * 0.09f, -2.6f, accelRate);
  } else {
    vs.speedKmh = vs.targetSpeedKmh;
  }

  bool prevBraking = vs.braking;
  vs.braking = (diff < -6.0f);
  if (prevBraking != vs.braking) vs.adasDirty = true;

  float targetPower = 0.0f;
  if (vs.gear == 'D' || vs.gear == 'R') {
    targetPower = (diff > 1.0f) ? (vs.speedKmh * 0.6f + diff * 2.2f)
                                : (vs.braking ? (-25.0f - (vs.regenLevel * 12.0f)) : (vs.speedKmh * 0.14f));
  }
  targetPower = clampFloat(targetPower, -60.0f, 180.0f);
  vs.powerKw += (targetPower - vs.powerKw) * 0.16f;
  if (vs.gear == 'P' && fabsf(vs.powerKw) < 0.4f) vs.powerKw = 0.0f;

  // Overspeed Detection & Full-Screen Pulsing Red Warning Flash
  bool prevOverspeed = isOverspeed;
  isOverspeed = (vs.speedKmh > (float)vs.speedLimitKmh + 0.5f);
  if (prevOverspeed != isOverspeed) vs.adasDirty = true;

  if (overspeedOverlay) {
    if (isOverspeed && !bootActive && !kbModal) {
      lv_obj_clear_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN);
      float pulse = 0.5f + 0.5f * sinf(now * 0.014f);
      lv_opa_t borderOpa = (lv_opa_t)(90 + (int)(pulse * 165.0f));
      lv_opa_t tintOpa   = (lv_opa_t)(8 + (int)(pulse * 34.0f));
      lv_obj_set_style_border_opa(overspeedOverlay, borderOpa, 0);
      lv_obj_set_style_bg_opa(overspeedOverlay, tintOpa, 0);
    } else if (!lv_obj_has_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_add_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN);
    }
  }

  bool moving = (vs.speedKmh > 0.5f);
  if (moving) {
    vs.roadPhase = fmodf(vs.roadPhase + (vs.speedKmh * 0.00055f), 1.0f);
    float dKm = (vs.speedKmh / 3600.0f) * dt;
    vs.tripKm += dKm;
    vs.odoKm  += dKm;
  }

  bool eqActive = vs.mediaPlaying;
  if (vs.mediaPlaying) {
    vs.vinylAngle = fmodf(vs.vinylAngle + 3.2f, 360.0f);
    for (int i = 0; i < 12; ++i) {
      float targetH = 20.0f + 72.0f * (0.5f + 0.5f * sinf(now * 0.008f + i * 0.7f) * cosf(now * 0.004f + i * 1.1f));
      vs.eqBars[i] += (targetH - vs.eqBars[i]) * 0.35f;
    }
  } else {
    for (int i = 0; i < 12; ++i) {
      if (fabsf(vs.eqBars[i] - 8.0f) > 0.25f) {
        vs.eqBars[i] += (8.0f - vs.eqBars[i]) * 0.15f;
        eqActive = true;
      } else {
        vs.eqBars[i] = 8.0f;
      }
    }
  }

  if (currentPage == PAGE_DRIVE) {
    int curSpd = displaySpeed();
    int curPwr = (int)lroundf(vs.powerKw);

    if (curSpd != lastShownSpeed || isOverspeed != lastOverspeedVis || (isOverspeed && blinkState != lastBlinkVis)) {
      if (drvSpeedArc) {
        lv_arc_set_value(drvSpeedArc, (int)vs.speedKmh);
        uint32_t arcCol = isOverspeed ? (blinkState ? 0xEF4444 : 0x991B1B) : modeAccentColor();
        lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(arcCol), LV_PART_INDICATOR);
      }
      if (drvSpeedVal) {
        char sBuf[12];
        snprintf(sBuf, sizeof(sBuf), "%d", curSpd);
        setLabelIfChanged(drvSpeedVal, sBuf);
        lv_obj_set_style_text_color(drvSpeedVal, lv_color_hex(isOverspeed ? pal().red : pal().text), 0);
      }
      if (drvSpeedLimitBox) {
        lv_obj_set_style_bg_color(drvSpeedLimitBox, lv_color_hex((isOverspeed && blinkState) ? 0xEF4444 : 0xFFFFFF), 0);
        if (drvSpeedLimitLbl) {
          lv_obj_set_style_text_color(drvSpeedLimitLbl, lv_color_hex((isOverspeed && blinkState) ? 0xFFFFFF : 0x0F172A), 0);
        }
      }
      lastShownSpeed   = curSpd;
      lastOverspeedVis = isOverspeed;
      lastBlinkVis     = blinkState;
    }

    if (curPwr != lastShownPower) {
      if (drvPowerArc) {
        lv_arc_set_value(drvPowerArc, curPwr);
        lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(curPwr < 0 ? pal().green : VISTEON_ORANGE), LV_PART_INDICATOR);
      }
      if (drvPowerVal) {
        char pBuf[16];
        snprintf(pBuf, sizeof(pBuf), "%s%d kW", (curPwr > 0 ? "+" : ""), curPwr);
        setLabelIfChanged(drvPowerVal, pBuf);
        lv_obj_set_style_text_color(drvPowerVal, lv_color_hex(curPwr < 0 ? pal().green : pal().muted), 0);
      }
      lastShownPower = curPwr;
    }

    bool blinkVisual = (vs.leftBlinker || vs.rightBlinker || vs.hazard) && blinkToggled;
    if (drvAdasCanvas && (moving || vs.adasDirty || blinkVisual)) {
      lv_obj_invalidate(drvAdasCanvas);
      vs.adasDirty = false;
    }
  } else if (currentPage == PAGE_MEDIA) {
    if (vs.mediaPlaying && medDiscCanvas) lv_obj_invalidate(medDiscCanvas);
    if (eqActive && medEqCanvas)          lv_obj_invalidate(medEqCanvas);
  }
}

static void updateSlowUi(uint32_t now) {
  const bool wifiOk = (WiFi.status() == WL_CONNECTED);

  // Trigger standalone IP-Geo + Open-Meteo + Timezone sync when Wi-Fi connects & every 15 min
  if (wifiOk) {
    if (!ntpConfigured) {
      configTime(vs.utcOffsetSec, 0, "pool.ntp.org", "time.nist.gov");
      ntpConfigured = true;
      triggerAlert("WI-FI CONNECTED  |  SYNCING WEATHER", pal().green, 3000);
    }
    if (!netSyncRunning && (!weatherEverSynced || (now - lastWeatherSyncMs >= WEATHER_REFRESH_MS))) {
      triggerStandaloneWeatherSync();
    }
  }

  if (ntpConfigured && !bleTimeOverride) {
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
      strftime(vs.timeStr, sizeof(vs.timeStr), "%I:%M %p", &ti);
      strftime(vs.dateStr, sizeof(vs.dateStr), "%a, %d %b", &ti);
    }
  }

  // Persistent Top Bar Icons (Battery Icon instead of Flash Icon)
  if (topWifiIcon) lv_obj_set_style_text_color(topWifiIcon, lv_color_hex(wifiOk ? pal().green : pal().muted), 0);
  if (topBleIcon)  lv_obj_set_style_text_color(topBleIcon, lv_color_hex(bleConnected ? pal().green : pal().cyan), 0);
  if (topSupplyLabel) {
    char supBuf[24];
    snprintf(supBuf, sizeof(supBuf), "%s %d%%", batterySymbolForPct(vs.supplyBattery), vs.supplyBattery);
    setLabelIfChanged(topSupplyLabel, supBuf);
  }

  // Dynamic Pill Priority: Overspeed > Incoming Call (Pulsing Emerald) > Timed Alert > Time | Date | Weather
  if (dynamicPill && dynamicPillLabel) {
    uint32_t pillBg  = pal().surface;
    uint32_t pillTxt = pal().text;
    char pillBuf[96];

    if (isOverspeed) {
      pillBg  = blinkState ? 0xEF4444 : 0x991B1B;
      pillTxt = 0xFFFFFF;
      snprintf(pillBuf, sizeof(pillBuf), LV_SYMBOL_WARNING "  OVERSPEED  |  LIMIT %d %s",
               displayLimit(), vs.useMph ? "MPH" : "KM/H");
    } else if (vs.callActive && !vs.callAnswered) {
      pillBg  = blinkState ? 0x10B981 : 0x047857;
      pillTxt = 0xFFFFFF;
      snprintf(pillBuf, sizeof(pillBuf), LV_SYMBOL_CALL "  INCOMING: %s  (TAP)", vs.callerName);
    } else if (now < vs.alertUntil && vs.alertText[0]) {
      pillBg  = vs.alertColor;
      pillTxt = 0x06090F;
      snprintf(pillBuf, sizeof(pillBuf), "%s", vs.alertText);
    } else {
      snprintf(pillBuf, sizeof(pillBuf), "%s   |   %s   |   %s", vs.timeStr, vs.dateStr, vs.weatherStr);
    }

    if (pillBg != lastPillBgHex) {
      lv_obj_set_style_bg_color(dynamicPill, lv_color_hex(pillBg), 0);
      lastPillBgHex = pillBg;
    }
    if (pillTxt != lastPillTxtHex) {
      lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(pillTxt), 0);
      lastPillTxtHex = pillTxt;
    }
    setLabelIfChanged(dynamicPillLabel, pillBuf);
  }

  // Pause fallback progress drift whenever live phone media packets are actively arriving
  static uint32_t lastProgSec = 0;
  bool freshPhoneMedia = vs.liveMediaSynced && ((now - vs.lastMediaRxMs) < 10000);
  if (vs.mediaPlaying && !freshPhoneMedia && (now - lastProgSec >= 2000)) {
    lastProgSec = now;
    vs.mediaProgress = (vs.mediaProgress + 1) % 101;
  }

  // Active Page Slow Refreshes
  if (currentPage == PAGE_DRIVE) {
    if (drvAdasFooter) {
      char fBuf[48];
      if (vs.gear == 'P')      snprintf(fBuf, sizeof(fBuf), "PARKED  |  SHIFT TO D OR DEMO");
      else if (vs.gear == 'R') snprintf(fBuf, sizeof(fBuf), "REVERSE  |  REAR SENSORS ACTIVE");
      else if (vs.gear == 'N') snprintf(fBuf, sizeof(fBuf), "NEUTRAL  |  COASTING");
      else                     snprintf(fBuf, sizeof(fBuf), "ACC %d  |  LKA ON  |  %dm", displayLimit(), (int)vs.leadDistM);
      setLabelIfChanged(drvAdasFooter, fBuf);
    }
    if (drvNavDistLbl) {
      if (vs.navActive) {
        char dBuf[24];
        formatDistance(dBuf, sizeof(dBuf), vs.navDistM);
        setLabelIfChanged(drvNavDistLbl, dBuf);
      } else {
        setLabelIfChanged(drvNavDistLbl, "Navigation Ready");
      }
    }
    setLabelIfChanged(drvNavStreetLbl, vs.navStreet);
    if (drvNavEtaLbl) {
      if (vs.navActive) {
        char eBuf[20], buf[72];
        formatEta(eBuf, sizeof(eBuf), vs.navEtaMin);
        snprintf(buf, sizeof(buf), "%s  |  %.1f km  |  %s", eBuf, vs.navRemKm, vs.navDest);
        setLabelIfChanged(drvNavEtaLbl, buf);
      } else {
        setLabelIfChanged(drvNavEtaLbl, "Start route in Google Maps");
      }
    }
    setLabelIfChanged(drvMediaTitle, vs.mediaTitle);
    setLabelIfChanged(drvMediaArtist, vs.mediaArtist);
    if (drvMediaProg) lv_bar_set_value(drvMediaProg, vs.mediaProgress, LV_ANIM_OFF);
    if (drvSocLbl) {
      int rangeKm = (int)(vs.socPercent * 4.9f);
      char sBuf[44];
      snprintf(sBuf, sizeof(sBuf), "%s %d%%   |   %d %s",
               batterySymbolForPct((int)vs.socPercent),
               (int)vs.socPercent,
               vs.useMph ? (int)(rangeKm * 0.621f) : rangeKm,
               vs.useMph ? "mi" : "km");
      setLabelIfChanged(drvSocLbl, sBuf);
    }
    if (drvSocBar) lv_bar_set_value(drvSocBar, (int)vs.socPercent, LV_ANIM_OFF);
    if (drvEffLbl) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%.1f kWh/100km  |  Trip %.1f km", vs.efficiencyKwh, vs.tripKm);
      setLabelIfChanged(drvEffLbl, buf);
    }
  } else if (currentPage == PAGE_NAV) {
    if (vs.navDirty) refreshNavHudCard();
    if (vs.callDirty || (vs.callActive && !vs.callAnswered)) {
      refreshNavCallCard();
    }
    if (vs.notifDirty) rebuildNotifList();
  } else if (currentPage == PAGE_MEDIA) {
    setLabelIfChanged(medTitleLbl, vs.mediaTitle);
    setLabelIfChanged(medArtistLbl, vs.mediaArtist);
    if (medProgSlider && !lv_slider_is_dragged(medProgSlider)) {
      lv_slider_set_value(medProgSlider, vs.mediaProgress, LV_ANIM_OFF);
    }
    if (medVolLbl) {
      char vBuf[24];
      snprintf(vBuf, sizeof(vBuf), "%s  %d%%",
               vs.mediaMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX,
               vs.mediaMuted ? 0 : vs.mediaVolume);
      setLabelIfChanged(medVolLbl, vBuf);
    }
    if (medTimeLbl) {
      char tBuf[44];
      if (vs.liveMediaSynced) snprintf(tBuf, sizeof(tBuf), "%d%%  |  Live Phone Sync", vs.mediaProgress);
      else                    snprintf(tBuf, sizeof(tBuf), "0%%  |  Waiting for phone playback");
      setLabelIfChanged(medTimeLbl, tBuf);
    }
  } else if (currentPage == PAGE_VEHICLE) {
    if (vehHvStatsLbl) {
      char buf[96];
      snprintf(buf, sizeof(buf), "Pack: %.1f V   |   Temp: %.0f C   |   SOH: 99%%\nOdometer: %.1f km   |   Trip A: %.1f km",
               vs.hvVoltage, vs.hvTempC, vs.odoKm, vs.tripKm);
      setLabelIfChanged(vehHvStatsLbl, buf);
    }
  } else if (currentPage == PAGE_CONNECT) {
    if (conWifiStatusLbl) {
      setLabelIfChanged(conWifiStatusLbl, wifiOk ? LV_SYMBOL_WIFI "  WI-FI CONNECTED" : LV_SYMBOL_WIFI "  WI-FI DISCONNECTED");
      lv_obj_set_style_text_color(conWifiStatusLbl, lv_color_hex(wifiOk ? pal().green : pal().amber), 0);
    }
    if (conWifiDetailLbl) {
      if (wifiOk) {
        char det[112];
        snprintf(det, sizeof(det), "%s  |  %s  |  %s (%s)",
                 WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), vs.cityStr, vs.weatherStr);
        setLabelIfChanged(conWifiDetailLbl, det);
      } else {
        setLabelIfChanged(conWifiDetailLbl, "Tap CONNECT ANSHIK or SCAN");
      }
    }
    if (conBleStatusLbl) {
      setLabelIfChanged(conBleStatusLbl, bleConnected ? LV_SYMBOL_BLUETOOTH "  PHONE CONNECTED" : LV_SYMBOL_BLUETOOTH "  ADVERTISING: Visteon-Lunet");
      lv_obj_set_style_text_color(conBleStatusLbl, lv_color_hex(bleConnected ? pal().green : pal().cyan), 0);
    }
    if (conBlePhoneLbl) {
      char bBuf[48];
      if (vs.phoneBattery >= 0) snprintf(bBuf, sizeof(bBuf), "MTU 247  |  Phone: %s %d%%", batterySymbolForPct(vs.phoneBattery), vs.phoneBattery);
      else                      snprintf(bBuf, sizeof(bBuf), "MTU 247  |  Phone Batt: Waiting for App");
      setLabelIfChanged(conBlePhoneLbl, bBuf);
    }
    if (conBleLogLbl && bleLogDirty) {
      char logBuf[720];
      size_t pos = 0;
      logBuf[0] = '\0';
      for (int i = 0; i < bleLogCount && pos < sizeof(logBuf) - 2; ++i) {
        pos += snprintf(logBuf + pos, sizeof(logBuf) - pos, "%s\n", bleLogLines[i]);
      }
      setLabelIfChanged(conBleLogLbl, logBuf);
      bleLogDirty = false;
    }
  } else if (currentPage == PAGE_SYSTEM) {
    if (sysMemLbl) {
      char buf[164];
      snprintf(buf, sizeof(buf),
               "CPU: 240 MHz Dual-Core LX7  |  Loc: %s (%.2f, %.2f)\nInternal Heap: %lu KB  |  OPI PSRAM: %lu KB\nUptime: %lu s  |  GT911 Touch: %s",
               vs.cityStr, vs.latitude, vs.longitude,
               (unsigned long)(ESP.getFreeHeap() / 1024),
               (unsigned long)(ESP.getFreePsram() / 1024),
               (unsigned long)(now / 1000),
               touchReady ? "ACTIVE" : "OFFLINE");
      setLabelIfChanged(sysMemLbl, buf);
    }
    if (sysTouchLbl && sysTouchDot) {
      char tBuf[40];
      snprintf(tBuf, sizeof(tBuf), "Last Touch: X=%d, Y=%d", vs.lastTouchX, vs.lastTouchY);
      setLabelIfChanged(sysTouchLbl, tBuf);
      int dx = clampInt(10 + (vs.lastTouchX * 264) / 800, 10, 274);
      int dy = clampInt(28 + (vs.lastTouchY * 78) / 480, 28, 106);
      lv_obj_set_pos(sysTouchDot, dx, dy);
    }
  }
}

// -----------------------------------------------------------------------------
// Arduino Entry Points
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[VISTEON LUNET OS 3.0] Booting...");

  board = new Board();
  board->init();
  board->begin();

  lcd   = board->getLCD();
  touch = board->getTouch();

  if (!initLvgl()) {
    Serial.println("[LUNET] LVGL initialization failed!");
    while (true) delay(1000);
  }

  // Non-blocking auto-connect to saved Wi-Fi so standalone time/location/weather sync on boot
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(SAVED_SSID, SAVED_PASSWORD);

  startBle();

  initOverspeedOverlay();
  buildShell();
  showBootSplash();
  Serial.println("[VISTEON LUNET OS 3.0] Online (Park Mode).");
}

void loop() {
  const uint32_t now = millis();
  lv_timer_handler();

  char rxBuf[BLE_MSG_MAX];
  while (dequeueBleRx(rxBuf, sizeof(rxBuf))) {
    processBleCommand(rxBuf);
  }

  applyStandaloneNetSyncResult();

  if (bleSyncReqPending && bleConnected && (now - bleConnectedAt >= 650)) {
    bleSyncReqPending = false;
    sendBlePacket("CMD:SYNC:REQ");
  }

  if (bleRestartPending && !bleConnected && (now - bleDisconnectAt >= 800)) {
    BLEDevice::startAdvertising();
    bleRestartPending = false;
    appendBleLog("SYS", "Auto-restarted advertising");
  }

  updateWifiScanResults();

  if (now - lastPhysicsMs >= 33) {
    lastPhysicsMs = now;
    stepPhysicsAndAnimations(now);
  }

  if (now - lastSlowUiMs >= 250) {
    lastSlowUiMs = now;
    updateSlowUi(now);
  }

#if LV_USE_CHART
  if (currentPage == PAGE_SYSTEM && sysChart && (now - lastChartMs >= 500)) {
    lastChartMs = now;
    lv_chart_set_next_value(sysChart, serSpeed, (lv_coord_t)vs.speedKmh);
    lv_chart_set_next_value(sysChart, serPower, (lv_coord_t)(vs.powerKw + 60));
  }
#endif

  delay(5);
}