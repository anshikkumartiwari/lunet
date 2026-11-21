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
#include <time.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <cmath>
#include <cstring>
#include <cstdio>

using namespace esp_panel::board;
using namespace esp_panel::drivers;

// -----------------------------------------------------------------------------
// Safe Font Fallbacks (Compiles cleanly on any lv_conf.h)
// -----------------------------------------------------------------------------
#if LV_FONT_MONTSERRAT_12
  #define FONT_XS (&lv_font_montserrat_12)
#else
  #define FONT_XS (&lv_font_montserrat_14)
#endif

#define FONT_SM (&lv_font_montserrat_14)

#if LV_FONT_MONTSERRAT_16
  #define FONT_MD (&lv_font_montserrat_16)
#else
  #define FONT_MD (&lv_font_montserrat_14)
#endif

#if LV_FONT_MONTSERRAT_20
  #define FONT_LG (&lv_font_montserrat_20)
#elif LV_FONT_MONTSERRAT_16
  #define FONT_LG (&lv_font_montserrat_16)
#else
  #define FONT_LG (&lv_font_montserrat_14)
#endif

#if LV_FONT_MONTSERRAT_28
  #define FONT_XL (&lv_font_montserrat_28)
#elif LV_FONT_MONTSERRAT_24
  #define FONT_XL (&lv_font_montserrat_24)
#elif LV_FONT_MONTSERRAT_20
  #define FONT_XL (&lv_font_montserrat_20)
#else
  #define FONT_XL (&lv_font_montserrat_16)
#endif

#if LV_FONT_MONTSERRAT_48
  #define FONT_HERO (&lv_font_montserrat_48)
#elif LV_FONT_MONTSERRAT_40
  #define FONT_HERO (&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_36
  #define FONT_HERO (&lv_font_montserrat_36)
#elif LV_FONT_MONTSERRAT_32
  #define FONT_HERO (&lv_font_montserrat_32)
#elif LV_FONT_MONTSERRAT_28
  #define FONT_HERO (&lv_font_montserrat_28)
#else
  #define FONT_HERO FONT_XL
#endif

// -----------------------------------------------------------------------------
// Shared BLE 5.0 Protocol Constants (Matches Kotlin Companion App)
// -----------------------------------------------------------------------------
static const char *BLE_DEVICE_NAME  = "Visteon-Lunet";
static const char *BLE_SERVICE_UUID = "7b5c0001-7b7c-4f2a-9a6d-2c0c0e4a1000";
static const char *BLE_CHAR_UUID    = "7b5c0002-7b7c-4f2a-9a6d-2c0c0e4a1000";

static const char *SAVED_SSID       = "Anshik";
static const char *SAVED_PASSWORD   = "anshik.ti";

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
static inline const ThemePalette &T() { return lightTheme ? THEME_LIGHT : THEME_DARK; }

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

// BLE State & Thread-Safe 12-Slot Ring Queue (Zero Dropped Burst Packets)
static bool bleReady = false;
static volatile bool bleConnected = false;
static volatile bool bleRestartPending = false;
static volatile bool bleSyncReqPending = false;
static uint32_t bleDisconnectAt = 0;
static uint32_t bleConnectedAt  = 0;
static BLECharacteristic *bleCharacteristic = nullptr;

static constexpr uint8_t BLE_QUEUE_SIZE = 12;
static constexpr size_t  BLE_MSG_MAX    = 160;
static portMUX_TYPE bleQueueMux = portMUX_INITIALIZER_UNLOCKED;
static char bleQueue[BLE_QUEUE_SIZE][BLE_MSG_MAX];
static volatile uint8_t bleQHead  = 0;
static volatile uint8_t bleQTail  = 0;
static volatile uint8_t bleQCount = 0;

// Wi-Fi & NTP State
static bool wifiScanInProgress = false;
static bool ntpConfigured = false;
static bool bleTimeOverride = false;
static char activeWifiName[33] = "";

struct WifiEntry {
  char ssid[33];
  int  rssi;
  bool open;
};
static WifiEntry scannedWifi[16];
static uint8_t scannedWifiCount = 0;
static char pendingWifiSsid[33] = "";

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

struct VehicleState {
  // Powertrain & Dynamics (Safe Boot Defaults)
  float speedKmh       = 0.0f;
  float targetSpeedKmh = 0.0f;
  float powerKw        = 0.0f;  // -60 (Regen) to +180 (Power)
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
  int   regenLevel     = 1;     // 0=Low, 1=Std, 2=One-Pedal

  // ADAS & Signals
  bool  leftBlinker    = false;
  bool  rightBlinker   = false;
  bool  hazard         = false;
  bool  braking        = false;
  bool  lkaActive      = true;
  bool  accActive      = true;
  float leadDistM      = 42.0f;
  float roadPhase      = 0.0f;

  // Body & TPMS (PSI)
  float tpms[4]        = {34.2f, 34.4f, 33.8f, 34.1f}; // FL, FR, RL, RR
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
  int   seatHeat       = 0;     // 0..3

  // Phone Sync & Dynamic Alert
  char  timeStr[16]    = "12:00 PM";
  char  dateStr[24]    = "Sat, 21 Nov";
  char  weatherStr[28] = "27C Clear";
  int   supplyBattery  = 100;   // Direct power supply = 100%
  int   phoneBattery   = -1;    // Synced via PBATT:<0-100> (-1 = not synced)
  char  alertText[72]  = "";
  uint32_t alertColor  = VISTEON_ORANGE;
  uint32_t alertUntil  = 0;

  // Navigation
  NavArrow navDir      = NAV_RIGHT;
  int   navDistM       = 250;
  char  navStreet[56]  = "Outer Ring Expressway";
  char  navDest[40]    = "Visteon Tech Park";
  int   navEtaMin      = 16;
  float navRemKm       = 11.4f;
  int   navRoutePct    = 38;

  // Phone Call & Notifications
  bool  callActive     = false;
  bool  callAnswered   = false;
  char  callerName[44] = "No Active Call";
  char  notifText[96]  = "Waiting for phone notifications...";

  // Media
  char  mediaTitle[56] = "Not Playing";
  char  mediaArtist[44]= "Connect Lunet App for Live Audio";
  bool  mediaPlaying   = false;
  int   mediaProgress  = 0;     // 0..100
  int   mediaVolume    = 70;    // 0..100
  bool  mediaMuted     = false;
  int   eqPreset       = 0;     // 0=Studio, 1=Bass+, 2=Vocal, 3=Spatial
  float eqBars[12]     = {8,12,10,14,12,10,8,12,10,12,8,10};
  float vinylAngle     = 0.0f;

  // Touch Tracker
  int   lastTouchX     = 400;
  int   lastTouchY     = 240;
} vs;

static char bleLogLines[8][82];
static uint8_t bleLogCount = 0;

// -----------------------------------------------------------------------------
// Persistent Shell & Page Widget Pointers
// -----------------------------------------------------------------------------
static lv_obj_t *bootOverlay      = nullptr;
static lv_obj_t *bootBar          = nullptr;
static bool      bootActive       = false;
static uint32_t  bootStartMs      = 0;

static lv_obj_t *overspeedOverlay = nullptr; // Full-screen pulsing red edge warning

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
static lv_obj_t *navBigArrowBox   = nullptr;
static lv_obj_t *navBigDistLbl    = nullptr;
static lv_obj_t *navBigActionLbl  = nullptr;
static lv_obj_t *navBigStreetLbl  = nullptr;
static lv_obj_t *navProgBar       = nullptr;
static lv_obj_t *navEtaDetailLbl  = nullptr;
static lv_obj_t *navCallStatusLbl = nullptr;
static lv_obj_t *navCallerLbl     = nullptr;
static lv_obj_t *navPhoneBattLbl  = nullptr;
static lv_obj_t *navNotifLbl      = nullptr;

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
static lv_obj_t *sysChart         = nullptr;
static lv_chart_series_t *serSpeed = nullptr;
static lv_chart_series_t *serPower = nullptr;
static lv_obj_t *sysMemLbl        = nullptr;
static lv_obj_t *sysTouchLbl      = nullptr;
static lv_obj_t *sysTouchDot      = nullptr;
static lv_obj_t *sysUnitBtnLbl    = nullptr;

// Timing Trackers
static uint32_t lastPhysicsMs     = 0;
static uint32_t lastSlowUiMs      = 0;
static uint32_t lastChartMs       = 0;
static uint32_t lastVolTxMs       = 0;
static bool     blinkState        = false;
static uint32_t lastBlinkMs       = 0;
static bool     isOverspeed       = false;

// -----------------------------------------------------------------------------
// Forward Declarations
// -----------------------------------------------------------------------------
static void buildShell();
static void selectPage(PageId page);
static void showBootSplash();
static void sendBlePacket(const char *packet);
static void appendBleLog(const char *prefix, const char *msg);
static void triggerAlert(const char *text, uint32_t colorHex, uint32_t durationMs = 4000);
static void startWifiConnection(const char *ssid, const char *password);
static void scanWifiNetworks();
static void updateWifiScanResults();
static void restartBleAdvertising();

static inline uint32_t modeAccentColor() {
  if (vs.mode == MODE_ECO) return T().green;
  if (vs.mode == MODE_SPORT) return VISTEON_ORANGE;
  return T().cyan;
}

static inline int displaySpeed() {
  return (int)lroundf(vs.useMph ? (vs.speedKmh * 0.621371f) : vs.speedKmh);
}

static inline int displayLimit() {
  return (int)lroundf(vs.useMph ? (vs.speedLimitKmh * 0.621371f) : (float)vs.speedLimitKmh);
}

// -----------------------------------------------------------------------------
// Custom Vector Renderers (Anti-Aliased LVGL 8.4 — Zero Extra Flash/RAM)
// -----------------------------------------------------------------------------

// Exact geometric vector reconstruction of Visteon v.png (#F37B20)
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

// Crisp Icon-Only Theme Toggle (Sun in Dark Mode, Crescent Moon in Light Mode)
static void drawThemeIconCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int cy = (a.y1 + a.y2) / 2;

  if (!lightTheme) {
    // Dark Mode active -> Show Sun icon to switch to Light Mode
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
    // Light Mode active -> Show Crescent Moon icon to switch to Dark Mode
    lv_draw_rect_dsc_t moon;
    lv_draw_rect_dsc_init(&moon);
    moon.bg_color = lv_color_hex(0x0F172A);
    moon.bg_opa   = LV_OPA_COVER;
    moon.radius   = LV_RADIUS_CIRCLE;
    lv_area_t outer = {(lv_coord_t)(cx - 8), (lv_coord_t)(cy - 8),
                       (lv_coord_t)(cx + 8), (lv_coord_t)(cy + 8)};
    lv_draw_rect(ctx, &moon, &outer);

    // Cutout circle matching button surface background
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

// Vector Turn-by-Turn Navigation Arrow (Straight / Right / Left / U-Turn)
static void drawNavArrowCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int cx = (a.x1 + a.x2) / 2;
  const int cy = (a.y1 + a.y2) / 2;
  const int w  = lv_area_get_width(&a);
  const int h  = lv_area_get_height(&a);
  const int sw = (w > 70) ? 10 : 5;
  const int hs = (w > 70) ? 18 : 9;

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(T().green);
  ld.width = sw;
  ld.round_start = 1;
  ld.round_end   = 1;

  lv_draw_rect_dsc_t headDsc;
  lv_draw_rect_dsc_init(&headDsc);
  headDsc.bg_color = lv_color_hex(T().green);
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

// Live 3D-Perspective ADAS Road, Lane Keep Assist, Radar & Ego EV Canvas
static void drawAdasCanvasCb(lv_event_t *e) {
  lv_obj_t *obj = lv_event_get_target(e);
  lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);

  const int w  = lv_area_get_width(&a);
  const int cx = a.x1 + w / 2;
  const int vy = a.y1 + 24;
  const int rH = (a.y2 - vy);

  // 1. Perspective Road Surface
  lv_draw_rect_dsc_t roadDsc;
  lv_draw_rect_dsc_init(&roadDsc);
  roadDsc.bg_color = lv_color_hex(T().road);
  roadDsc.bg_opa   = LV_OPA_COVER;

  lv_point_t roadPoly[4] = {
    {(lv_coord_t)(cx - 28), (lv_coord_t)vy},
    {(lv_coord_t)(cx + 28), (lv_coord_t)vy},
    {(lv_coord_t)(a.x2 - 8), (lv_coord_t)a.y2},
    {(lv_coord_t)(a.x1 + 8), (lv_coord_t)a.y2}
  };
  lv_draw_polygon(ctx, &roadDsc, roadPoly, 4);

  // 2. Horizon Line
  lv_draw_line_dsc_t horzDsc;
  lv_draw_line_dsc_init(&horzDsc);
  horzDsc.color = lv_color_hex(T().border);
  horzDsc.width = 1;
  lv_point_t h1 = {(lv_coord_t)(a.x1 + 4), (lv_coord_t)vy};
  lv_point_t h2 = {(lv_coord_t)(a.x2 - 4), (lv_coord_t)vy};
  lv_draw_line(ctx, &horzDsc, &h1, &h2);

  // 3. Perspective Center Dashes
  lv_draw_line_dsc_t dashDsc;
  lv_draw_line_dsc_init(&dashDsc);
  dashDsc.color = lv_color_hex(T().muted);
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

  // 4. Active LKA Boundary Rails (Turns crimson during overspeed)
  lv_draw_line_dsc_t railDsc;
  lv_draw_line_dsc_init(&railDsc);
  uint32_t railCol = isOverspeed ? T().red : (vs.lkaActive && vs.gear == 'D' ? modeAccentColor() : T().muted);
  railDsc.color = lv_color_hex(railCol);
  railDsc.width = 3;
  railDsc.opa   = LV_OPA_90;

  lv_point_t lTop = {(lv_coord_t)(cx - 28), (lv_coord_t)vy};
  lv_point_t lBot = {(lv_coord_t)(a.x1 + 10), (lv_coord_t)a.y2};
  lv_point_t rTop = {(lv_coord_t)(cx + 28), (lv_coord_t)vy};
  lv_point_t rBot = {(lv_coord_t)(a.x2 - 10), (lv_coord_t)a.y2};
  lv_draw_line(ctx, &railDsc, &lTop, &lBot);
  lv_draw_line(ctx, &railDsc, &rTop, &rBot);

  // 5. Radar-Tracked Lead Vehicle (Visible when moving or in D)
  if (vs.gear == 'D') {
    float normDist = (vs.leadDistM - 15.0f) / 60.0f;
    if (normDist < 0.0f) normDist = 0.0f;
    if (normDist > 1.0f) normDist = 1.0f;
    float leadT = 0.56f - normDist * 0.34f;
    int leadY = vy + (int)(leadT * rH);
    int leadW = 28 + (int)(leadT * 42.0f);
    int leadH = 16 + (int)(leadT * 22.0f);

    if (vs.accActive && vs.speedKmh > 5.0f) {
      lv_draw_arc_dsc_t radDsc;
      lv_draw_arc_dsc_init(&radDsc);
      radDsc.color = lv_color_hex(vs.leadDistM < 22.0f ? T().amber : T().cyan);
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
    tailDsc.bg_color = lv_color_hex(T().red);
    tailDsc.bg_opa   = LV_OPA_COVER;
    tailDsc.radius   = 2;
    lv_area_t ltL = {(lv_coord_t)(lb.x1 + 3), (lv_coord_t)(lb.y2 - 7), (lv_coord_t)(lb.x1 + 11), (lv_coord_t)(lb.y2 - 3)};
    lv_area_t ltR = {(lv_coord_t)(lb.x2 - 11), (lv_coord_t)(lb.y2 - 7), (lv_coord_t)(lb.x2 - 3), (lv_coord_t)(lb.y2 - 3)};
    lv_draw_rect(ctx, &tailDsc, &ltL);
    lv_draw_rect(ctx, &tailDsc, &ltR);
  }

  // 6. Ego Vehicle Silhouette
  int egoW = 74, egoH = 92;
  int egoTop = a.y2 - egoH - 6;

  lv_draw_rect_dsc_t glowDsc;
  lv_draw_rect_dsc_init(&glowDsc);
  glowDsc.bg_color = lv_color_hex(isOverspeed ? T().red : modeAccentColor());
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
  egoDsc.border_color = lv_color_hex(T().muted);
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

  // Full-Width EV Taillight Bar (bright when braking or in Park)
  bool brakeLit = vs.braking || (vs.gear == 'P');
  lv_draw_rect_dsc_t egoTail;
  lv_draw_rect_dsc_init(&egoTail);
  egoTail.bg_color = lv_color_hex(brakeLit ? 0xFF2222 : 0xB91C1C);
  egoTail.bg_opa   = brakeLit ? LV_OPA_COVER : LV_OPA_80;
  egoTail.radius   = 3;
  lv_area_t etBar = {(lv_coord_t)(cx - 28), (lv_coord_t)(egoTop + egoH - 9),
                     (lv_coord_t)(cx + 28), (lv_coord_t)(egoTop + egoH - 4)};
  lv_draw_rect(ctx, &egoTail, &etBar);

  // Turn Signal / Hazard Chevrons
  bool showLeft  = (vs.leftBlinker  || vs.hazard) && blinkState;
  bool showRight = (vs.rightBlinker || vs.hazard) && blinkState;

  lv_draw_rect_dsc_t sigDsc;
  lv_draw_rect_dsc_init(&sigDsc);
  sigDsc.bg_color = lv_color_hex(T().amber);
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

// Media Vinyl / Album Art Disc
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
  disc.border_color = lv_color_hex(T().border);
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
  lv_draw_arc(ctx, &arcDsc, &center, r - 12, startA, (startA + 75) % 360);

  arcDsc.color = lv_color_hex(T().cyan);
  arcDsc.width = 2;
  lv_draw_arc(ctx, &arcDsc, &center, r - 24, (startA + 180) % 360, (startA + 250) % 360);

  lv_draw_rect_dsc_t hub;
  lv_draw_rect_dsc_init(&hub);
  hub.bg_color = lv_color_hex(VISTEON_ORANGE);
  hub.bg_opa   = LV_OPA_COVER;
  hub.radius   = LV_RADIUS_CIRCLE;
  lv_area_t hArea = {(lv_coord_t)(cx - 24), (lv_coord_t)(cy - 24),
                     (lv_coord_t)(cx + 24), (lv_coord_t)(cy + 24)};
  lv_draw_rect(ctx, &hub, &hArea);

  hub.bg_color = lv_color_hex(T().bg);
  lv_area_t spindle = {(lv_coord_t)(cx - 7), (lv_coord_t)(cy - 7),
                       (lv_coord_t)(cx + 7), (lv_coord_t)(cy + 7)};
  lv_draw_rect(ctx, &hub, &spindle);
}

// Live 12-Bar Audio Spectrum Visualizer
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
    bd.bg_color = lv_color_hex((i % 3 == 0) ? VISTEON_ORANGE : T().cyan);
    int x = a.x1 + i * (barW + 6);
    lv_area_t r = {(lv_coord_t)x, (lv_coord_t)(a.y2 - bh),
                   (lv_coord_t)(x + barW), (lv_coord_t)a.y2};
    lv_draw_rect(ctx, &bd, &r);
  }
}

// Top-Down EV Chassis & TPMS Blueprint
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
    uint32_t wc = (vs.tpms[i] < 29.0f) ? T().red : ((vs.tpms[i] < 32.0f) ? T().amber : T().green);
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
  body.border_color = lv_color_hex(vs.doorsLocked ? T().cyan : T().amber);
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
  lv_obj_set_style_bg_color(card, lv_color_hex(bgHex ? bgHex : T().surface), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(T().border), 0);
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
  lv_obj_set_style_bg_color(btn, lv_color_hex(bgHex ? bgHex : T().surfaceAlt), LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(VISTEON_ORANGE), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(T().border), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_radius(btn, radius, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);

  if (text && text[0]) {
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, FONT_SM, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(txtHex ? txtHex : T().text), 0);
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

  navBigArrowBox = navBigDistLbl = navBigActionLbl = navBigStreetLbl = nullptr;
  navProgBar = navEtaDetailLbl = navCallStatusLbl = navCallerLbl = navPhoneBattLbl = navNotifLbl = nullptr;

  medDiscCanvas = medEqCanvas = medTitleLbl = medArtistLbl = medProgSlider = nullptr;
  medTimeLbl = medPlayBtnLbl = medVolSlider = medVolLbl = nullptr;
  for (int i = 0; i < 4; ++i) medEqBtns[i] = nullptr;

  vehChassisCanvas = vehLightBtn = vehLockBtn = vehTrunkBtn = nullptr;
  vehDrvTempLbl = vehPasTempLbl = vehFanLbl = vehAcBtn = vehAutoBtn = nullptr;
  vehDefrostBtn = vehSeatBtn = vehHvStatsLbl = vehRegenBtn = nullptr;
  for (int i = 0; i < 4; ++i) vehTpmsLbl[i] = nullptr;

  conWifiStatusLbl = conWifiDetailLbl = conWifiList = conBleStatusLbl = conBlePhoneLbl = conBleLogLbl = nullptr;

  sysChart = sysMemLbl = sysTouchLbl = sysTouchDot = sysUnitBtnLbl = nullptr;
  serSpeed = serPower = nullptr;
}

static void triggerAlert(const char *text, uint32_t colorHex, uint32_t durationMs) {
  strncpy(vs.alertText, text, sizeof(vs.alertText) - 1);
  vs.alertText[sizeof(vs.alertText) - 1] = '\0';
  vs.alertColor = colorHex;
  vs.alertUntil = millis() + durationMs;
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
  uint32_t acc = modeAccentColor();
  if (drvSpeedArc && !isOverspeed) {
    lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(acc), LV_PART_INDICATOR);
  }
  if (drvModeBadge) {
    const char *mNames[3] = {"ECO MODE", "COMFORT", "SPORT+"};
    lv_label_set_text(drvModeBadge, mNames[vs.mode]);
    lv_obj_set_style_text_color(drvModeBadge, lv_color_hex(acc), 0);
  }
  const char gears[4] = {'P', 'R', 'N', 'D'};
  for (int i = 0; i < 4; ++i) {
    if (!drvGearBtns[i]) continue;
    bool active = (vs.gear == gears[i]);
    uint32_t gc = (active && vs.gear == 'P') ? VISTEON_ORANGE : (active ? acc : T().surfaceAlt);
    lv_obj_set_style_bg_color(drvGearBtns[i], lv_color_hex(gc), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvGearBtns[i], 0),
                                lv_color_hex(active ? 0x06090F : T().text), 0);
  }
  for (int i = 0; i < 3; ++i) {
    if (!drvModeBtns[i]) continue;
    bool active = (vs.mode == i);
    uint32_t mc = (i == 0) ? T().green : ((i == 1) ? T().cyan : VISTEON_ORANGE);
    lv_obj_set_style_bg_color(drvModeBtns[i], lv_color_hex(active ? mc : T().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvModeBtns[i], 0),
                                lv_color_hex(active ? 0x06090F : T().text), 0);
  }
  if (drvDemoBtn) {
    lv_obj_set_style_bg_color(drvDemoBtn, lv_color_hex(vs.demoDrive ? T().green : T().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(drvDemoBtn, 0),
                                lv_color_hex(vs.demoDrive ? 0x06090F : T().text), 0);
  }
  if (drvHazardBtn) {
    lv_obj_set_style_bg_color(drvHazardBtn, lv_color_hex(vs.hazard ? T().red : T().surfaceAlt), 0);
  }
  if (drvLeftBtn) {
    lv_obj_set_style_bg_color(drvLeftBtn, lv_color_hex(vs.leftBlinker ? T().amber : T().surfaceAlt), 0);
  }
  if (drvRightBtn) {
    lv_obj_set_style_bg_color(drvRightBtn, lv_color_hex(vs.rightBlinker ? T().amber : T().surfaceAlt), 0);
  }
}

static void buildPageDrive() {
  // === LEFT CARD: Dual-Arc Speedometer + Power/Regen + PRND ===
  lv_obj_t *leftCard = makeCard(pageContainer, 12, 6, 254, 362);

  drvModeBadge = makeLabel(leftCard, "COMFORT", FONT_SM, modeAccentColor(), 16, 12);

  // Speed Limit Sign Badge
  drvSpeedLimitBox = lv_obj_create(leftCard);
  lv_obj_set_pos(drvSpeedLimitBox, 206, 8);
  lv_obj_set_size(drvSpeedLimitBox, 36, 36);
  lv_obj_clear_flag(drvSpeedLimitBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(drvSpeedLimitBox, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(drvSpeedLimitBox, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(drvSpeedLimitBox, lv_color_hex(T().red), 0);
  lv_obj_set_style_border_width(drvSpeedLimitBox, 3, 0);
  drvSpeedLimitLbl = lv_label_create(drvSpeedLimitBox);
  lv_label_set_text_fmt(drvSpeedLimitLbl, "%d", displayLimit());
  lv_obj_set_style_text_font(drvSpeedLimitLbl, FONT_XS, 0);
  lv_obj_set_style_text_color(drvSpeedLimitLbl, lv_color_hex(0x0F172A), 0);
  lv_obj_center(drvSpeedLimitLbl);

  // Outer Speed Arc
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
  lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_arc_width(drvSpeedArc, 13, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(modeAccentColor()), LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(drvSpeedArc, true, LV_PART_INDICATOR);

  // Inner Symmetrical Power / Regen Arc
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
  lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(T().surfaceDeep), LV_PART_MAIN);
  lv_obj_set_style_arc_width(drvPowerArc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(T().green), LV_PART_INDICATOR);

  // Hero Digital Speed Readout
  drvSpeedVal = lv_label_create(leftCard);
  lv_label_set_text_fmt(drvSpeedVal, "%d", displaySpeed());
  lv_obj_set_style_text_font(drvSpeedVal, FONT_HERO, 0);
  lv_obj_set_style_text_color(drvSpeedVal, lv_color_hex(T().text), 0);
  lv_obj_set_width(drvSpeedVal, 150);
  lv_obj_set_style_text_align(drvSpeedVal, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvSpeedVal, 52, 108);

  drvSpeedUnit = lv_label_create(leftCard);
  lv_label_set_text(drvSpeedUnit, vs.useMph ? "MPH" : "KM/H");
  lv_obj_set_style_text_font(drvSpeedUnit, FONT_SM, 0);
  lv_obj_set_style_text_color(drvSpeedUnit, lv_color_hex(T().muted), 0);
  lv_obj_set_width(drvSpeedUnit, 100);
  lv_obj_set_style_text_align(drvSpeedUnit, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvSpeedUnit, 77, 162);

  drvPowerVal = lv_label_create(leftCard);
  lv_label_set_text(drvPowerVal, "0 kW");
  lv_obj_set_style_text_font(drvPowerVal, FONT_XS, 0);
  lv_obj_set_style_text_color(drvPowerVal, lv_color_hex(T().muted), 0);
  lv_obj_set_width(drvPowerVal, 120);
  lv_obj_set_style_text_align(drvPowerVal, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(drvPowerVal, 67, 184);

  // Interactive Manual Throttle Slider
  makeLabel(leftCard, "THROTTLE", FONT_XS, T().muted, 16, 254);
  drvThrottleSld = lv_slider_create(leftCard);
  lv_obj_set_pos(drvThrottleSld, 16, 274);
  lv_obj_set_size(drvThrottleSld, 222, 10);
  lv_slider_set_range(drvThrottleSld, 0, 200);
  lv_slider_set_value(drvThrottleSld, (int)vs.targetSpeedKmh, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvThrottleSld, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
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

  // Interactive PRND Shifter
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

  // === CENTER CARD: Live ADAS 3D Road & Signals ===
  lv_obj_t *centerCard = makeCard(pageContainer, 274, 6, 252, 362, T().surfaceDeep);

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
      triggerAlert("AUTOPILOT DEMO ENGAGED", T().green, 2500);
    } else {
      vs.targetSpeedKmh = 0.0f;
      if (drvThrottleSld) lv_slider_set_value(drvThrottleSld, 0, LV_ANIM_OFF);
      triggerAlert("DEMO MODE PAUSED", T().muted, 2000);
    }
    refreshDriveStyles();
  });
  drvHazardBtn = makeBtn(centerCard, LV_SYMBOL_WARNING, 144, 10, 44, 34, [](lv_event_t *e) {
    vs.hazard = !vs.hazard;
    if (vs.hazard) triggerAlert("HAZARD LIGHTS ACTIVE", T().red, 3500);
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

  lv_obj_t *adasBar = makeCard(centerCard, 10, 322, 232, 30, T().surface, 8);
  drvAdasFooter = makeLabel(adasBar, "PARKED  |  READY TO DRIVE", FONT_XS, T().cyan, 14, 7);

  // === RIGHT COLUMN: Contextual Smart Stack ===
  // 1. Mini Navigation Card
  lv_obj_t *navCard = makeCard(pageContainer, 534, 6, 254, 106);
  lv_obj_add_flag(navCard, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(navCard, [](lv_event_t *e) { selectPage(PAGE_NAV); }, LV_EVENT_CLICKED, nullptr);

  drvNavArrowBox = makeCard(navCard, 12, 16, 48, 48, T().surfaceAlt, 12);
  lv_obj_add_event_cb(drvNavArrowBox, drawNavArrowCb, LV_EVENT_DRAW_MAIN, nullptr);

  drvNavDistLbl   = makeLabel(navCard, "250 m", FONT_LG, T().text, 70, 14);
  drvNavStreetLbl = makeLabel(navCard, vs.navStreet, FONT_SM, T().muted, 70, 40, 174);
  drvNavEtaLbl    = makeLabel(navCard, "16 min  |  11.4 km", FONT_XS, T().green, 12, 78, 232);

  // 2. Mini Live Phone Media Card
  lv_obj_t *medCard = makeCard(pageContainer, 534, 120, 254, 124);
  makeLabel(medCard, LV_SYMBOL_AUDIO "  NOW PLAYING", FONT_XS, VISTEON_ORANGE, 14, 10);
  drvMediaTitle  = makeLabel(medCard, vs.mediaTitle, FONT_MD, T().text, 14, 28, 226);
  drvMediaArtist = makeLabel(medCard, vs.mediaArtist, FONT_XS, T().muted, 14, 50, 226);

  drvMediaProg = lv_bar_create(medCard);
  lv_obj_set_pos(drvMediaProg, 14, 70);
  lv_obj_set_size(drvMediaProg, 226, 4);
  lv_bar_set_value(drvMediaProg, vs.mediaProgress, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvMediaProg, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(drvMediaProg, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);

  makeBtn(medCard, LV_SYMBOL_PREV, 14, 82, 68, 32, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:PREV");
  });
  lv_obj_t *pBtn = makeBtn(medCard, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY,
                           92, 82, 70, 32, [](lv_event_t *e) {
    vs.mediaPlaying = !vs.mediaPlaying;
    sendBlePacket(vs.mediaPlaying ? "CMD:MEDIA:PLAY" : "CMD:MEDIA:PAUSE");
    if (drvMediaPlayLbl) lv_label_set_text(drvMediaPlayLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  }, nullptr, VISTEON_ORANGE, 0x06090F);
  drvMediaPlayLbl = lv_obj_get_child(pBtn, 0);
  makeBtn(medCard, LV_SYMBOL_NEXT, 172, 82, 68, 32, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:NEXT");
  });

  // 3. EV Energy & Drive Mode Card
  lv_obj_t *evCard = makeCard(pageContainer, 534, 252, 254, 116);
  drvSocLbl = makeLabel(evCard, LV_SYMBOL_CHARGE " 84%   |   412 km", FONT_SM, T().text, 14, 10);

  drvSocBar = lv_bar_create(evCard);
  lv_obj_set_pos(drvSocBar, 14, 34);
  lv_obj_set_size(drvSocBar, 226, 8);
  lv_bar_set_value(drvSocBar, (int)vs.socPercent, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(drvSocBar, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(drvSocBar, lv_color_hex(T().green), LV_PART_INDICATOR);

  drvEffLbl = makeLabel(evCard, "14.2 kWh/100km  |  Trip 48.6 km", FONT_XS, T().muted, 14, 48);

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
// Page 2: NAV / HUD & Phone Companion
// -----------------------------------------------------------------------------
static void applyRoutePreset(int idx) {
  if (idx == 0) {
    vs.navDir = NAV_RIGHT; vs.navDistM = 250; vs.navEtaMin = 16; vs.navRemKm = 11.4f;
    strncpy(vs.navStreet, "Outer Ring Expressway", sizeof(vs.navStreet) - 1);
    strncpy(vs.navDest, "Visteon Tech Park", sizeof(vs.navDest) - 1);
  } else if (idx == 1) {
    vs.navDir = NAV_STRAIGHT; vs.navDistM = 850; vs.navEtaMin = 38; vs.navRemKm = 31.2f;
    strncpy(vs.navStreet, "NH-44 Airport Corridor", sizeof(vs.navStreet) - 1);
    strncpy(vs.navDest, "BLR Airport T2", sizeof(vs.navDest) - 1);
  } else {
    vs.navDir = NAV_LEFT; vs.navDistM = 140; vs.navEtaMin = 22; vs.navRemKm = 9.8f;
    strncpy(vs.navStreet, "MG Road Boulevard", sizeof(vs.navStreet) - 1);
    strncpy(vs.navDest, "City Center Plaza", sizeof(vs.navDest) - 1);
  }
  vs.navRoutePct = 20;
  triggerAlert("ROUTE GUIDANCE UPDATED", T().green, 2500);
  if (navBigArrowBox) lv_obj_invalidate(navBigArrowBox);
}

static void buildPageNav() {
  lv_obj_t *hudCard = makeCard(pageContainer, 12, 6, 452, 220);
  makeLabel(hudCard, LV_SYMBOL_GPS "  HEADS-UP NAVIGATION", FONT_XS, T().green, 18, 14);

  navBigArrowBox = makeCard(hudCard, 18, 42, 114, 114, T().surfaceAlt, 18);
  lv_obj_add_event_cb(navBigArrowBox, drawNavArrowCb, LV_EVENT_DRAW_MAIN, nullptr);

  navBigDistLbl   = makeLabel(hudCard, "250 m", FONT_XL, T().text, 148, 44);
  navBigActionLbl = makeLabel(hudCard, "TURN RIGHT", FONT_MD, T().green, 148, 82);
  navBigStreetLbl = makeLabel(hudCard, vs.navStreet, FONT_MD, T().muted, 148, 112, 290);

  navProgBar = lv_bar_create(hudCard);
  lv_obj_set_pos(navProgBar, 18, 174);
  lv_obj_set_size(navProgBar, 416, 8);
  lv_bar_set_value(navProgBar, vs.navRoutePct, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(navProgBar, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(navProgBar, lv_color_hex(T().green), LV_PART_INDICATOR);

  navEtaDetailLbl = makeLabel(hudCard, "Dest: Visteon Tech Park  |  16 min  |  11.4 km",
                              FONT_XS, T().cyan, 18, 190, 416);

  lv_obj_t *destCard = makeCard(pageContainer, 12, 234, 452, 134);
  makeLabel(destCard, "QUICK DESTINATIONS", FONT_XS, T().muted, 18, 14);

  makeBtn(destCard, "Visteon Park\n11.4 km", 16, 42, 136, 74,
          [](lv_event_t *e) { applyRoutePreset(0); }, nullptr, T().surfaceAlt);
  makeBtn(destCard, "BLR Airport\n31.2 km", 158, 42, 136, 74,
          [](lv_event_t *e) { applyRoutePreset(1); }, nullptr, T().surfaceAlt);
  makeBtn(destCard, "City Center\n9.8 km", 300, 42, 136, 74,
          [](lv_event_t *e) { applyRoutePreset(2); }, nullptr, T().surfaceAlt);

  // Right Top: Incoming Call & Phone Status Card
  lv_obj_t *callCard = makeCard(pageContainer, 474, 6, 314, 170);
  navCallStatusLbl = makeLabel(callCard, "HANDS-FREE CALL CONTROL", FONT_XS, T().cyan, 18, 14);
  navPhoneBattLbl  = makeLabel(callCard, "Phone: --", FONT_XS, T().muted, 210, 14);
  navCallerLbl     = makeLabel(callCard, vs.callActive ? vs.callerName : "Ready for Phone Calls",
                               FONT_LG, T().text, 18, 42, 278);

  makeBtn(callCard, LV_SYMBOL_CALL " ACCEPT", 16, 98, 92, 50, [](lv_event_t *e) {
    vs.callActive = true;
    vs.callAnswered = true;
    sendBlePacket("CMD:CALL:ACCEPT");
    triggerAlert("CALL ACCEPTED", T().green, 2500);
  }, nullptr, T().green, 0x06090F);

  makeBtn(callCard, LV_SYMBOL_CLOSE " END", 114, 98, 84, 50, [](lv_event_t *e) {
    vs.callActive = false;
    vs.callAnswered = false;
    strncpy(vs.callerName, "No Active Call", sizeof(vs.callerName) - 1);
    sendBlePacket("CMD:CALL:DECLINE");
  }, nullptr, T().red, 0xFFFFFF);

  makeBtn(callCard, "SIM CALL", 204, 98, 94, 50, [](lv_event_t *e) {
    vs.callActive = true;
    vs.callAnswered = false;
    strncpy(vs.callerName, "Aditya (Mobile)", sizeof(vs.callerName) - 1);
    triggerAlert("INCOMING CALL: ADITYA", T().green, 5000);
  });

  // Right Bottom: Real Phone Notification Stream
  lv_obj_t *notifCard = makeCard(pageContainer, 474, 184, 314, 184);
  makeLabel(notifCard, LV_SYMBOL_BELL "  NOTIFICATION STREAM", FONT_XS, T().amber, 18, 14);
  navNotifLbl = makeLabel(notifCard, vs.notifText, FONT_MD, T().text, 18, 44, 278);
  lv_label_set_long_mode(navNotifLbl, LV_LABEL_LONG_WRAP);

  makeBtn(notifCard, "DISMISS", 16, 126, 134, 42, [](lv_event_t *e) {
    strncpy(vs.notifText, "All caught up. No unread alerts.", sizeof(vs.notifText) - 1);
    if (navNotifLbl) lv_label_set_text(navNotifLbl, vs.notifText);
  });
  makeBtn(notifCard, "PING PHONE", 160, 126, 138, 42, [](lv_event_t *e) {
    sendBlePacket("CMD:PHONE:PING");
    triggerAlert("PINGING PAIRED PHONE...", VISTEON_ORANGE, 3000);
  }, nullptr, VISTEON_ORANGE, 0x06090F);
}

// -----------------------------------------------------------------------------
// Page 3: MEDIA (Infotainment Audio Hub)
// -----------------------------------------------------------------------------
static void refreshMediaEqBtns() {
  for (int i = 0; i < 4; ++i) {
    if (!medEqBtns[i]) continue;
    bool act = (vs.eqPreset == i);
    lv_obj_set_style_bg_color(medEqBtns[i], lv_color_hex(act ? VISTEON_ORANGE : T().surfaceAlt), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(medEqBtns[i], 0),
                                lv_color_hex(act ? 0x06090F : T().text), 0);
  }
}

static void buildPageMedia() {
  lv_obj_t *artCard = makeCard(pageContainer, 12, 6, 278, 362);
  makeLabel(artCard, "SPATIAL AUDIO ENGINE", FONT_XS, T().muted, 18, 12);

  medDiscCanvas = lv_obj_create(artCard);
  lv_obj_remove_style_all(medDiscCanvas);
  lv_obj_set_pos(medDiscCanvas, 49, 40);
  lv_obj_set_size(medDiscCanvas, 180, 180);
  lv_obj_add_event_cb(medDiscCanvas, drawMediaDiscCb, LV_EVENT_DRAW_MAIN, nullptr);

  makeLabel(artCard, "LIVE SPECTRUM", FONT_XS, T().cyan, 18, 232);
  medEqCanvas = lv_obj_create(artCard);
  lv_obj_remove_style_all(medEqCanvas);
  lv_obj_set_pos(medEqCanvas, 18, 254);
  lv_obj_set_size(medEqCanvas, 242, 92);
  lv_obj_add_event_cb(medEqCanvas, drawMediaEqCb, LV_EVENT_DRAW_MAIN, nullptr);

  lv_obj_t *ctrlCard = makeCard(pageContainer, 300, 6, 488, 362);
  makeLabel(ctrlCard, LV_SYMBOL_BLUETOOTH "  COMPANION LIVE AUDIO", FONT_XS, T().cyan, 24, 16);

  medTitleLbl  = makeLabel(ctrlCard, vs.mediaTitle, FONT_XL, T().text, 24, 42, 440);
  medArtistLbl = makeLabel(ctrlCard, vs.mediaArtist, FONT_LG, T().muted, 24, 80, 440);

  // Scrubbable Progress Slider (sends CMD:MEDIA:SEEK:<pct> on release)
  medProgSlider = lv_slider_create(ctrlCard);
  lv_obj_set_pos(medProgSlider, 24, 128);
  lv_obj_set_size(medProgSlider, 440, 10);
  lv_slider_set_range(medProgSlider, 0, 100);
  lv_slider_set_value(medProgSlider, vs.mediaProgress, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(VISTEON_ORANGE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(medProgSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(medProgSlider, [](lv_event_t *e) {
    vs.mediaProgress = lv_slider_get_value(lv_event_get_target(e));
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(medProgSlider, [](lv_event_t *e) {
    vs.mediaProgress = lv_slider_get_value(lv_event_get_target(e));
    char pkt[28];
    snprintf(pkt, sizeof(pkt), "CMD:MEDIA:SEEK:%d", vs.mediaProgress);
    sendBlePacket(pkt);
  }, LV_EVENT_RELEASED, nullptr);

  medTimeLbl = makeLabel(ctrlCard, "00:00 / 03:50", FONT_XS, T().muted, 24, 146);

  makeBtn(ctrlCard, LV_SYMBOL_PREV "  PREV", 24, 174, 124, 52, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:PREV");
  });
  lv_obj_t *playBtn = makeBtn(ctrlCard, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY",
                              162, 170, 164, 60, [](lv_event_t *e) {
    vs.mediaPlaying = !vs.mediaPlaying;
    sendBlePacket(vs.mediaPlaying ? "CMD:MEDIA:PLAY" : "CMD:MEDIA:PAUSE");
    if (medPlayBtnLbl) lv_label_set_text(medPlayBtnLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY");
  }, nullptr, VISTEON_ORANGE, 0x06090F, 14);
  medPlayBtnLbl = lv_obj_get_child(playBtn, 0);
  makeBtn(ctrlCard, "NEXT  " LV_SYMBOL_NEXT, 340, 174, 124, 52, [](lv_event_t *e) {
    sendBlePacket("CMD:MEDIA:NEXT");
  });

  // Volume Bar & Mute (throttled BLE updates)
  medVolLbl = makeLabel(ctrlCard, LV_SYMBOL_VOLUME_MAX "  70%", FONT_SM, T().text, 24, 250);
  medVolSlider = lv_slider_create(ctrlCard);
  lv_obj_set_pos(medVolSlider, 114, 254);
  lv_obj_set_size(medVolSlider, 270, 12);
  lv_slider_set_range(medVolSlider, 0, 100);
  lv_slider_set_value(medVolSlider, vs.mediaVolume, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(T().cyan), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(medVolSlider, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
  lv_obj_add_event_cb(medVolSlider, [](lv_event_t *e) {
    vs.mediaMuted = false;
    vs.mediaVolume = lv_slider_get_value(lv_event_get_target(e));
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
    vs.mediaVolume = lv_slider_get_value(lv_event_get_target(e));
    char pkt[24];
    snprintf(pkt, sizeof(pkt), "CMD:VOL:%d", vs.mediaVolume);
    sendBlePacket(pkt);
  }, LV_EVENT_RELEASED, nullptr);

  makeBtn(ctrlCard, LV_SYMBOL_MUTE, 400, 242, 64, 34, [](lv_event_t *e) {
    vs.mediaMuted = !vs.mediaMuted;
    sendBlePacket(vs.mediaMuted ? "CMD:VOL:MUTE" : "CMD:VOL:UNMUTE");
  });

  // Acoustic EQ Presets
  makeLabel(ctrlCard, "CABIN ACOUSTICS", FONT_XS, T().muted, 24, 290);
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
    lv_label_set_text(vehTpmsLbl[i], buf);
    uint32_t c = (vs.tpms[i] < 29.0f) ? T().red : ((vs.tpms[i] < 32.0f) ? T().amber : T().green);
    lv_obj_set_style_text_color(vehTpmsLbl[i], lv_color_hex(c), 0);
  }
  if (vehLightBtn) lv_obj_set_style_bg_color(vehLightBtn, lv_color_hex(vs.headlightsOn ? T().amber : T().surfaceAlt), 0);
  if (vehLockBtn)  lv_obj_set_style_bg_color(vehLockBtn,  lv_color_hex(vs.doorsLocked ? T().cyan : T().surfaceAlt), 0);
  if (vehTrunkBtn) lv_obj_set_style_bg_color(vehTrunkBtn, lv_color_hex(vs.trunkOpen ? VISTEON_ORANGE : T().surfaceAlt), 0);

  if (vehDrvTempLbl) {
    lv_label_set_text_fmt(vehDrvTempLbl, "%d C", vs.tempDriver);
    lv_obj_set_style_text_color(vehDrvTempLbl, lv_color_hex(vs.tempDriver >= 24 ? VISTEON_ORANGE : T().cyan), 0);
  }
  if (vehPasTempLbl) {
    lv_label_set_text_fmt(vehPasTempLbl, "%d C", vs.tempPassenger);
    lv_obj_set_style_text_color(vehPasTempLbl, lv_color_hex(vs.tempPassenger >= 24 ? VISTEON_ORANGE : T().cyan), 0);
  }
  if (vehFanLbl)     lv_label_set_text_fmt(vehFanLbl, "FAN LVL %d", vs.fanSpeed);
  if (vehAcBtn)      lv_obj_set_style_bg_color(vehAcBtn, lv_color_hex(vs.acOn ? T().cyan : T().surfaceAlt), 0);
  if (vehAutoBtn)    lv_obj_set_style_bg_color(vehAutoBtn, lv_color_hex(vs.autoClimate ? T().green : T().surfaceAlt), 0);
  if (vehDefrostBtn) lv_obj_set_style_bg_color(vehDefrostBtn, lv_color_hex(vs.defrostOn ? VISTEON_ORANGE : T().surfaceAlt), 0);
  if (vehSeatBtn) {
    lv_obj_set_style_bg_color(vehSeatBtn, lv_color_hex(vs.seatHeat > 0 ? T().red : T().surfaceAlt), 0);
    lv_label_set_text_fmt(lv_obj_get_child(vehSeatBtn, 0), "SEAT %d", vs.seatHeat);
  }
  if (vehRegenBtn) {
    const char *rTxt[3] = {"REGEN: LOW", "REGEN: STD", "ONE-PEDAL"};
    lv_label_set_text(lv_obj_get_child(vehRegenBtn, 0), rTxt[vs.regenLevel]);
  }
}

static void buildPageVehicle() {
  lv_obj_t *carCard = makeCard(pageContainer, 12, 6, 386, 362);
  makeLabel(carCard, "CHASSIS & TIRE PRESSURE (TPMS)", FONT_XS, T().muted, 16, 12);

  vehChassisCanvas = lv_obj_create(carCard);
  lv_obj_remove_style_all(vehChassisCanvas);
  lv_obj_set_pos(vehChassisCanvas, 108, 34);
  lv_obj_set_size(vehChassisCanvas, 170, 250);
  lv_obj_add_event_cb(vehChassisCanvas, drawVehicleChassisCb, LV_EVENT_DRAW_MAIN, nullptr);

  int tx[4] = {16, 294, 16, 294};
  int ty[4] = {76, 76, 198, 198};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *box = makeCard(carCard, tx[i], ty[i], 78, 52, T().surfaceAlt, 10);
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
    triggerAlert(vs.doorsLocked ? "DOORS LOCKED" : "DOORS UNLOCKED", T().cyan, 2000);
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
      triggerAlert("LOW TIRE PRESSURE: REAR RIGHT 24.5 PSI", T().red, 5000);
    } else {
      vs.tpms[3] = 34.1f;
      triggerAlert("ALL TIRES NOMINAL", T().green, 2500);
    }
    refreshVehicleWidgets();
  });

  lv_obj_t *hvacCard = makeCard(pageContainer, 408, 6, 380, 224);
  makeLabel(hvacCard, "DUAL-ZONE CABIN CLIMATE", FONT_XS, T().muted, 16, 12);

  makeLabel(hvacCard, "DRIVER", FONT_XS, T().muted, 58, 36);
  makeBtn(hvacCard, "-", 16, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempDriver > 16) vs.tempDriver--;
    refreshVehicleWidgets();
  });
  vehDrvTempLbl = makeLabel(hvacCard, "21 C", FONT_LG, T().cyan, 62, 66);
  makeBtn(hvacCard, "+", 126, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempDriver < 30) vs.tempDriver++;
    refreshVehicleWidgets();
  });

  makeLabel(hvacCard, "PASSENGER", FONT_XS, T().muted, 246, 36);
  makeBtn(hvacCard, "-", 214, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempPassenger > 16) vs.tempPassenger--;
    refreshVehicleWidgets();
  });
  vehPasTempLbl = makeLabel(hvacCard, "22 C", FONT_LG, T().cyan, 260, 66);
  makeBtn(hvacCard, "+", 324, 56, 38, 44, [](lv_event_t *e) {
    if (vs.tempPassenger < 30) vs.tempPassenger++;
    refreshVehicleWidgets();
  });

  vehFanLbl = makeLabel(hvacCard, "FAN LVL 3", FONT_XS, T().text, 16, 120);
  lv_obj_t *fanSld = lv_slider_create(hvacCard);
  lv_obj_set_pos(fanSld, 110, 122);
  lv_obj_set_size(fanSld, 250, 12);
  lv_slider_set_range(fanSld, 0, 5);
  lv_slider_set_value(fanSld, vs.fanSpeed, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(fanSld, lv_color_hex(T().surfaceAlt), LV_PART_MAIN);
  lv_obj_set_style_bg_color(fanSld, lv_color_hex(T().cyan), LV_PART_INDICATOR);
  lv_obj_add_event_cb(fanSld, [](lv_event_t *e) {
    vs.fanSpeed = lv_slider_get_value(lv_event_get_target(e));
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
  makeLabel(hvCard, LV_SYMBOL_CHARGE "  HV POWERTRAIN & THERMAL", FONT_XS, T().green, 16, 12);
  vehHvStatsLbl = makeLabel(hvCard, "Pack: 398.4 V   |   Temp: 31 C   |   SOH: 99%\nOdometer: 12480.4 km   |   Trip A: 48.6 km",
                            FONT_SM, T().text, 16, 36);
  vehRegenBtn = makeBtn(hvCard, "REGEN: STD", 16, 82, 170, 36, [](lv_event_t *e) {
    vs.regenLevel = (vs.regenLevel + 1) % 3;
    refreshVehicleWidgets();
  }, nullptr, T().surfaceAlt);
  makeBtn(hvCard, "RESET TRIP A", 198, 82, 166, 36, [](lv_event_t *e) {
    vs.tripKm = 0.0f;
    triggerAlert("TRIP A ODOMETER RESET", T().cyan, 2000);
  });

  refreshVehicleWidgets();
}

// -----------------------------------------------------------------------------
// Page 5: CONNECT (Wi-Fi + On-Screen Keyboard + BLE 5.0 Console)
// -----------------------------------------------------------------------------
static void openWifiPasswordModal(const char *ssid) {
  strncpy(pendingWifiSsid, ssid, sizeof(pendingWifiSsid) - 1);
  pendingWifiSsid[sizeof(pendingWifiSsid) - 1] = '\0';
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
}

static void buildPageConnect() {
  lv_obj_t *wifiCard = makeCard(pageContainer, 12, 6, 384, 362);
  conWifiStatusLbl = makeLabel(wifiCard, LV_SYMBOL_WIFI "  WI-FI MANAGER", FONT_MD, T().cyan, 16, 12);
  conWifiDetailLbl = makeLabel(wifiCard, "Checking radio...", FONT_XS, T().muted, 16, 38, 352);

  makeBtn(wifiCard, "CONNECT ANSHIK", 16, 66, 174, 38, [](lv_event_t *e) {
    startWifiConnection(SAVED_SSID, SAVED_PASSWORD);
  }, nullptr, VISTEON_ORANGE, 0x06090F);

  makeBtn(wifiCard, LV_SYMBOL_REFRESH "  SCAN 2.4G", 198, 66, 170, 38, [](lv_event_t *e) {
    scanWifiNetworks();
  });

  conWifiList = lv_obj_create(wifiCard);
  lv_obj_set_pos(conWifiList, 16, 114);
  lv_obj_set_size(conWifiList, 352, 234);
  lv_obj_set_scroll_dir(conWifiList, LV_DIR_VER);
  lv_obj_set_style_bg_color(conWifiList, lv_color_hex(T().surfaceDeep), 0);
  lv_obj_set_style_border_color(conWifiList, lv_color_hex(T().border), 0);
  lv_obj_set_style_radius(conWifiList, 10, 0);
  lv_obj_set_style_pad_all(conWifiList, 6, 0);
  makeLabel(conWifiList, "Tap SCAN 2.4G to discover networks.\nTap any SSID to enter password.", FONT_XS, T().muted, 8, 8);

  lv_obj_t *bleCard = makeCard(pageContainer, 406, 6, 382, 362);
  conBleStatusLbl = makeLabel(bleCard, bleConnected ? LV_SYMBOL_BLUETOOTH "  PHONE CONNECTED" : LV_SYMBOL_BLUETOOTH "  ADVERTISING: Visteon-Lunet",
                              FONT_MD, bleConnected ? T().green : T().cyan, 16, 12);
  conBlePhoneLbl  = makeLabel(bleCard, "MTU 247  |  Phone Batt: --", FONT_XS, T().muted, 16, 38);

  makeBtn(bleCard, "RESTART ADV", 16, 66, 114, 38, [](lv_event_t *e) {
    restartBleAdvertising();
  });
  makeBtn(bleCard, "SYNC PHONE", 136, 66, 114, 38, [](lv_event_t *e) {
    sendBlePacket("CMD:SYNC:REQ");
    triggerAlert(bleConnected ? "SYNC REQUESTED" : "WAITING FOR APP", bleConnected ? T().green : T().amber, 2200);
  }, nullptr, T().cyan, 0x06090F);
  makeBtn(bleCard, "PING PHONE", 256, 66, 110, 38, [](lv_event_t *e) {
    sendBlePacket("CMD:PHONE:PING");
    triggerAlert("PING PACKET SENT", VISTEON_ORANGE, 2200);
  });

  lv_obj_t *termBox = makeCard(bleCard, 16, 114, 350, 234, T().surfaceDeep, 10);
  lv_obj_add_flag(termBox, LV_OBJ_FLAG_SCROLLABLE);
  conBleLogLbl = makeLabel(termBox, "", FONT_XS, T().text, 10, 8, 326);
  lv_label_set_long_mode(conBleLogLbl, LV_LABEL_LONG_WRAP);
}

// -----------------------------------------------------------------------------
// Page 6: SYSTEM (Telemetry Chart, Memory, Units, Touch Pad, Boot Replay)
// -----------------------------------------------------------------------------
static void buildPageSystem() {
  lv_obj_t *chartCard = makeCard(pageContainer, 12, 6, 434, 230);
  makeLabel(chartCard, "REAL-TIME TELEMETRY  (ORANGE: SPEED  |  GREEN: POWER)", FONT_XS, T().muted, 16, 10);

  sysChart = lv_chart_create(chartCard);
  lv_obj_set_pos(sysChart, 14, 34);
  lv_obj_set_size(sysChart, 406, 184);
  lv_chart_set_type(sysChart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(sysChart, 36);
  lv_chart_set_range(sysChart, LV_CHART_AXIS_PRIMARY_Y, 0, 200);
  lv_chart_set_div_line_count(sysChart, 4, 6);
  lv_obj_set_style_bg_color(sysChart, lv_color_hex(T().surfaceDeep), 0);
  lv_obj_set_style_border_color(sysChart, lv_color_hex(T().border), 0);
  lv_obj_set_style_line_width(sysChart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(sysChart, 0, LV_PART_INDICATOR);

  serSpeed = lv_chart_add_series(sysChart, lv_color_hex(VISTEON_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
  serPower = lv_chart_add_series(sysChart, lv_color_hex(T().green), LV_CHART_AXIS_PRIMARY_Y);
  for (int i = 0; i < 36; ++i) {
    lv_chart_set_next_value(sysChart, serSpeed, (lv_coord_t)vs.speedKmh);
    lv_chart_set_next_value(sysChart, serPower, (lv_coord_t)(vs.powerKw + 60));
  }

  lv_obj_t *hwCard = makeCard(pageContainer, 12, 244, 434, 124);
  makeLabel(hwCard, "ESP32-S3 HARDWARE DIAGNOSTICS", FONT_XS, T().cyan, 16, 12);
  sysMemLbl = makeLabel(hwCard, "CPU: 240 MHz Dual-Core LX7  |  ST7262 800x480\nHeap: -- KB  |  OPI PSRAM: -- KB\nUptime: -- s",
                        FONT_SM, T().text, 16, 36);

  lv_obj_t *setCard = makeCard(pageContainer, 456, 6, 332, 362);
  makeLabel(setCard, "CLUSTER SETTINGS & DEMO TOOLS", FONT_XS, T().muted, 16, 12);

  lv_obj_t *uBtn = makeBtn(setCard, vs.useMph ? "UNITS: MPH (IMPERIAL)" : "UNITS: KM/H (METRIC)",
                           16, 40, 300, 42, [](lv_event_t *e) {
    vs.useMph = !vs.useMph;
    if (sysUnitBtnLbl) lv_label_set_text(sysUnitBtnLbl, vs.useMph ? "UNITS: MPH (IMPERIAL)" : "UNITS: KM/H (METRIC)");
    triggerAlert(vs.useMph ? "SWITCHED TO MPH" : "SWITCHED TO KM/H", T().cyan, 2000);
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

  makeLabel(setCard, "GT911 CAPACITIVE TOUCH TEST PAD", FONT_XS, T().green, 16, 198);
  lv_obj_t *pad = makeCard(setCard, 16, 218, 300, 128, T().surfaceDeep, 12);
  sysTouchLbl = makeLabel(pad, "Touch screen to verify GT911 (X, Y)", FONT_XS, T().muted, 12, 10);

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
    lv_obj_set_style_bg_color(dockBtns[i], lv_color_hex(active ? T().surfaceAlt : T().surface), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(dockBtns[i], 0),
                                lv_color_hex(active ? VISTEON_ORANGE : T().muted), 0);
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
  lv_obj_set_style_bg_color(scr, lv_color_hex(T().bg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);

  // === PERSISTENT TOP BAR (800 x 46) ===
  topBar = lv_obj_create(scr);
  lv_obj_set_pos(topBar, 0, 0);
  lv_obj_set_size(topBar, 800, 46);
  lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(topBar, lv_color_hex(T().bg), 0);
  lv_obj_set_style_border_width(topBar, 0, 0);
  lv_obj_set_style_pad_all(topBar, 0, 0);

  // Top-Left Exact Vector Visteon V Logo (tap replays boot animation)
  lv_obj_t *logoHit = lv_obj_create(topBar);
  lv_obj_remove_style_all(logoHit);
  lv_obj_set_pos(logoHit, 14, 10);
  lv_obj_set_size(logoHit, 32, 26);
  lv_obj_add_flag(logoHit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoHit, drawVisteonLogoCb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(logoHit, [](lv_event_t *e) { showBootSplash(); }, LV_EVENT_CLICKED, nullptr);

  makeLabel(topBar, "VISTEON", FONT_MD, T().text, 52, 13);
  makeLabel(topBar, "LUNET", FONT_XS, VISTEON_ORANGE, 128, 16);

  // Center Dynamic Status / Alert / Overspeed Pill
  dynamicPill = makeCard(topBar, 200, 6, 396, 34, T().surface, 17);
  dynamicPillLabel = lv_label_create(dynamicPill);
  lv_label_set_text(dynamicPillLabel, "12:00 PM   |   Sat, 21 Nov   |   27C Clear");
  lv_obj_set_style_text_font(dynamicPillLabel, FONT_SM, 0);
  lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(T().text), 0);
  lv_obj_center(dynamicPillLabel);

  // Right System Status Icons (Direct Supply = 100%)
  topWifiIcon    = makeLabel(topBar, LV_SYMBOL_WIFI, FONT_SM, T().muted, 610, 15);
  topBleIcon     = makeLabel(topBar, LV_SYMBOL_BLUETOOTH, FONT_SM, T().cyan, 636, 15);
  topSupplyLabel = makeLabel(topBar, LV_SYMBOL_CHARGE " 100%", FONT_XS, T().green, 658, 16);

  // Icon-Only Sun / Crescent Moon Theme Button (Zero Text)
  lv_obj_t *themeIconBtn = makeBtn(topBar, "", 744, 7, 42, 32, [](lv_event_t *e) {
    lightTheme = !lightTheme;
    buildShell();
  }, nullptr, T().surfaceAlt, T().text, 16);
  lv_obj_add_event_cb(themeIconBtn, drawThemeIconCb, LV_EVENT_DRAW_MAIN_END, nullptr);

  // === ACTIVE PAGE CONTAINER (800 x 376) ===
  pageContainer = lv_obj_create(scr);
  lv_obj_remove_style_all(pageContainer);
  lv_obj_set_pos(pageContainer, 0, 46);
  lv_obj_set_size(pageContainer, 800, 376);
  lv_obj_clear_flag(pageContainer, LV_OBJ_FLAG_SCROLLABLE);

  // === PERSISTENT FLOATING BOTTOM DOCK ===
  lv_obj_t *dock = makeCard(scr, 44, 424, 712, 50, T().surface, 18);
  const char *dockNames[PAGE_COUNT] = {
    LV_SYMBOL_HOME " DRIVE",
    LV_SYMBOL_GPS " NAV",
    LV_SYMBOL_AUDIO " MEDIA",
    LV_SYMBOL_CHARGE " VEHICLE",
    LV_SYMBOL_WIFI " CONNECT",
    LV_SYMBOL_SETTINGS " SYSTEM"
  };

  for (int i = 0; i < PAGE_COUNT; ++i) {
    int bx = 8 + i * 116;
    dockBtns[i] = makeBtn(dock, dockNames[i], bx, 5, 112, 40,
      [](lv_event_t *e) {
        selectPage((PageId)(uintptr_t)lv_event_get_user_data(e));
      }, (void *)(uintptr_t)i, T().surface, T().muted, 12);
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
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    bleRestartPending = false;
    bleSyncReqPending = true;
    bleConnectedAt = millis();
    Serial.println("[BLE] Companion app connected");
  }
  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    bleSyncReqPending = false;
    bleRestartPending = true;
    bleDisconnectAt = millis();
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
  bleCharacteristic->setValue(packet);
  if (bleConnected) {
    bleCharacteristic->notify();
    Serial.printf("[BLE TX] %s\n", packet);
  }
}

static void startBle() {
  if (bleReady) return;
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(247);

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new LunetServerCallbacks());

  BLEService *service = server->createService(BLE_SERVICE_UUID);
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

static void restartBleAdvertising() {
  if (!bleReady) startBle();
  else if (!bleConnected) {
    BLEDevice::startAdvertising();
    bleRestartPending = false;
    appendBleLog("SYS", "Advertising restarted");
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
  } else if (cmd.startsWith("WEATHER:")) {
    strncpy(vs.weatherStr, cmd.substring(8).c_str(), sizeof(vs.weatherStr) - 1);
    vs.weatherStr[sizeof(vs.weatherStr) - 1] = '\0';
  } else if (cmd.startsWith("PBATT:")) {
    vs.phoneBattery = constrain(cmd.substring(6).toInt(), 0, 100);
  } else if (cmd.startsWith("BATTERY:")) {
    vs.phoneBattery = constrain(cmd.substring(8).toInt(), 0, 100);
  } else if (cmd.startsWith("SPEED:")) {
    vs.demoDrive = false;
    float spd = constrain(cmd.substring(6).toFloat(), 0.0f, 220.0f);
    if (spd > 0.0f && vs.gear == 'P') vs.gear = 'D';
    vs.targetSpeedKmh = spd;
    if (drvThrottleSld) lv_slider_set_value(drvThrottleSld, (int)constrain(spd, 0.0f, 200.0f), LV_ANIM_OFF);
    refreshDriveStyles();
  } else if (cmd.startsWith("LIMIT:")) {
    vs.speedLimitKmh = constrain(cmd.substring(6).toInt(), 20, 200);
    if (drvSpeedLimitLbl) lv_label_set_text_fmt(drvSpeedLimitLbl, "%d", displayLimit());
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
    vs.socPercent = constrain(cmd.substring(4).toFloat(), 0.0f, 100.0f);
  } else if (cmd.startsWith("NOTIF:")) {
    // Format: NOTIF:<appName>|<message>  OR  NOTIF:<message>
    String payload = cmd.substring(6);
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (n == 2) {
      snprintf(vs.notifText, sizeof(vs.notifText), "%s: %s", parts[0].c_str(), parts[1].c_str());
    } else {
      strncpy(vs.notifText, payload.c_str(), sizeof(vs.notifText) - 1);
      vs.notifText[sizeof(vs.notifText) - 1] = '\0';
    }
    triggerAlert(vs.notifText, T().amber, 4500);
  } else if (cmd.startsWith("CALL:")) {
    // Format: CALL:INCOMING|<name> | CALL:ACTIVE|<name> | CALL:ENDED
    String payload = cmd.substring(5);
    String parts[2];
    int n = splitPipes(payload, parts, 2);
    if (parts[0].equalsIgnoreCase("INCOMING")) {
      vs.callActive = true;
      vs.callAnswered = false;
      strncpy(vs.callerName, (n == 2 && parts[1].length() > 0) ? parts[1].c_str() : "Unknown Caller", sizeof(vs.callerName) - 1);
      char alert[64];
      snprintf(alert, sizeof(alert), "INCOMING CALL: %s", vs.callerName);
      triggerAlert(alert, T().green, 8000);
    } else if (parts[0].equalsIgnoreCase("ACTIVE")) {
      vs.callActive = true;
      vs.callAnswered = true;
      if (n == 2 && parts[1].length() > 0) strncpy(vs.callerName, parts[1].c_str(), sizeof(vs.callerName) - 1);
      triggerAlert("CALL CONNECTED", T().green, 2500);
    } else if (parts[0].equalsIgnoreCase("ENDED")) {
      vs.callActive = false;
      vs.callAnswered = false;
      strncpy(vs.callerName, "No Active Call", sizeof(vs.callerName) - 1);
      triggerAlert("CALL ENDED", T().muted, 2000);
    } else {
      // Legacy fallback: CALL:<name>
      vs.callActive = true;
      vs.callAnswered = false;
      strncpy(vs.callerName, payload.c_str(), sizeof(vs.callerName) - 1);
      triggerAlert("INCOMING CALL", T().green, 6000);
    }
  } else if (cmd.startsWith("MEDIA:")) {
    // Format: MEDIA:<title>|<artist>|<PLAY|PAUSE>|<progress_0_100>|<vol_0_100>
    String parts[5];
    int n = splitPipes(cmd.substring(6), parts, 5);
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
      vs.mediaProgress = constrain(parts[3].toInt(), 0, 100);
    }
    if (n >= 5) {
      vs.mediaVolume = constrain(parts[4].toInt(), 0, 100);
      if (medVolSlider) lv_slider_set_value(medVolSlider, vs.mediaVolume, LV_ANIM_OFF);
    }
    if (drvMediaPlayLbl) lv_label_set_text(drvMediaPlayLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (medPlayBtnLbl)   lv_label_set_text(medPlayBtnLbl, vs.mediaPlaying ? LV_SYMBOL_PAUSE "  PAUSE" : LV_SYMBOL_PLAY "  PLAY");
  } else if (cmd.startsWith("NAV:")) {
    // Format: NAV:<STRAIGHT|LEFT|RIGHT|UTURN>|<distM>|<street>|<etaMin>|<remKm>
    String payload = cmd.substring(4);
    String parts[5];
    int n = splitPipes(payload, parts, 5);
    if (n >= 3) {
      if (parts[0].equalsIgnoreCase("LEFT"))       vs.navDir = NAV_LEFT;
      else if (parts[0].equalsIgnoreCase("RIGHT")) vs.navDir = NAV_RIGHT;
      else if (parts[0].equalsIgnoreCase("UTURN")) vs.navDir = NAV_UTURN;
      else                                         vs.navDir = NAV_STRAIGHT;

      vs.navDistM = max(0, parts[1].toInt());
      strncpy(vs.navStreet, parts[2].c_str(), sizeof(vs.navStreet) - 1);
      vs.navStreet[sizeof(vs.navStreet) - 1] = '\0';
      if (n >= 4) vs.navEtaMin = max(0, parts[3].toInt());
      if (n >= 5) vs.navRemKm  = max(0.0f, parts[4].toFloat());
    } else {
      strncpy(vs.navStreet, payload.c_str(), sizeof(vs.navStreet) - 1);
      vs.navStreet[sizeof(vs.navStreet) - 1] = '\0';
    }
    if (drvNavArrowBox) lv_obj_invalidate(drvNavArrowBox);
    if (navBigArrowBox) lv_obj_invalidate(navBigArrowBox);
  }
}

// -----------------------------------------------------------------------------
// Wi-Fi Engine & IST NTP Clock Sync
// -----------------------------------------------------------------------------
static void startWifiConnection(const char *ssid, const char *password) {
  if (!ssid || !ssid[0]) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  delay(40);
  WiFi.begin(ssid, password ? password : "");
  strncpy(activeWifiName, ssid, sizeof(activeWifiName) - 1);
  triggerAlert("CONNECTING WI-FI...", T().cyan, 3000);
}

static void scanWifiNetworks() {
  if (wifiScanInProgress) return;
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true, false);
  wifiScanInProgress = true;
  if (conWifiList) {
    lv_obj_clean(conWifiList);
    makeLabel(conWifiList, "Scanning 2.4 GHz networks...", FONT_XS, T().cyan, 8, 8);
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
      makeLabel(conWifiList, "No 2.4 GHz networks found.", FONT_XS, T().amber, 8, 8);
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
      }, (void *)(uintptr_t)i, T().surfaceAlt, T().text, 8);
    lv_obj_align(lv_obj_get_child(row, 0), LV_ALIGN_LEFT_MID, 10, 0);
  }
}

// -----------------------------------------------------------------------------
// 30 Hz Physics, Overspeed Red Flash & Reactive In-Place UI Updates
// -----------------------------------------------------------------------------
static void stepPhysicsAndAnimations(uint32_t now) {
  // 1. Boot Splash Progress (2.2s)
  if (bootActive) {
    uint32_t el = now - bootStartMs;
    int pct = (int)(el * 100 / 2100);
    if (pct > 100) pct = 100;
    if (bootBar) lv_bar_set_value(bootBar, pct, LV_ANIM_OFF);
    if (el >= 2200) {
      bootActive = false;
      if (bootOverlay) { lv_obj_del(bootOverlay); bootOverlay = nullptr; bootBar = nullptr; }
    }
  }

  // 2. Blinker Pulse (450ms)
  if (now - lastBlinkMs >= 450) {
    lastBlinkMs = now;
    blinkState = !blinkState;
  }

  // 3. Auto-Drive Demo Scenario (Only when manually toggled ON)
  float dt = 0.033f;
  if (vs.demoDrive && vs.gear == 'D') {
    float t = now * 0.001f;
    float base = (vs.mode == MODE_SPORT) ? 112.0f : ((vs.mode == MODE_COMFORT) ? 74.0f : 52.0f);
    float wave = 26.0f * sinf(t * 0.28f) + 12.0f * cosf(t * 0.65f);
    vs.targetSpeedKmh = constrain(base + wave, 18.0f, 185.0f);
    vs.leadDistM = 36.0f + 16.0f * sinf(t * 0.42f + 1.0f);
  } else if (vs.gear == 'P' || vs.gear == 'N') {
    vs.targetSpeedKmh = 0.0f;
  }

  // Smooth Speed & Power/Regen Physics
  float diff = vs.targetSpeedKmh - vs.speedKmh;
  float accelRate = (vs.mode == MODE_SPORT) ? 2.6f : ((vs.mode == MODE_COMFORT) ? 1.8f : 1.2f);
  if (fabsf(diff) > 0.3f) {
    vs.speedKmh += constrain(diff * 0.09f, -2.6f, accelRate);
  } else {
    vs.speedKmh = vs.targetSpeedKmh;
  }
  vs.braking = (diff < -6.0f);

  float targetPower = 0.0f;
  if (vs.gear == 'D' || vs.gear == 'R') {
    targetPower = (diff > 1.0f) ? (vs.speedKmh * 0.6f + diff * 2.2f)
                                : (vs.braking ? (-25.0f - (vs.regenLevel * 12.0f)) : (vs.speedKmh * 0.14f));
  }
  targetPower = constrain(targetPower, -60.0f, 180.0f);
  vs.powerKw += (targetPower - vs.powerKw) * 0.16f;
  if (vs.gear == 'P' && fabsf(vs.powerKw) < 0.4f) vs.powerKw = 0.0f;

  // Overspeed Detection & Full-Screen Pulsing Red Warning Flash
  isOverspeed = (vs.speedKmh > (float)vs.speedLimitKmh + 0.5f);
  if (overspeedOverlay) {
    if (isOverspeed && !bootActive && !kbModal) {
      lv_obj_clear_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN);
      float pulse = 0.5f + 0.5f * sinf(now * 0.014f); // Crisp ~2.2 Hz warning pulse
      lv_opa_t borderOpa = (lv_opa_t)(90 + (int)(pulse * 165.0f));
      lv_opa_t tintOpa   = (lv_opa_t)(8 + (int)(pulse * 34.0f));
      lv_obj_set_style_border_opa(overspeedOverlay, borderOpa, 0);
      lv_obj_set_style_bg_opa(overspeedOverlay, tintOpa, 0);
    } else {
      lv_obj_add_flag(overspeedOverlay, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Road Perspective Scroll & Trip Integration
  if (vs.speedKmh > 0.5f) {
    vs.roadPhase = fmodf(vs.roadPhase + (vs.speedKmh * 0.00055f), 1.0f);
    float dKm = (vs.speedKmh / 3600.0f) * dt;
    vs.tripKm += dKm;
    vs.odoKm  += dKm;
  }

  // Demo Nav Distance Countdown when driving without live phone Nav push
  if (vs.demoDrive && vs.speedKmh > 5.0f) {
    static float navAccumM = 0.0f;
    navAccumM += (vs.speedKmh / 3.6f) * dt;
    if (navAccumM >= 5.0f) {
      vs.navDistM -= (int)navAccumM;
      navAccumM = 0.0f;
      if (vs.navDistM <= 10) {
        vs.navDistM = 450;
        vs.navDir = (NavArrow)((vs.navDir + 1) % 3);
        vs.navRoutePct = (vs.navRoutePct + 8) % 96;
        if (drvNavArrowBox) lv_obj_invalidate(drvNavArrowBox);
        if (navBigArrowBox) lv_obj_invalidate(navBigArrowBox);
      }
    }
  }

  // Media Visualizer & Vinyl Spin
  if (vs.mediaPlaying) {
    vs.vinylAngle = fmodf(vs.vinylAngle + 3.2f, 360.0f);
    for (int i = 0; i < 12; ++i) {
      float targetH = 20.0f + 72.0f * (0.5f + 0.5f * sinf(now * 0.008f + i * 0.7f) * cosf(now * 0.004f + i * 1.1f));
      vs.eqBars[i] += (targetH - vs.eqBars[i]) * 0.35f;
    }
  } else {
    for (int i = 0; i < 12; ++i) vs.eqBars[i] += (8.0f - vs.eqBars[i]) * 0.15f;
  }

  // === FAST IN-PLACE REDRAWS (30 Hz) ===
  if (currentPage == PAGE_DRIVE) {
    if (drvSpeedArc) {
      lv_arc_set_value(drvSpeedArc, (int)vs.speedKmh);
      uint32_t arcCol = isOverspeed ? (blinkState ? 0xEF4444 : 0x991B1B) : modeAccentColor();
      lv_obj_set_style_arc_color(drvSpeedArc, lv_color_hex(arcCol), LV_PART_INDICATOR);
    }
    if (drvPowerArc) {
      lv_arc_set_value(drvPowerArc, (int)vs.powerKw);
      lv_obj_set_style_arc_color(drvPowerArc, lv_color_hex(vs.powerKw < 0 ? T().green : VISTEON_ORANGE), LV_PART_INDICATOR);
    }
    if (drvSpeedVal) {
      lv_label_set_text_fmt(drvSpeedVal, "%d", displaySpeed());
      lv_obj_set_style_text_color(drvSpeedVal, lv_color_hex(isOverspeed ? T().red : T().text), 0);
    }
    if (drvPowerVal) {
      int pInt = (int)lroundf(vs.powerKw);
      lv_label_set_text_fmt(drvPowerVal, "%s%d kW", (pInt > 0 ? "+" : ""), pInt);
      lv_obj_set_style_text_color(drvPowerVal, lv_color_hex(pInt < 0 ? T().green : T().muted), 0);
    }
    if (drvSpeedLimitBox) {
      lv_obj_set_style_bg_color(drvSpeedLimitBox, lv_color_hex((isOverspeed && blinkState) ? 0xEF4444 : 0xFFFFFF), 0);
      if (drvSpeedLimitLbl) {
        lv_obj_set_style_text_color(drvSpeedLimitLbl, lv_color_hex((isOverspeed && blinkState) ? 0xFFFFFF : 0x0F172A), 0);
      }
    }
    if (drvAdasCanvas) lv_obj_invalidate(drvAdasCanvas);
  } else if (currentPage == PAGE_MEDIA) {
    if (medDiscCanvas) lv_obj_invalidate(medDiscCanvas);
    if (medEqCanvas)   lv_obj_invalidate(medEqCanvas);
  }
}

static void updateSlowUi(uint32_t now) {
  const bool wifiOk = (WiFi.status() == WL_CONNECTED);

  // Automatic IST NTP Time Sync when Wi-Fi connects
  if (wifiOk && !ntpConfigured) {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
    ntpConfigured = true;
    triggerAlert("WI-FI CONNECTED  |  TIME SYNCED", T().green, 3000);
  }
  if (ntpConfigured && !bleTimeOverride) {
    struct tm ti;
    if (getLocalTime(&ti, 5)) {
      strftime(vs.timeStr, sizeof(vs.timeStr), "%I:%M %p", &ti);
      strftime(vs.dateStr, sizeof(vs.dateStr), "%a, %d %b", &ti);
    }
  }

  // Persistent Top Bar Icons (Top Bar Supply stays 100%)
  if (topWifiIcon)    lv_obj_set_style_text_color(topWifiIcon, lv_color_hex(wifiOk ? T().green : T().muted), 0);
  if (topBleIcon)     lv_obj_set_style_text_color(topBleIcon, lv_color_hex(bleConnected ? T().green : T().cyan), 0);
  if (topSupplyLabel) lv_label_set_text_fmt(topSupplyLabel, LV_SYMBOL_CHARGE " %d%%", vs.supplyBattery);

  // Dynamic Status / Alert / Overspeed Pill
  if (dynamicPill && dynamicPillLabel) {
    if (isOverspeed) {
      lv_obj_set_style_bg_color(dynamicPill, lv_color_hex(blinkState ? 0xEF4444 : 0x991B1B), 0);
      lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(0xFFFFFF), 0);
      char osBuf[64];
      snprintf(osBuf, sizeof(osBuf), LV_SYMBOL_WARNING "  OVERSPEED  |  LIMIT %d %s",
               displayLimit(), vs.useMph ? "MPH" : "KM/H");
      lv_label_set_text(dynamicPillLabel, osBuf);
    } else if (now < vs.alertUntil && vs.alertText[0]) {
      lv_obj_set_style_bg_color(dynamicPill, lv_color_hex(vs.alertColor), 0);
      lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(0x06090F), 0);
      lv_label_set_text(dynamicPillLabel, vs.alertText);
    } else {
      lv_obj_set_style_bg_color(dynamicPill, lv_color_hex(T().surface), 0);
      lv_obj_set_style_text_color(dynamicPillLabel, lv_color_hex(T().text), 0);
      char pillBuf[84];
      snprintf(pillBuf, sizeof(pillBuf), "%s   |   %s   |   %s", vs.timeStr, vs.dateStr, vs.weatherStr);
      lv_label_set_text(dynamicPillLabel, pillBuf);
    }
  }

  // Gentle media bar tick when playing without continuous seek packets
  static uint32_t lastProgSec = 0;
  if (vs.mediaPlaying && now - lastProgSec >= 2000) {
    lastProgSec = now;
    vs.mediaProgress = (vs.mediaProgress + 1) % 101;
  }

  // Active Page Slow Refreshes
  if (currentPage == PAGE_DRIVE) {
    if (drvAdasFooter) {
      if (vs.gear == 'P') {
        lv_label_set_text(drvAdasFooter, "PARKED  |  SHIFT TO D OR DEMO");
      } else if (vs.gear == 'R') {
        lv_label_set_text(drvAdasFooter, "REVERSE  |  REAR SENSORS ACTIVE");
      } else if (vs.gear == 'N') {
        lv_label_set_text(drvAdasFooter, "NEUTRAL  |  COASTING");
      } else {
        lv_label_set_text_fmt(drvAdasFooter, "ACC %d  |  LKA ON  |  %dm",
                              displayLimit(), (int)vs.leadDistM);
      }
    }
    if (drvNavDistLbl)   lv_label_set_text_fmt(drvNavDistLbl, "%d m", vs.navDistM);
    if (drvNavStreetLbl) lv_label_set_text(drvNavStreetLbl, vs.navStreet);
    if (drvNavEtaLbl) {
      char buf[56];
      snprintf(buf, sizeof(buf), "%d min  |  %.1f km  |  %s", vs.navEtaMin, vs.navRemKm, vs.navDest);
      lv_label_set_text(drvNavEtaLbl, buf);
    }
    if (drvMediaTitle)   lv_label_set_text(drvMediaTitle, vs.mediaTitle);
    if (drvMediaArtist)  lv_label_set_text(drvMediaArtist, vs.mediaArtist);
    if (drvMediaProg)    lv_bar_set_value(drvMediaProg, vs.mediaProgress, LV_ANIM_OFF);
    if (drvSocLbl) {
      int rangeKm = (int)(vs.socPercent * 4.9f);
      lv_label_set_text_fmt(drvSocLbl, LV_SYMBOL_CHARGE " %d%%   |   %d %s",
                            (int)vs.socPercent,
                            vs.useMph ? (int)(rangeKm * 0.621f) : rangeKm,
                            vs.useMph ? "mi" : "km");
    }
    if (drvSocBar) lv_bar_set_value(drvSocBar, (int)vs.socPercent, LV_ANIM_OFF);
    if (drvEffLbl) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%.1f kWh/100km  |  Trip %.1f km", vs.efficiencyKwh, vs.tripKm);
      lv_label_set_text(drvEffLbl, buf);
    }
  } else if (currentPage == PAGE_NAV) {
    if (navBigDistLbl) lv_label_set_text_fmt(navBigDistLbl, "%d m", vs.navDistM);
    if (navBigActionLbl) {
      const char *act[4] = {"CONTINUE STRAIGHT", "TURN RIGHT", "TURN LEFT", "MAKE U-TURN"};
      lv_label_set_text(navBigActionLbl, act[vs.navDir]);
    }
    if (navBigStreetLbl) lv_label_set_text(navBigStreetLbl, vs.navStreet);
    if (navProgBar)      lv_bar_set_value(navProgBar, vs.navRoutePct, LV_ANIM_OFF);
    if (navEtaDetailLbl) {
      char buf[72];
      snprintf(buf, sizeof(buf), "Dest: %s  |  %d min  |  %.1f km", vs.navDest, vs.navEtaMin, vs.navRemKm);
      lv_label_set_text(navEtaDetailLbl, buf);
    }
    if (navCallStatusLbl) {
      const char *st = vs.callActive ? (vs.callAnswered ? "CALL IN PROGRESS" : "INCOMING CALL...") : "HANDS-FREE CALL CONTROL";
      lv_label_set_text(navCallStatusLbl, st);
      lv_obj_set_style_text_color(navCallStatusLbl, lv_color_hex(vs.callActive ? T().green : T().cyan), 0);
    }
    if (navCallerLbl) lv_label_set_text(navCallerLbl, vs.callActive ? vs.callerName : "Ready for Phone Calls");
    if (navPhoneBattLbl) {
      if (vs.phoneBattery >= 0) lv_label_set_text_fmt(navPhoneBattLbl, "Phone: %d%%", vs.phoneBattery);
      else lv_label_set_text(navPhoneBattLbl, "Phone: --");
    }
    if (navNotifLbl) lv_label_set_text(navNotifLbl, vs.notifText);
  } else if (currentPage == PAGE_MEDIA) {
    if (medTitleLbl)  lv_label_set_text(medTitleLbl, vs.mediaTitle);
    if (medArtistLbl) lv_label_set_text(medArtistLbl, vs.mediaArtist);
    if (medProgSlider && !lv_slider_is_dragged(medProgSlider)) {
      lv_slider_set_value(medProgSlider, vs.mediaProgress, LV_ANIM_OFF);
    }
    if (medVolLbl) {
      lv_label_set_text_fmt(medVolLbl, "%s  %d%%",
                            vs.mediaMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX,
                            vs.mediaMuted ? 0 : vs.mediaVolume);
    }
    if (medTimeLbl) {
      int sec = (vs.mediaProgress * 230) / 100;
      lv_label_set_text_fmt(medTimeLbl, "%02d:%02d / 03:50", sec / 60, sec % 60);
    }
  } else if (currentPage == PAGE_VEHICLE) {
    if (vehHvStatsLbl) {
      char buf[96];
      snprintf(buf, sizeof(buf), "Pack: %.1f V   |   Temp: %.0f C   |   SOH: 99%%\nOdometer: %.1f km   |   Trip A: %.1f km",
               vs.hvVoltage, vs.hvTempC, vs.odoKm, vs.tripKm);
      lv_label_set_text(vehHvStatsLbl, buf);
    }
  } else if (currentPage == PAGE_CONNECT) {
    if (conWifiStatusLbl) {
      lv_label_set_text(conWifiStatusLbl, wifiOk ? LV_SYMBOL_WIFI "  WI-FI CONNECTED" : LV_SYMBOL_WIFI "  WI-FI DISCONNECTED");
      lv_obj_set_style_text_color(conWifiStatusLbl, lv_color_hex(wifiOk ? T().green : T().amber), 0);
    }
    if (conWifiDetailLbl) {
      if (wifiOk) {
        String det = WiFi.SSID() + "  |  " + WiFi.localIP().toString() + "  |  " + String(WiFi.RSSI()) + " dBm";
        lv_label_set_text(conWifiDetailLbl, det.c_str());
      } else {
        lv_label_set_text(conWifiDetailLbl, "Tap CONNECT ANSHIK or SCAN 2.4G");
      }
    }
    if (conBleStatusLbl) {
      lv_label_set_text(conBleStatusLbl, bleConnected ? LV_SYMBOL_BLUETOOTH "  PHONE CONNECTED" : LV_SYMBOL_BLUETOOTH "  ADVERTISING: Visteon-Lunet");
      lv_obj_set_style_text_color(conBleStatusLbl, lv_color_hex(bleConnected ? T().green : T().cyan), 0);
    }
    if (conBlePhoneLbl) {
      if (vs.phoneBattery >= 0) lv_label_set_text_fmt(conBlePhoneLbl, "MTU 247  |  Phone Batt: %d%%", vs.phoneBattery);
      else lv_label_set_text(conBlePhoneLbl, "MTU 247  |  Phone Batt: Waiting for App");
    }
    if (conBleLogLbl) {
      String logAll;
      for (int i = 0; i < bleLogCount; ++i) { logAll += bleLogLines[i]; logAll += "\n"; }
      lv_label_set_text(conBleLogLbl, logAll.c_str());
    }
  } else if (currentPage == PAGE_SYSTEM) {
    if (sysMemLbl) {
      char buf[148];
      snprintf(buf, sizeof(buf),
               "CPU: 240 MHz Dual-Core LX7  |  ST7262 800x480\nInternal Heap: %lu KB  |  OPI PSRAM: %lu KB\nUptime: %lu s  |  GT911 Touch: %s",
               (unsigned long)(ESP.getFreeHeap() / 1024),
               (unsigned long)(ESP.getFreePsram() / 1024),
               (unsigned long)(now / 1000),
               touchReady ? "ACTIVE" : "OFFLINE");
      lv_label_set_text(sysMemLbl, buf);
    }
    if (sysTouchLbl && sysTouchDot) {
      lv_label_set_text_fmt(sysTouchLbl, "Last Touch: X=%d, Y=%d", vs.lastTouchX, vs.lastTouchY);
      int dx = map(vs.lastTouchX, 0, 800, 10, 274);
      int dy = map(vs.lastTouchY, 0, 480, 28, 106);
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

  WiFi.mode(WIFI_STA);
  startBle();

  initOverspeedOverlay();
  buildShell();
  showBootSplash();
  Serial.println("[VISTEON LUNET OS 3.0] Online (Park Mode).");
}

void loop() {
  const uint32_t now = millis();
  lv_timer_handler();

  // Drain all queued BLE RX packets safely on Core 1
  char rxBuf[BLE_MSG_MAX];
  while (dequeueBleRx(rxBuf, sizeof(rxBuf))) {
    processBleCommand(rxBuf);
  }

  // Request full initial sync ~650ms after companion app connects
  if (bleSyncReqPending && bleConnected && (now - bleConnectedAt >= 650)) {
    bleSyncReqPending = false;
    sendBlePacket("CMD:SYNC:REQ");
  }

  // Non-blocking BLE advertising auto-recovery after disconnect
  if (bleRestartPending && !bleConnected && (now - bleDisconnectAt >= 800)) {
    BLEDevice::startAdvertising();
    bleRestartPending = false;
    appendBleLog("SYS", "Auto-restarted advertising");
  }

  updateWifiScanResults();

  // 30 Hz Physics, Overspeed Flash & Vector ADAS Animations
  if (now - lastPhysicsMs >= 33) {
    lastPhysicsMs = now;
    stepPhysicsAndAnimations(now);
  }

  // 4 Hz Status Bar, Telemetry & Text Refreshes
  if (now - lastSlowUiMs >= 250) {
    lastSlowUiMs = now;
    updateSlowUi(now);
  }

  // 2 Hz Telemetry Chart Feed (on System page)
  if (currentPage == PAGE_SYSTEM && sysChart && (now - lastChartMs >= 500)) {
    lastChartMs = now;
    lv_chart_set_next_value(sysChart, serSpeed, (lv_coord_t)vs.speedKmh);
    lv_chart_set_next_value(sysChart, serPower, (lv_coord_t)(vs.powerKw + 60));
  }

  delay(5);
}