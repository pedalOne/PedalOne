#include <Arduino.h>
#include <Arduino_DataBus.h>
#include <canvas/Arduino_Canvas.h>
#include <databus/Arduino_ESP32QSPI.h>
#include <display/Arduino_CO5300.h>
#include <Adafruit_GFX.h>  // Supplies the native-resolution FreeSans font assets.
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <TouchDrvCST92xx.h>
#include <Wire.h>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <FFat.h>
#include <Preferences.h>
#include "recovered_rides_2_0_21.h"
#include <math.h>
#include <time.h>

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

#include "Arial_Bold72pt7b.h"
#include "Arial_Bold92pt7b.h"
#include "ancs_client.h"
#include "onboard_gps.h"
#include "ota_update.h"
#include "wifi_config.h"
#include "pin_config.h"
#include "gpx_navigation.h"
#include "device_icon.h"
#include "gpx_library.h"
#include "shared_map.h"
#include "pq_map.h"
#include "rider_network.h"
#include "radar_client.h"
#ifndef PEDALONE_GPX_DEMO_BOOT
#define PEDALONE_GPX_DEMO_BOOT 0
#endif

// PedalOne bike computer firmware for the ESP32-S3-Touch-AMOLED-1.75.
// Touch: swipe left/right to rotate, tap to click. BOOT long-press replaces
// the M5Dial long-press action.

constexpr int SCREEN_SIZE = 466;
constexpr int CENTER = SCREEN_SIZE / 2;
constexpr float UI_SCALE = SCREEN_SIZE / 240.0f;
constexpr int RADAR_PAGE_INDEX = gpx::PAGE_INDEX + 1;
constexpr int RIDAR_PAGE_INDEX = gpx::PAGE_INDEX + 2;
constexpr int MAX_RIDE_PAGE_COUNT = 6;
constexpr int BOOT_BUTTON_PIN = 0;
constexpr uint32_t LONG_PRESS_MS = 1500;
constexpr int SWIPE_THRESHOLD = 40;
constexpr int SWIPE_VERTICAL_LIMIT = 140;
constexpr uint32_t TOUCH_RELEASE_MS = 90;
constexpr uint32_t TOUCH_POLL_MS = 15;
constexpr uint32_t MENU_TOUCH_LOCKOUT_MS = 350;
constexpr uint32_t PAGE_TOUCH_LOCKOUT_MS = 250;
constexpr uint32_t COUNTDOWN_CANCEL_TOUCH_LOCKOUT_MS = 750;
constexpr uint8_t IO_EXPANDER_ADDRESS = 0x20;
constexpr uint8_t IO_EXPANDER_INPUT_REGISTER = 0x00;
constexpr uint8_t POWER_BUTTON_MASK = 0x10;  // TCA9554 P4 / EXIO4 / SYS_OUT.
constexpr uint32_t POWER_BUTTON_POLL_MS = 25;
// The AXP2101 hardware-off default is four seconds. Start preparing GPS after
// a deliberate hold, leaving ample time for Quectel's required one-second
// backup-mode settling interval before the PMIC removes VCC.
constexpr uint32_t POWER_BUTTON_GPS_BACKUP_HOLD_MS = 750;
#ifndef PEDALONE_POWER_TEST
#define PEDALONE_POWER_TEST 0
#endif
constexpr uint32_t AUTO_POWER_OFF_MS = (PEDALONE_POWER_TEST ? 1UL : 5UL) * 60UL * 1000UL;
constexpr uint32_t HARD_POWER_OFF_MS = (PEDALONE_POWER_TEST ? 5UL : 90UL) * 60UL * 1000UL;
constexpr uint32_t OFF_COURSE_FLASH_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t AUTO_PAUSE_STATIONARY_MS = 3UL * 60UL * 1000UL;
constexpr uint32_t AUTO_RESUME_CONFIRM_MS = 2000;
constexpr float AUTO_PAUSE_MAX_SPEED_MPH = 0.5f;
constexpr float AUTO_RESUME_MIN_SPEED_MPH = 1.5f;
constexpr float AUTO_SAVE_RIDE_MIN_MILES = 2.0f;
// The ten-minute inactive transition already happened when either sleep tier
// starts, so this timer covers only the remaining time to true power-off.
constexpr uint64_t HARD_POWER_OFF_REMAINING_US =
    uint64_t(HARD_POWER_OFF_MS - AUTO_POWER_OFF_MS) * 1000ULL;
constexpr uint64_t HARD_POWER_OFF_MANUAL_US =
    uint64_t(HARD_POWER_OFF_MS) * 1000ULL;

constexpr uint16_t BLACK = RGB565_BLACK;
constexpr uint16_t WHITE = RGB565_WHITE;
constexpr uint16_t RED = RGB565_RED;
constexpr uint16_t YELLOW = RGB565_YELLOW;
constexpr uint16_t ORANGE = 0xFD20;
constexpr uint16_t BABY_BLUE = 0x8E7E;  // #89CFF0
constexpr uint16_t MENU_PINK = 0xFC79;  // #FF8FCB
// Pure neon green sampled from the reference text (#00F900).
constexpr uint16_t NEON_GREEN = 0x07C0;
constexpr uint16_t GREEN = NEON_GREEN;
constexpr uint16_t CYAN = NEON_GREEN;
constexpr uint16_t VALUE_TEAL = NEON_GREEN;
constexpr uint16_t BLE_CONNECTED_BLUE = 0x867F;
constexpr uint16_t START_GREEN = NEON_GREEN;
constexpr uint16_t SUMMARY_PINK = 0xFBB8;
constexpr uint16_t MUSHROOM_RED = 0xFA64;
constexpr uint16_t MUSHROOM_TAN = 0xDDB0;
constexpr uint16_t LOGO_LIME = 0xB7E0;  // #B7FF00
constexpr uint16_t LOGO_CYAN = 0x067F;  // #00CFFF

constexpr char BLE_DEVICE_NAME[] = "PedalOne";
constexpr char LEGACY_BLE_DEVICE_NAME[] = "RAC9000";
constexpr char RADAR_DEVICE_NAME[] = "HLK-LD2451_2F42";
constexpr char FW_VERSION[] = "2.1.119";
constexpr uint32_t CPU_IDLE_MHZ = 80;
constexpr uint32_t CPU_BOOST_MHZ = 160;
constexpr uint32_t CPU_TOUCH_BOOST_MS = 1500;
constexpr char BLE_SERVICE_UUID[] = "8E400001-F315-4F60-9FB8-838830DAEA50";
constexpr char BLE_LOCATION_UUID[] = "8E400002-F315-4F60-9FB8-838830DAEA50";
constexpr char BLE_STATUS_UUID[] = "8E400003-F315-4F60-9FB8-838830DAEA50";
constexpr char BLE_DEVICE_NAME_UUID[] = "8E400006-F315-4F60-9FB8-838830DAEA50";
constexpr size_t BLE_DEVICE_NAME_MAX_BYTES = 24;
constexpr size_t BLE_DEVICE_EMOJI_MAX_BYTES = 16;
constexpr uint32_t LOCATION_TIMEOUT_MS = 4000;
constexpr uint32_t GPS_STATUS_TIMEOUT_MS = 30000;
constexpr uint32_t NAV_TIMEOUT_MS = 20000;
constexpr uint16_t MAX_ALTITUDE_SAMPLES = 720;  // Up to 12 hours at 1/minute.
constexpr uint16_t MAX_GPS_SAMPLES = 4320;      // Up to 12 hours at 1/10 seconds.
constexpr uint8_t MAX_SAVED_RIDES = 5;
constexpr uint32_t RIDE_FILE_MAGIC = 0x52494445; // "RIDE"
constexpr uint32_t ODOMETER_MAGIC = 0x4F444F31; // "ODO1"
constexpr char ACTIVE_GPS_PATH[] = "/active.gps";

#pragma pack(push, 1)
struct LocationPacket {
  uint8_t version;
  uint16_t sequence;
  uint32_t timestamp;
  int32_t latitudeE7;
  int32_t longitudeE7;
  int16_t altitudeDm;
  uint16_t speedCms;
  uint16_t courseDeg100;
  uint16_t accuracyCm;
  uint8_t flags;
  int32_t barometricRelativeAltitudeCm;
};

struct GpsTrackPoint {
  uint32_t elapsedSeconds;
  int32_t latitudeE7;
  int32_t longitudeE7;
  int16_t altitudeDm;
  uint16_t speedCms;
};

struct RideFileHeader {
  uint32_t magic;
  uint16_t formatVersion;
  uint16_t minuteCount;
  uint16_t gpsCount;
  uint16_t reserved;
  uint32_t startEpoch;
  uint32_t durationSeconds;
  float distanceMiles;
  float averageMph;
  int32_t climbFeet;
};

struct SavedRideMeta {
  char path[20];
  uint32_t startEpoch;
  uint32_t durationSeconds;
  float distanceMiles;
};

struct OdometerRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  double odometerMiles;
  double tripAMiles;
  double tripBMiles;
};
#pragma pack(pop)
constexpr size_t LOCATION_PACKET_V1_SIZE = 24;
static_assert(sizeof(LocationPacket) == 28, "Location packet v2 must be 28 bytes");

enum AppState { READY, MENU, OPTIONS_MENU, RIDES_LIST, ANCS_TEST, RIDE_MENU, STATUS_PAGE,
                DISPLAY_PAGE, COUNTDOWN, RIDING, SUMMARY, CONFIRM_PAGE,
                SAVE_RIDE_PROMPT, GPX_DEMO, ROUTES_LIST, START_ROUTE_MENU,
                BLUETOOTH_PAGE, RADAR_SETUP, RADAR_SETTINGS, RADAR_PREVIEW,
                RIDE_CANCELED };
enum Gesture { GESTURE_NONE, GESTURE_TAP, GESTURE_LEFT, GESTURE_RIGHT,
               GESTURE_UP, GESTURE_POWER_OFF, GESTURE_ODOMETER_RESET,
               GESTURE_START_DEMO };
enum PendingAction { ACTION_NONE, ACTION_END_RIDE, ACTION_POWER_OFF,
                     ACTION_RESET_TRIP_A, ACTION_RESET_TRIP_B,
                     ACTION_RESET_ODOMETER, ACTION_DELETE_RIDE };
enum Maneuver {
  MANEUVER_GENERIC, MANEUVER_LEFT, MANEUVER_RIGHT, MANEUVER_SLIGHT_LEFT,
  MANEUVER_SLIGHT_RIGHT, MANEUVER_SHARP_LEFT, MANEUVER_SHARP_RIGHT,
  MANEUVER_STRAIGHT, MANEUVER_UTURN, MANEUVER_ROUNDABOUT, MANEUVER_ARRIVE
};

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *display = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// CO5300 hardware rotation only mirrors axes. Rotate the framebuffer in
// software so rotation 1 is a true 90-degree clockwise transform.
Arduino_Canvas *canvas = new Arduino_Canvas(
    SCREEN_SIZE, SCREEN_SIZE, display, 0, 0, 1);
TouchDrvCST92xx touch;
XPowersPMU power;
AncsClient notifications;
Preferences odometerPreferences;
Preferences devicePreferences;
OtaUpdate otaUpdate;
WifiConfig wifiConfig;
DeviceIconStore deviceIcon;
RiderNetwork riderNetwork;
RadarClient radarClient;
bool otaLinkFast = false;

AppState appState = READY;
AppState statusReturnState = MENU;
AppState displayReturnState = RIDE_MENU;
AppState confirmReturnState = MENU;
AppState radarSettingsReturnState = OPTIONS_MENU;
PendingAction pendingAction = ACTION_NONE;
gpx::Simulation gpxSimulation;
constexpr uint32_t PQ_LOOP_ROUTE_ID = 0x25acf895u;
bool demoRideActive = false;
bool demoStartHoldActive = false;
uint32_t demoStartHoldMs = 0;
uint32_t demoPreviousRouteId = 0;
float demoRouteMeters = 0.0f;
float demoTargetSpeedMph = 14.0f;
uint32_t demoNextSpeedTargetMs = 0;
uint32_t demoRandomState = 0x50454441u;
GpxLibrary gpxLibrary;
SharedMapLibrary sharedMap;
int selectedRouteRow=0;
bool routeSelectionForStart=false, routeNavigationEnabled=false;
int currentPage = 0;
int menuSelection = 0;
uint32_t previousDrawMs = 0;
uint32_t lastFramebufferHash = 0;
bool hasFramebufferHash = false;
bool suppressFrameFlush = false;
bool suppressedFrameCompleted = false;
uint32_t countdownStartMs = 0;
uint32_t rideStartMs = 0;
uint32_t ridePreviousMs = 0;
uint32_t ridePausedAtMs = 0;
bool ridePaused = false;
bool rideAutoPaused = false;
uint32_t autoPauseStationarySinceMs = 0;
uint32_t autoResumeMovingSinceMs = 0;
uint32_t autoResumeLastLocationMs = 0;
uint32_t stationarySinceMs = 0;
uint32_t zeroSpeedSinceMs = 0;
uint32_t lastActivityMs = 0;
uint32_t cpuBoostUntilMs = 0;
uint32_t activeCpuMhz = CPU_BOOST_MHZ;
esp_sleep_wakeup_cause_t bootWakeCause = ESP_SLEEP_WAKEUP_UNDEFINED;
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
uint8_t normalBrightness = 196;
bool displayAutoDimmed = false;
float currentSpeedMph = 0.0f;
float gpsSpeedTargetMph = 0.0f;
float gpsSpeedCandidateMph = 0.0f;
uint16_t gpsSpeedLastSequence = 0;
uint8_t gpsSpeedCandidateCount = 0;
bool gpsSpeedLocked = false;
bool gpsSpeedHadFreshFix = false;
uint32_t gpsSpeedCandidateStartMs = 0;
uint32_t gpsSpeedLastSampleMs = 0;
uint32_t gpsSpeedLastValidMs = 0;
uint32_t gpsSpeedFilterMs = 0;
float rideMaxSpeedMph = 0.0f;
float distanceMiles = 0.0f;
float averageSpeedMph = 0.0f;
float climbFeet = 0.0f;
float filteredElevationFt = 0.0f;
float climbAnchorFt = 0.0f;
bool altitudeFusionInitialized = false;
bool barometerInitialized = false;
bool gpsAltitudeInitialized = false;
float barometerBaselineFt = 0.0f;
float gpsAltitudeBaselineFt = 0.0f;
float lastRawBarometerFt = 0.0f;
float lastRawGpsAltitudeFt = 0.0f;
float filteredBarometerRelativeFt = 0.0f;
float filteredGpsRelativeFt = 0.0f;
float gpsAltitudeDriftCorrectionFt = 0.0f;
uint32_t lastAltitudePacketMs = 0;

void resetGpsSpeedFilter(uint32_t now) {
  currentSpeedMph = 0.0f;
  gpsSpeedTargetMph = 0.0f;
  gpsSpeedCandidateMph = 0.0f;
  gpsSpeedLastSequence = 0;
  gpsSpeedCandidateCount = 0;
  gpsSpeedLocked = false;
  gpsSpeedHadFreshFix = false;
  gpsSpeedCandidateStartMs = 0;
  gpsSpeedLastSampleMs = 0;
  gpsSpeedLastValidMs = 0;
  gpsSpeedFilterMs = now;
}
constexpr float GPS_ALTITUDE_CORRECTION_TAU_SECONDS = 120.0f;
constexpr float BAROMETER_FILTER_TAU_SECONDS = 2.3f;
constexpr float GPS_ALTITUDE_FILTER_TAU_SECONDS = 6.2f;
constexpr float CLIMB_DEADBAND_FT = 6.0f;
struct GradeSample {
  float distanceFt;
  float elevationFt;
  uint32_t timeMs;
};
constexpr uint8_t GRADE_SAMPLE_CAPACITY = 32;
constexpr uint32_t GRADE_STABLE_WINDOW_MS = 15000;
constexpr uint32_t GRADE_FAST_WINDOW_MS = 5000;
GradeSample gradeSamples[GRADE_SAMPLE_CAPACITY] = {};
uint8_t gradeSampleHead = 0;
uint8_t gradeSampleCount = 0;
uint32_t lastGradeSampleMs = 0;
float liveGradePercent = 0.0f;
bool liveGradeValid = false;
bool gradeMotionActive = false;
float altitudeSamples[MAX_ALTITUDE_SAMPLES] = {};
float speedSamples[MAX_ALTITUDE_SAMPLES] = {};
uint16_t altitudeSampleCount = 0;
uint32_t nextAltitudeSampleMs = 0;
uint32_t minuteAccumulatorMs = 0;
uint32_t minuteWeightedMs = 0;
float altitudeMinuteWeightedSum = 0.0f;
float speedMinuteWeightedSum = 0.0f;
File activeGpsFile;
uint16_t gpsSampleCount = 0;
uint32_t nextGpsSampleMs = 0;
uint8_t gpsSamplesSinceFlush = 0;
uint32_t rideStartEpoch = 0;
LocationPacket latestLocation = {};
portMUX_TYPE locationMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool hasLocation = false;
volatile uint32_t lastLocationMs = 0;
OnboardGps onboardGps;
bool onboardGpsPresent = false;
bool physicalPowerButtonDown = false;
bool gpsBackupPreparedForPowerButton = false;
bool gpsBackupAttemptedForPowerButton = false;
bool rideAutoSavedForPowerButton = false;
uint32_t physicalPowerButtonDownMs = 0;
uint32_t lastPowerButtonPollMs = 0;
uint16_t lastOnboardGpsSequence = 0;
bool bleBarometerAvailable = false;
uint32_t lastBleBarometerMs = 0;
int32_t latestBleBarometerCm = 0;
uint32_t latestBleTimestamp = 0;
uint32_t lastBleTimestampMs = 0;
bool phoneConnected = false;
portMUX_TYPE navMux = portMUX_INITIALIZER_UNLOCKED;
bool navVisible = false;
uint32_t navUID = 0;
uint32_t navReceivedMs = 0;
Maneuver navManeuver = MANEUVER_GENERIC;
char navTitle[128] = {};
char navMessage[256] = {};
char navDistance[32] = {};
constexpr uint8_t ANCS_HISTORY_SIZE = 10;
struct AncsHistoryItem {
  uint32_t uid = 0;
  uint32_t receivedMs = 0;
  uint8_t category = 0;
  Maneuver maneuver = MANEUVER_GENERIC;
  char app[96] = {};
  char title[128] = {};
  char message[256] = {};
  char distance[32] = {};
};
AncsHistoryItem ancsHistory[ANCS_HISTORY_SIZE] = {};
uint8_t ancsHistoryCount = 0;
uint8_t ancsHistoryPage = 0;
volatile uint32_t ancsDeliveredCount = 0;
volatile uint32_t ancsRemovedCount = 0;
bool pmicAvailable = false;
bool batteryConnected = false;
bool batteryCharging = false;
uint8_t batteryPercent = 0;
uint32_t lastBatteryReadMs = 0;
bool bluetoothEnabled=true;
bool ridarEnabled=false;
bool radarEnabled=false;
RadarClient::Settings radarSettings;
bool radarSettingsDirty=false;
BLECharacteristic *statusCharacteristic = nullptr;
BLECharacteristic *deviceNameCharacteristic = nullptr;
String deviceNickname = BLE_DEVICE_NAME;
String deviceEmoji;
bool deviceStorageReady = false;
uint32_t summaryRideSeconds = 0;
float summaryDistanceMiles = 0.0f;
float summaryAverageMph = 0.0f;
int summaryClimbFeet = 0;
uint8_t summaryPage = 0;
uint8_t statusPage = 0;
bool odometerResetArmed = false;
double odometerMiles = 0.0;
double tripAMiles = 0.0;
double tripBMiles = 0.0;
double odometerPendingMiles = 0.0;
double tripAPendingMiles = 0.0;
double tripBPendingMiles = 0.0;
bool odometerStorageReady = false;
bool odometerRecordInvalid = false;
SavedRideMeta savedRides[MAX_SAVED_RIDES] = {};
uint8_t savedRideCount = 0;
bool rideStorageReady = false;
bool viewingSavedRide = false;
char summaryGpsPath[20] = {};
size_t summaryGpsOffset = 0;
struct RouteScreenPoint { int16_t x; int16_t y; };
RouteScreenPoint summaryRoutePoints[MAX_GPS_SAMPLES] = {};
uint16_t summaryRoutePointCount = 0;
bool summaryRouteLoaded = false;
bool ridesEditMode = false;
int8_t pendingRideDeleteIndex = -1;

volatile bool touchPending = false;
bool touchActive = false;
bool touchCancelConsumed = false;
bool touchAdjustedBrightness = false;
int16_t touchStartX = 0, touchStartY = 0, touchLastX = 0, touchLastY = 0;
uint32_t touchStartMs = 0, touchLastMs = 0;
bool powerSliderActive = false;
int16_t powerSliderX = 0;
int16_t powerSliderStartX = 0;
bool odometerResetSliderActive = false;
int16_t odometerResetSliderX = 0;
int16_t odometerResetSliderStartX = 0;
uint32_t lastTouchPollMs = 0;
uint32_t ignoreTouchUntilMs = 0;
bool touchBlockedUntilRelease = false;
int16_t lastTapX = 0, lastTapY = 0;
bool buttonWasDown = false;
bool buttonLongHandled = false;
uint32_t buttonDownMs = 0;

int us(float value) { return int(roundf(value * UI_SCALE)); }
int ux(int x) { return us(x); }
int uy(int y) { return us(y); }

uint32_t nonNegativeElapsedMs(uint32_t now, uint32_t timestamp) {
  const int32_t elapsed = int32_t(now - timestamp);
  return elapsed < 0 ? 0u : uint32_t(elapsed);
}

bool timestampIsFresh(uint32_t now, uint32_t timestamp, uint32_t timeoutMs) {
  return nonNegativeElapsedMs(now, timestamp) <= timeoutMs;
}

void IRAM_ATTR onTouchInterrupt() { touchPending = true; }

void updateDynamicCpuClock(uint32_t now) {
  // The CST9217 interrupt reaches us while the CPU is still at its low idle
  // clock. Raise it before reading I2C or drawing the response, then retain the
  // boost long enough to finish a swipe and render the destination screen.
  if (touchPending || touchActive || !previousDrawMs)
    cpuBoostUntilMs = now + CPU_TOUCH_BOOST_MS;
  const bool busy = int32_t(cpuBoostUntilMs - now) > 0 ||
      appState == COUNTDOWN || appState == GPX_DEMO ||
      otaUpdate.active() || sharedMap.active() || gpxLibrary.active();
  const uint32_t targetMhz = busy ? CPU_BOOST_MHZ : CPU_IDLE_MHZ;
  if (targetMhz != activeCpuMhz) {
    setCpuFrequencyMhz(targetMhz);
    activeCpuMhz = targetMhz;
  }
}

void boostCpuForMapRender() {
  // Map rasterization and the full-frame QSPI transfer are latency-sensitive.
  // Run only that burst at full speed, then let the next loop iteration return
  // to the 80 MHz baseline unless touch or another heavyweight task is active.
  if (activeCpuMhz != CPU_BOOST_MHZ) {
    setCpuFrequencyMhz(CPU_BOOST_MHZ);
    activeCpuMhz = CPU_BOOST_MHZ;
  }
}

class LocationCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    if (value.length() != LOCATION_PACKET_V1_SIZE &&
        value.length() != sizeof(LocationPacket)) return;
    LocationPacket packet = {};
    memcpy(&packet, value.c_str(), value.length());
    if ((packet.version == 1 && value.length() != LOCATION_PACKET_V1_SIZE) ||
        (packet.version == 2 && value.length() != sizeof(LocationPacket)) ||
        (packet.version != 1 && packet.version != 2)) return;
    const uint32_t receivedMs = millis();
    portENTER_CRITICAL(&locationMux);
    if (onboardGpsPresent) {
      // Keep the phone's barometer as an elevation aid, but do not let phone
      // coordinates or speed replace a detected onboard receiver.
      if (packet.timestamp) {
        latestBleTimestamp = packet.timestamp;
        lastBleTimestampMs = receivedMs;
      }
      if (packet.version >= 2 && (packet.flags & (1 << 3))) {
        latestBleBarometerCm = packet.barometricRelativeAltitudeCm;
        lastBleBarometerMs = receivedMs;
        bleBarometerAvailable = true;
      }
    } else {
      latestLocation = packet;
      lastLocationMs = receivedMs;
      hasLocation = true;
    }
    portEXIT_CRITICAL(&locationMux);
  }
};

// Accept printable UTF-8 while rejecting control characters, malformed byte
// sequences, surrogate code points, and values longer than the BLE contract.
bool validDeviceNickname(const String &value) {
  const size_t length = value.length();
  if (!length || length > BLE_DEVICE_NAME_MAX_BYTES) return false;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(value.c_str());
  for (size_t i = 0; i < length;) {
    const uint8_t first = bytes[i];
    if (first < 0x80) {
      if (first < 0x20 || first == 0x7f) return false;
      ++i;
      continue;
    }
    size_t continuationCount = 0;
    uint32_t codePoint = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      continuationCount = 1;
      codePoint = first & 0x1f;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuationCount = 2;
      codePoint = first & 0x0f;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuationCount = 3;
      codePoint = first & 0x07;
    } else {
      return false;
    }
    if (i + continuationCount >= length) return false;
    for (size_t offset = 1; offset <= continuationCount; ++offset) {
      const uint8_t next = bytes[i + offset];
      if ((next & 0xc0) != 0x80) return false;
      codePoint = (codePoint << 6) | (next & 0x3f);
    }
    if ((continuationCount == 2 && codePoint < 0x800) ||
        (continuationCount == 3 && codePoint < 0x10000) ||
        (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff)
      return false;
    i += continuationCount + 1;
  }
  return true;
}

void publishDeviceNickname() {
  if (!deviceNameCharacteristic) return;
  deviceNameCharacteristic->setValue(deviceNickname);
  if (phoneConnected) deviceNameCharacteristic->notify();
}

void applyDeviceNickname(const String &nickname, bool persist) {
  deviceNickname = nickname;
  if (persist && deviceStorageReady) {
    if (deviceNickname == BLE_DEVICE_NAME) devicePreferences.remove("nickname");
    else devicePreferences.putString("nickname", deviceNickname);
  }
  notifications.setDeviceName(deviceNickname);
  publishDeviceNickname();
}

bool validDeviceEmoji(const String &value);
void applyDeviceEmoji(const String &emoji, bool persist);

class DeviceNameCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    String requested = characteristic->getValue();
    // Compatibility transport for phones whose bonded GATT cache predates the
    // dedicated emoji characteristic. The leading control byte makes older
    // firmware reject this as an invalid nickname instead of renaming itself.
    if(requested.length()>=7 && uint8_t(requested[0])==0x1e &&
       requested.substring(1,7)=="EMOJI:") {
      const String emoji=requested.substring(7);
      if(!emoji.length()) {
        applyDeviceEmoji("",true);
        Serial.println("Device emoji cleared via name compatibility path");
      } else if(validDeviceEmoji(emoji)) {
        applyDeviceEmoji(emoji,true);
        Serial.printf("Device emoji saved via name compatibility path (%u bytes)\n",
                      unsigned(emoji.length()));
      } else {
        Serial.println("Rejected malformed device emoji compatibility write");
      }
      publishDeviceNickname();
      return;
    }
    // An empty write restores the factory nickname. Non-empty input is used
    // byte-for-byte so user-visible names do not change unexpectedly.
    if (!requested.length()) {
      applyDeviceNickname(BLE_DEVICE_NAME, true);
      return;
    }
    if (!validDeviceNickname(requested)) {
      // Re-publish the accepted value so the app can immediately reconcile a
      // rejected malformed, control-containing, or over-length write.
      publishDeviceNickname();
      return;
    }
    applyDeviceNickname(requested, true);
  }
};

bool validDeviceEmoji(const String &value) {
  // The phone validates this as a single grapheme. Firmware repeats the UTF-8
  // structural checks so malformed values cannot be persisted in NVS.
  if (!value.length() || value.length() > BLE_DEVICE_EMOJI_MAX_BYTES) return false;
  return validDeviceNickname(value);
}

void applyDeviceEmoji(const String &emoji, bool persist) {
  deviceEmoji = emoji;
  if (persist && deviceStorageReady) {
    if (deviceEmoji.isEmpty()) devicePreferences.remove("emoji");
    else devicePreferences.putString("emoji", deviceEmoji);
  }
  previousDrawMs = 0;
}

bool containsIgnoreCase(const String &value, const char *needle) {
  String lower = value;
  lower.toLowerCase();
  return lower.indexOf(needle) >= 0;
}

Maneuver parseManeuver(const String &text) {
  if (containsIgnoreCase(text, "arriv") || containsIgnoreCase(text, "destination")) return MANEUVER_ARRIVE;
  if (containsIgnoreCase(text, "roundabout") || containsIgnoreCase(text, "traffic circle")) return MANEUVER_ROUNDABOUT;
  if (containsIgnoreCase(text, "u-turn") || containsIgnoreCase(text, "u turn")) return MANEUVER_UTURN;
  if (containsIgnoreCase(text, "sharp left")) return MANEUVER_SHARP_LEFT;
  if (containsIgnoreCase(text, "sharp right")) return MANEUVER_SHARP_RIGHT;
  if (containsIgnoreCase(text, "slight left") || containsIgnoreCase(text, "keep left") || containsIgnoreCase(text, "bear left")) return MANEUVER_SLIGHT_LEFT;
  if (containsIgnoreCase(text, "slight right") || containsIgnoreCase(text, "keep right") || containsIgnoreCase(text, "bear right") || containsIgnoreCase(text, "exit")) return MANEUVER_SLIGHT_RIGHT;
  if (containsIgnoreCase(text, "left")) return MANEUVER_LEFT;
  if (containsIgnoreCase(text, "right")) return MANEUVER_RIGHT;
  if (containsIgnoreCase(text, "straight") || containsIgnoreCase(text, "continue")) return MANEUVER_STRAIGHT;
  return MANEUVER_GENERIC;
}

String extractDistance(const String &text) {
  String lower = text;
  lower.toLowerCase();
  for (int start = 0; start < int(lower.length()); ++start) {
    if (!isDigit(lower[start])) continue;
    int numberEnd = start;
    while (numberEnd < int(lower.length()) &&
           (isDigit(lower[numberEnd]) || lower[numberEnd] == '.' ||
            lower[numberEnd] == ',')) ++numberEnd;
    int unitStart = numberEnd;
    while (unitStart < int(lower.length()) && lower[unitStart] == ' ') ++unitStart;
    int unitEnd = unitStart;
    while (unitEnd < int(lower.length()) && isAlpha(lower[unitEnd])) ++unitEnd;
    const String unit = lower.substring(unitStart, unitEnd);
    String shortUnit;
    if (unit == "ft" || unit == "foot" || unit == "feet") shortUnit = "FT";
    else if (unit == "mi" || unit == "mile" || unit == "miles") shortUnit = "MI";
    else if (unit == "m" || unit == "meter" || unit == "meters" ||
             unit == "metre" || unit == "metres") shortUnit = "M";
    else if (unit == "km" || unit == "kilometer" || unit == "kilometers" ||
             unit == "kilometre" || unit == "kilometres") shortUnit = "KM";
    else if (unit == "yd" || unit == "yard" || unit == "yards") shortUnit = "YD";
    else continue;
    return "IN " + text.substring(start, numberEnd) + " " + shortUnit;
  }
  return String();
}

void copyNavText(char *destination, size_t size, const String &source) {
  if (!size) return;
  strncpy(destination, source.c_str(), size - 1);
  destination[size - 1] = '\0';
}

int navigationInstructionScore(const String &value) {
  if (!value.length()) return -1000;
  String lower = value;
  lower.toLowerCase();
  int score = min(30, int(value.length()) / 4);
  if (lower.indexOf("turn") >= 0 || lower.indexOf("continue") >= 0 ||
      lower.indexOf("keep ") >= 0 || lower.indexOf("bear ") >= 0 ||
      lower.indexOf("merge") >= 0 || lower.indexOf("exit") >= 0 ||
      lower.indexOf("roundabout") >= 0 || lower.indexOf("arrive") >= 0)
    score += 50;
  if (lower.indexOf(" onto ") >= 0 || lower.indexOf(" on ") >= 0 ||
      lower.indexOf(" toward ") >= 0) score += 35;
  // Prefer the current maneuver title ("Turn ... onto Street") over ANCS
  // body text describing what happens afterward ("Continue toward ...").
  if ((lower.indexOf("turn") >= 0 && lower.indexOf(" onto ") >= 0) ||
      (lower.indexOf("u-turn") >= 0 && lower.indexOf(" at ") >= 0))
    score += 100;
  if (lower.indexOf("continue toward") >= 0) score -= 45;
  if (lower == "google maps" || lower == "maps" || lower == "komoot" ||
      lower == "ride with gps" || lower == "waze" ||
      lower == "navigation") score -= 100;
  const String distance = extractDistance(value);
  if (distance.length() && value.length() <= distance.length() + 6) score -= 60;
  return score;
}

String chooseNavigationInstruction(const AncsNotification &notification) {
  const String candidates[] = {notification.title, notification.subtitle,
                               notification.message};
  int best = 0;
  int bestScore = navigationInstructionScore(candidates[0]);
  for (int i = 1; i < 3; ++i) {
    const int score = navigationInstructionScore(candidates[i]);
    if (score > bestScore) {
      best = i;
      bestScore = score;
    }
  }
  return candidates[best];
}

String navigationStreetName(const char *instruction) {
  String street(instruction ? instruction : "");
  street.replace('\n', ' ');
  street.replace('\r', ' ');
  street.trim();
  if (!street.length()) return String("NEXT TURN");

  String lower = street;
  lower.toLowerCase();
  // The arrow already communicates the maneuver. Prefer the road/destination
  // after the instruction connector so long ANCS prose cannot hide it.
  const char *connectors[] = {" onto ", " toward ", " towards ", " on ",
                              " to stay on ", " at "};
  int connectorPos = -1;
  int connectorLength = 0;
  for (const char *connector : connectors) {
    const int pos = lower.indexOf(connector);
    if (pos >= 0 && (connectorPos < 0 || pos < connectorPos)) {
      connectorPos = pos;
      connectorLength = strlen(connector);
    }
  }
  if (connectorPos >= 0) {
    street = street.substring(connectorPos + connectorLength);
  } else {
    // Strip a leading distance clause such as "In 300 ft, ...".
    const int comma = street.indexOf(',');
    if (comma >= 0 && extractDistance(street.substring(0, comma)).length())
      street = street.substring(comma + 1);
    String command = street;
    command.toLowerCase();
    const char *prefixes[] = {"turn left ", "turn right ", "slight left ",
                              "slight right ", "keep left ", "keep right ",
                              "continue ", "head straight ", "go straight ",
                              "merge ", "take the exit "};
    for (const char *prefix : prefixes) {
      if (command.startsWith(prefix)) {
        street = street.substring(strlen(prefix));
        break;
      }
    }
  }
  street.trim();
  // Do not append the following maneuver to the current road name.
  String streetLower = street;
  streetLower.toLowerCase();
  const char *trailingDirections[] = {" and continue toward", " then continue toward",
                                      " · continue toward", " continue toward"};
  for (const char *suffix : trailingDirections) {
    const int pos = streetLower.indexOf(suffix);
    if (pos > 0) {
      street = street.substring(0, pos);
      street.trim();
      break;
    }
  }
  while (street.endsWith(".") || street.endsWith(","))
    street.remove(street.length() - 1);
  return street.length() ? street : String(instruction);
}

bool isNavigationNotification(const AncsNotification &notification) {
  String app = notification.appId;
  app.toLowerCase();
  // All navigation views use this strict source allowlist, including ANCS
  // Testing, so unrelated notifications can never reach the display.
  if (app.indexOf("com.google.maps") >= 0 ||
      app.indexOf("komoot") >= 0 ||
      app.indexOf("ridewithgps") >= 0 ||
      app.indexOf("com.byobike.pedalone") >= 0)
    return true;
  return false;
}

void onAncsStateChanged(AncsClient::State state) {
  phoneConnected = state != AncsClient::Disconnected;
  if (state == AncsClient::Disconnected) {
    portENTER_CRITICAL(&navMux);
    navVisible = false;
    portEXIT_CRITICAL(&navMux);
  }
  previousDrawMs = 0;
}

void onNavigationNotification(const AncsNotification &notification) {
  ++ancsDeliveredCount;
  if (!isNavigationNotification(notification)) return;
  if (appState == ANCS_TEST) {
    const String combined = notification.title + " " + notification.subtitle +
                            " " + notification.message;
    const String distance = extractDistance(combined);
    const uint8_t oldPage = ancsHistoryPage;
    const bool viewingClearPage = ancsHistoryCount && oldPage >= ancsHistoryCount;
    const uint8_t last = ancsHistoryCount < ANCS_HISTORY_SIZE
        ? ancsHistoryCount : ANCS_HISTORY_SIZE - 1;
    for (uint8_t i = last; i > 0; --i) ancsHistory[i] = ancsHistory[i - 1];
    AncsHistoryItem &item = ancsHistory[0];
    item.uid = notification.uid;
    item.receivedMs = millis();
    item.category = notification.category;
    item.maneuver = parseManeuver(combined);
    copyNavText(item.app, sizeof(item.app), notification.appId);
    copyNavText(item.title, sizeof(item.title), notification.title);
    copyNavText(item.message, sizeof(item.message),
                chooseNavigationInstruction(notification));
    copyNavText(item.distance, sizeof(item.distance), distance);
    if (ancsHistoryCount < ANCS_HISTORY_SIZE) ++ancsHistoryCount;
    if (viewingClearPage) ancsHistoryPage = ancsHistoryCount;
    else if (oldPage > 0)
      ancsHistoryPage = min(uint8_t(oldPage + 1), uint8_t(ancsHistoryCount - 1));
    else ancsHistoryPage = 0;
    previousDrawMs = 0;
    return;
  }
  const String combined = notification.title + " " + notification.subtitle +
                          " " + notification.message;
  const String distance = extractDistance(combined);
  portENTER_CRITICAL(&navMux);
  // Fresh navigation always replaces the visible instruction immediately;
  // retaining an older turn for later would present stale guidance.
  copyNavText(navTitle, sizeof(navTitle), notification.title);
  copyNavText(navMessage, sizeof(navMessage),
              chooseNavigationInstruction(notification));
  copyNavText(navDistance, sizeof(navDistance), distance);
  navManeuver = parseManeuver(combined);
  navUID = notification.uid;
  navReceivedMs = millis();
  navVisible = true;
  portEXIT_CRITICAL(&navMux);
  previousDrawMs = 0;
}

void onNavigationRemoved(uint32_t uid) {
  ++ancsRemovedCount;
  // Navigation apps frequently remove the current ANCS notification while
  // replacing it with the next maneuver.  The ANCS test page intentionally
  // retains its last event, so mirror that behavior during a ride: keep the
  // latest instruction until it is replaced, tapped away, or reaches the
  // normal 20-second navigation timeout.
  (void)uid;
}

void notifyPhone(const char *status) {
  if (!statusCharacteristic) return;
  statusCharacteristic->setValue(status);
  if (phoneConnected) statusCharacteristic->notify();
}

bool otaCanStart() {
  const bool rideActive = appState == COUNTDOWN || appState == RIDING ||
      appState == RIDE_MENU || appState == SUMMARY ||
      appState == SAVE_RIDE_PROMPT ||
      (appState == RADAR_SETTINGS && radarSettingsReturnState == RIDING);
  return !rideActive && (!batteryConnected || batteryCharging || batteryPercent >= 25);
}

void setupBluetooth() {
  notifications.setStateCallback(onAncsStateChanged);
  notifications.setNotificationCallback(onNavigationNotification);
  notifications.setRemovedCallback(onNavigationRemoved);
  // Keep all modes transport-filtered so unrelated notification traffic cannot
  // consume the same BLE link used by the 5 Hz GPS relay.
  notifications.setAcceptAll(false);
  notifications.begin(deviceNickname.c_str());
  BLEService *service = notifications.server()->getServiceByUUID(BLE_SERVICE_UUID);
  BLECharacteristic *location = service->getCharacteristic(BLE_LOCATION_UUID);
  location->setCallbacks(new LocationCallbacks());
  statusCharacteristic = service->getCharacteristic(BLE_STATUS_UUID);
  statusCharacteristic->setValue("ready");
  deviceNameCharacteristic = service->getCharacteristic(BLE_DEVICE_NAME_UUID);
  deviceNameCharacteristic->setCallbacks(new DeviceNameCallbacks());
  deviceNameCharacteristic->setValue(deviceNickname);
  gpxLibrary.begin(service->getCharacteristic(GPX_BLE_UUID), rideStorageReady);
  sharedMap.begin(service->getCharacteristic(SHARED_MAP_BLE_UUID), rideStorageReady);
  deviceIcon.begin(service->getCharacteristic(DEVICE_ICON_BLE_UUID),
                   rideStorageReady);
  otaUpdate.begin(notifications.server(), FW_VERSION, otaCanStart);
  wifiConfig.begin(notifications.server());
  // Advertising must start only after every GATT service is registered. ANCS
  // can auto-connect immediately after boot; advertising earlier allowed iOS
  // to connect while the OTA service was still mutating the GATT database,
  // leaving the GPS characteristic undiscoverable for that connection.
  if (bluetoothEnabled) notifications.startAdvertising();
  else notifications.setEnabled(false);
}

void loadDeviceNickname() {
  deviceStorageReady = devicePreferences.begin("bike_id", false);
  if (!deviceStorageReady) {
    Serial.println("NVS initialization failed; device nickname unavailable");
    return;
  }
  if (devicePreferences.isKey("nickname")) {
    const String stored = devicePreferences.getString("nickname", "");
    // Migrate units that explicitly persisted the former factory name. Custom
    // user nicknames remain untouched.
    if (stored == LEGACY_BLE_DEVICE_NAME) {
      devicePreferences.remove("nickname");
      deviceNickname = BLE_DEVICE_NAME;
    } else if (validDeviceNickname(stored)) {
      deviceNickname = stored;
    } else {
      // Self-heal corrupt or values from incompatible development builds.
      devicePreferences.remove("nickname");
    }
  }
  const String storedEmoji = devicePreferences.getString("emoji", "");
  if (!storedEmoji.length()) return;
  if (validDeviceEmoji(storedEmoji)) deviceEmoji = storedEmoji;
  else devicePreferences.remove("emoji");
}

void loadOdometers() {
  odometerStorageReady = odometerPreferences.begin("bike_odo", false);
  if (!odometerStorageReady) {
    Serial.println("NVS initialization failed; odometers unavailable");
    return;
  }
  OdometerRecord record = {};
  const bool valid = odometerPreferences.getBytesLength("counters") == sizeof(record) &&
      odometerPreferences.getBytes("counters", &record, sizeof(record)) == sizeof(record) &&
      record.magic == ODOMETER_MAGIC && record.version == 1 &&
      isfinite(record.odometerMiles) && isfinite(record.tripAMiles) &&
      isfinite(record.tripBMiles);
  odometerRecordInvalid = odometerPreferences.isKey("counters") && !valid;
  if (valid) {
    odometerMiles = max(0.0, record.odometerMiles);
    tripAMiles = max(0.0, record.tripAMiles);
    tripBMiles = max(0.0, record.tripBMiles);
  } else if (odometerRecordInvalid) {
    Serial.println("Odometer NVS record invalid; preserving it for recovery");
  }
}

void saveOdometers(bool includePending = true) {
  if (!odometerStorageReady || odometerRecordInvalid) return;
  if (!isfinite(odometerMiles) || !isfinite(tripAMiles) ||
      !isfinite(tripBMiles) || !isfinite(odometerPendingMiles) ||
      !isfinite(tripAPendingMiles) || !isfinite(tripBPendingMiles)) {
    Serial.println("Odometer checkpoint skipped: nonfinite counter");
    return;
  }
  if (!odometerPreferences.isKey("counters") &&
      odometerMiles == 0.0 && tripAMiles == 0.0 && tripBMiles == 0.0 &&
      odometerPendingMiles == 0.0) {
    // The first real movement or reset will create the record.
    return;
  }
  if (includePending) {
    odometerMiles += odometerPendingMiles;
    tripAMiles += tripAPendingMiles;
    tripBMiles += tripBPendingMiles;
    odometerPendingMiles = tripAPendingMiles = tripBPendingMiles = 0.0;
  }
  const OdometerRecord record = {
      ODOMETER_MAGIC, 1, 0, odometerMiles, tripAMiles, tripBMiles};
  odometerPreferences.putBytes("counters", &record, sizeof(record));
}

void resetTrip(bool tripA) {
  if (!odometerStorageReady) return;
  saveOdometers();
  if (tripA) tripAMiles = 0.0;
  else tripBMiles = 0.0;
  const OdometerRecord record = {
      ODOMETER_MAGIC, 1, 0, odometerMiles, tripAMiles, tripBMiles};
  odometerPreferences.putBytes("counters", &record, sizeof(record));
}

void resetOdometer() {
  if (!odometerStorageReady) return;
  // Commit pending distance to both trips first, then clear only the lifetime
  // odometer. Trip A and Trip B remain independent counters.
  saveOdometers();
  odometerMiles = 0.0;
  const OdometerRecord record = {
      ODOMETER_MAGIC, 1, 0, odometerMiles, tripAMiles, tripBMiles};
  odometerPreferences.putBytes("counters", &record, sizeof(record));
}

bool readRideHeader(const char *path, RideFileHeader &header) {
  File file = FFat.open(path, FILE_READ);
  if (!file) return false;
  const bool ok = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      header.magic == RIDE_FILE_MAGIC && header.formatVersion == 1 &&
      header.minuteCount <= MAX_ALTITUDE_SAMPLES && header.gpsCount <= MAX_GPS_SAMPLES;
  file.close();
  return ok;
}

void scanSavedRides() {
  savedRideCount = 0;
  if (!rideStorageReady) return;
  for (uint8_t slot = 0; slot < MAX_SAVED_RIDES; ++slot) {
    char path[20];
    snprintf(path, sizeof(path), "/ride%u.dat", slot);
    RideFileHeader header = {};
    if (!readRideHeader(path, header)) continue;
    SavedRideMeta &meta = savedRides[savedRideCount++];
    strncpy(meta.path, path, sizeof(meta.path) - 1);
    meta.startEpoch = header.startEpoch;
    meta.durationSeconds = header.durationSeconds;
    meta.distanceMiles = header.distanceMiles;
  }
  // Newest ride first. Sequence by timestamp; file slot breaks ties when an
  // iPhone timestamp was not available at the start of a ride.
  for (uint8_t i = 0; i < savedRideCount; ++i) {
    for (uint8_t j = i + 1; j < savedRideCount; ++j) {
      const bool newer = savedRides[j].startEpoch > savedRides[i].startEpoch ||
          (savedRides[j].startEpoch == savedRides[i].startEpoch &&
           strcmp(savedRides[j].path, savedRides[i].path) > 0);
      if (newer) {
        const SavedRideMeta swap = savedRides[i];
        savedRides[i] = savedRides[j];
        savedRides[j] = swap;
      }
    }
  }
}

bool writeRecoveredRide(const char *path, const uint8_t *data, size_t length) {
  File file = FFat.open(path, FILE_WRITE);
  if (!file) return false;
  const bool ok = file.write(data, length) == length;
  file.close();
  return ok;
}

bool initializeRideStorage() {
  if (FFat.begin(false, "/ffat", 8, "ffat")) return true;

  // This release carries a one-time recovery copy of the five rides extracted
  // from the raw partition backup. Rebuild the wear-levelling metadata only
  // once; a later unrelated mount failure must never silently erase rides.
  Preferences recoveryState;
  if (!recoveryState.begin("ffat_repair", false)) {
    Serial.println("FFat repair state unavailable; refusing to format");
    return false;
  }
  if (recoveryState.getBool("restored_v1", false)) {
    Serial.println("FFat repair already attempted; refusing to format again");
    recoveryState.end();
    return false;
  }

  Serial.println("Rebuilding FFat wear-levelling metadata");
  FFat.format(FFAT_WIPE_FULL);
  const bool mounted = FFat.begin(false, "/ffat", 8, "ffat");
  if (!mounted) {
    Serial.println("FFat still cannot mount after full format");
    recoveryState.end();
    return false;
  }

  const bool restored =
      writeRecoveredRide("/ride0.dat", recovered_ride0, recovered_ride0_len) &&
      writeRecoveredRide("/ride1.dat", recovered_ride1, recovered_ride1_len) &&
      writeRecoveredRide("/ride2.dat", recovered_ride2, recovered_ride2_len) &&
      writeRecoveredRide("/ride3.dat", recovered_ride3, recovered_ride3_len) &&
      writeRecoveredRide("/ride4.dat", recovered_ride4, recovered_ride4_len);
  if (restored) {
    recoveryState.putBool("restored_v1", true);
    Serial.println("Recovered five saved rides to fresh FFat storage");
  } else {
    Serial.println("One or more recovered ride files could not be written");
  }
  recoveryState.end();
  return restored;
}

void saveCurrentRide() {
  if (!rideStorageReady) return;
  if (activeGpsFile) {
    activeGpsFile.flush();
    activeGpsFile.close();
  }
  char path[20] = {};
  if (savedRideCount >= MAX_SAVED_RIDES) {
    strncpy(path, savedRides[savedRideCount - 1].path, sizeof(path) - 1);
    FFat.remove(path);
  } else {
    for (uint8_t slot = 0; slot < MAX_SAVED_RIDES; ++slot) {
      snprintf(path, sizeof(path), "/ride%u.dat", slot);
      if (!FFat.exists(path)) break;
    }
  }

  RideFileHeader header = {};
  header.magic = RIDE_FILE_MAGIC;
  header.formatVersion = 1;
  header.minuteCount = altitudeSampleCount;
  header.gpsCount = gpsSampleCount;
  header.startEpoch = rideStartEpoch;
  header.durationSeconds = summaryRideSeconds;
  header.distanceMiles = summaryDistanceMiles;
  header.averageMph = summaryAverageMph;
  header.climbFeet = summaryClimbFeet;

  File file = FFat.open(path, FILE_WRITE);
  if (!file) return;
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&header), sizeof(header)) == sizeof(header);
  ok = ok && file.write(reinterpret_cast<const uint8_t *>(altitudeSamples),
                        altitudeSampleCount * sizeof(float)) == altitudeSampleCount * sizeof(float);
  ok = ok && file.write(reinterpret_cast<const uint8_t *>(speedSamples),
                        altitudeSampleCount * sizeof(float)) == altitudeSampleCount * sizeof(float);
  File gpsFile = FFat.open(ACTIVE_GPS_PATH, FILE_READ);
  uint8_t copyBuffer[512];
  size_t gpsBytesRemaining = gpsSampleCount * sizeof(GpsTrackPoint);
  while (ok && gpsBytesRemaining) {
    const size_t amount = min(gpsBytesRemaining, sizeof(copyBuffer));
    if (!gpsFile || gpsFile.read(copyBuffer, amount) != amount ||
        file.write(copyBuffer, amount) != amount) {
      ok = false;
      break;
    }
    gpsBytesRemaining -= amount;
  }
  if (gpsFile) gpsFile.close();
  file.close();
  if (!ok) {
    FFat.remove(path);
    summaryGpsPath[0] = '\0';
  } else {
    strncpy(summaryGpsPath, path, sizeof(summaryGpsPath) - 1);
    summaryGpsPath[sizeof(summaryGpsPath) - 1] = '\0';
    summaryGpsOffset = sizeof(RideFileHeader) +
        altitudeSampleCount * sizeof(float) * 2;
  }
  summaryRouteLoaded = false;
  FFat.remove(ACTIVE_GPS_PATH);
  scanSavedRides();
}

bool loadSavedRide(uint8_t index) {
  if (index >= savedRideCount) return false;
  File file = FFat.open(savedRides[index].path, FILE_READ);
  if (!file) return false;
  RideFileHeader header = {};
  bool ok = file.read(reinterpret_cast<uint8_t *>(&header), sizeof(header)) == sizeof(header) &&
      header.magic == RIDE_FILE_MAGIC && header.formatVersion == 1 &&
      header.minuteCount <= MAX_ALTITUDE_SAMPLES && header.gpsCount <= MAX_GPS_SAMPLES;
  if (ok) ok = file.read(reinterpret_cast<uint8_t *>(altitudeSamples),
                         header.minuteCount * sizeof(float)) == header.minuteCount * sizeof(float);
  if (ok) ok = file.read(reinterpret_cast<uint8_t *>(speedSamples),
                         header.minuteCount * sizeof(float)) == header.minuteCount * sizeof(float);
  if (ok) {
    const size_t expectedSize = sizeof(RideFileHeader) +
        header.minuteCount * sizeof(float) * 2 +
        header.gpsCount * sizeof(GpsTrackPoint);
    ok = file.size() >= expectedSize;
  }
  file.close();
  if (!ok) return false;
  altitudeSampleCount = header.minuteCount;
  gpsSampleCount = header.gpsCount;
  rideStartEpoch = header.startEpoch;
  summaryRideSeconds = header.durationSeconds;
  summaryDistanceMiles = header.distanceMiles;
  summaryAverageMph = header.averageMph;
  summaryClimbFeet = header.climbFeet;
  summaryPage = 0;
  strncpy(summaryGpsPath, savedRides[index].path, sizeof(summaryGpsPath) - 1);
  summaryGpsPath[sizeof(summaryGpsPath) - 1] = '\0';
  summaryGpsOffset = sizeof(RideFileHeader) +
      header.minuteCount * sizeof(float) * 2;
  summaryRouteLoaded = false;
  viewingSavedRide = true;
  appState = SUMMARY;
  previousDrawMs = 0;
  return true;
}

void formatRideName(const SavedRideMeta &ride, char *text, size_t size) {
  if (!ride.startEpoch) {
    snprintf(text, size, "--/--/--");
    return;
  }
  const time_t timestamp = ride.startEpoch;
  struct tm localTime;
  localtime_r(&timestamp, &localTime);
  strftime(text, size, "%m/%d/%y", &localTime);
}

void textCenteredRide(const char *text, int x, int y, uint16_t color) {
  canvas->setFont(&FreeSansBold18pt7b);
  canvas->setTextSize(1);
  canvas->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor(x - (x1 + int(w) / 2), y - (y1 + int(h) / 2));
  canvas->print(text);
}

void deleteSavedRide(uint8_t index) {
  if (!rideStorageReady || index >= savedRideCount) return;
  FFat.remove(savedRides[index].path);
  scanSavedRides();
  pendingRideDeleteIndex = -1;
  previousDrawMs = 0;
}

void textCentered(const char *text, int x, int y, uint8_t size,
                  uint16_t color, bool bottom = false) {
  const GFXfont *font = &FreeSans9pt7b;
  if (size == 2) font = &FreeSans12pt7b;
  else if (size == 3) font = &FreeSans18pt7b;
  else if (size == 4) font = &FreeSansBold18pt7b;
  else if (size >= 5) font = &FreeSansBold24pt7b;
  canvas->setFont(font);
  canvas->setTextSize(1);
  canvas->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  const int baseline = bottom ? y - (y1 + h) : y - (y1 + int(h) / 2);
  canvas->setCursor(x - (x1 + int(w) / 2), baseline);
  canvas->print(text);
}

void textLeft(const char *text, int x, int y, uint8_t size, uint16_t color) {
  const GFXfont *font = size >= 3 ? &FreeSans18pt7b :
                        (size == 2 ? &FreeSans12pt7b : &FreeSans9pt7b);
  canvas->setFont(font);
  canvas->setTextSize(1);
  canvas->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor(x - x1, y - y1);
  canvas->print(text);
}

void textCenteredScaled(const char *text, int x, int y, uint8_t scale,
                        uint16_t color) {
  canvas->setFont(&FreeSansBold24pt7b);
  canvas->setTextSize(scale);
  canvas->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor(x - (x1 + int(w) / 2), y - (y1 + int(h) / 2));
  canvas->print(text);
  canvas->setTextSize(1);
}

void textCenteredNumeric(const char *text, int x, int y, uint16_t color) {
  canvas->setFont(&Arial_Bold92pt7b);
  canvas->setTextSize(1);
  canvas->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor(x - (x1 + int(w) / 2), y - (y1 + int(h) / 2));
  canvas->print(text);
}

void textPairCentered(const char *label, const char *value, int x, int y,
                      uint8_t size, uint16_t labelColor,
                      uint16_t valueColor) {
  const GFXfont *font = size >= 3 ? &FreeSans18pt7b :
                        (size == 2 ? &FreeSans12pt7b : &FreeSans9pt7b);
  canvas->setFont(font);
  canvas->setTextSize(1);
  int16_t lx, ly, vx, vy;
  uint16_t lw, lh, vw, vh;
  canvas->getTextBounds(label, 0, 0, &lx, &ly, &lw, &lh);
  canvas->getTextBounds(value, 0, 0, &vx, &vy, &vw, &vh);
  const int gap = us(7);
  const int start = x - (int(lw) + gap + int(vw)) / 2;
  const int height = max(int(lh), int(vh));
  const int baseline = y - (min(ly, vy) + height / 2);
  canvas->setTextColor(labelColor);
  canvas->setCursor(start - lx, baseline);
  canvas->print(label);
  canvas->setTextColor(valueColor);
  canvas->setCursor(start + lw + gap - vx, baseline);
  canvas->print(value);
}

void wideLine(int x1, int y1, int x2, int y2, int radius, uint16_t color) {
  canvas->drawLine(x1, y1, x2, y2, color);
  if (radius > 1) {
    canvas->drawLine(x1 + 1, y1, x2 + 1, y2, color);
    canvas->drawLine(x1, y1 + 1, x2, y2 + 1, color);
  }
}

bool gpsAvailable(uint32_t now) {
  bool available;
  uint32_t receivedMs;
  portENTER_CRITICAL(&locationMux);
  available = hasLocation;
  receivedMs = lastLocationMs;
  portEXIT_CRITICAL(&locationMux);
  return available && timestampIsFresh(now, receivedMs, GPS_STATUS_TIMEOUT_MS);
}

void publishOnboardGpsFix(uint32_t now) {
  OnboardGpsFix fix;
  if (!onboardGps.takeFix(lastOnboardGpsSequence, fix)) return;

  LocationPacket packet = {};
  packet.version = 1;
  packet.sequence = fix.sequence;
  packet.timestamp = fix.timestamp;
  packet.latitudeE7 = fix.latitudeE7;
  packet.longitudeE7 = fix.longitudeE7;
  packet.altitudeDm = fix.altitudeDm;
  packet.speedCms = fix.speedCms;
  packet.courseDeg100 = fix.courseDeg100;
  packet.accuracyCm = fix.accuracyCm;
  packet.flags = fix.flags;

  portENTER_CRITICAL(&locationMux);
  if (bleBarometerAvailable &&
      timestampIsFresh(now, lastBleBarometerMs, LOCATION_TIMEOUT_MS)) {
    packet.version = 2;
    packet.flags |= 1 << 3;
    packet.barometricRelativeAltitudeCm = latestBleBarometerCm;
  }
  latestLocation = packet;
  lastLocationMs = now;
  hasLocation = true;
  portEXIT_CRITICAL(&locationMux);
}

void drawBluetoothIcon(int x, int y, bool enabled, bool connected) {
  const uint16_t c = !enabled ? 0x8410 :
      (connected ? BLE_CONNECTED_BLUE : RED);
  const auto is = [](float v) { return us(v * 0.52f); };
  const int radius = max(1, is(1.1f));
  const auto thickLine = [radius, c](int x1, int y1, int x2, int y2) {
    for (int dx = -radius; dx <= radius; ++dx)
      for (int dy = -radius; dy <= radius; ++dy)
        if (dx * dx + dy * dy <= radius * radius)
          canvas->drawLine(x1 + dx, y1 + dy, x2 + dx, y2 + dy, c);
  };
  thickLine(x, y - is(12), x, y + is(12));
  thickLine(x, y - is(12), x + is(8), y - is(5));
  thickLine(x + is(8), y - is(5), x - is(7), y + is(7));
  thickLine(x - is(7), y - is(7), x + is(8), y + is(5));
  thickLine(x + is(8), y + is(5), x, y + is(12));
}

void drawNavigationIcon(int x, int y, bool available) {
  const uint16_t c = available ? BLE_CONNECTED_BLUE : YELLOW;
  const auto is = [](float v) { return us(v * 0.52f); };
  const int radius = is(12.5f);
  const int ringWidth = max(1, is(1.6f));
  for (int inset = 0; inset < ringWidth; ++inset)
    canvas->drawCircle(x, y, radius - inset, c);

  const int tipX = x + is(7), tipY = y - is(8);
  const int leftX = x - is(8), leftY = y - is(2);
  const int notchX = x - is(1), notchY = y + is(1);
  const int bottomX = x + is(1), bottomY = y + is(8);
  canvas->fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY, c);
  canvas->fillTriangle(tipX, tipY, notchX, notchY, bottomX, bottomY, c);
}

uint16_t batteryColor() {
  if (batteryPercent < 20) return RED;
  if (batteryPercent < 50) return YELLOW;
  return WHITE;
}

void drawBatteryIcon(int x, int y) {
  const int width = us(16);
  const int height = us(9);
  const int left = x - width / 2;
  const int top = y - height / 2;
  const uint16_t c = batteryConnected ? batteryColor() : RED;
  canvas->drawRoundRect(left, top, width, height, us(2), c);
  canvas->fillRect(left + width, y - us(2), us(2), us(4), c);
  if (!batteryConnected) {
    wideLine(left + us(4), y - us(2), left + width - us(4), y + us(2),
             max(1, us(1)), RED);
    wideLine(left + width - us(4), y - us(2), left + us(4), y + us(2),
             max(1, us(1)), RED);
    return;
  }
  const int innerWidth = max(1, (width - us(4)) * batteryPercent / 100);
  canvas->fillRoundRect(left + us(2), top + us(2), innerWidth,
                        height - us(4), us(1), batteryColor());
  if (batteryCharging) {
    const int cx = x;
    canvas->fillTriangle(cx + us(1), top + us(1), cx - us(4), y + us(1),
                         cx, y + us(1), GREEN);
    canvas->fillTriangle(cx - us(1), y - us(1), cx + us(4), y - us(1),
                         cx, top + height - us(1), GREEN);
  }
}

void updateBattery(uint32_t now, bool force = false) {
  if (!pmicAvailable || (!force && now - lastBatteryReadMs < 2000)) return;
  lastBatteryReadMs = now;
  batteryConnected = power.isBatteryConnect();
  batteryCharging = batteryConnected && power.isCharging();
  batteryPercent = batteryConnected ? constrain(int(power.getBatteryPercent()), 0, 100) : 0;
}

void drawClock(uint32_t now, int y, uint8_t size = 3) {
  char text[16] = "--:--";
  LocationPacket packet;
  bool timeAvailable;
  uint32_t receivedMs;
  portENTER_CRITICAL(&locationMux);
  packet = latestLocation;
  timeAvailable = hasLocation && packet.timestamp > 0;
  receivedMs = lastLocationMs;
  if (!timeAvailable && onboardGpsPresent && latestBleTimestamp &&
      timestampIsFresh(now, lastBleTimestampMs, GPS_STATUS_TIMEOUT_MS)) {
    packet.timestamp = latestBleTimestamp;
    receivedMs = lastBleTimestampMs;
    timeAvailable = true;
  }
  portEXIT_CRITICAL(&locationMux);
  if (timeAvailable) {
    time_t phoneTime = packet.timestamp + nonNegativeElapsedMs(now, receivedMs) / 1000;
    struct tm localTime;
    localtime_r(&phoneTime, &localTime);
    strftime(text, sizeof(text), "%I:%M%p", &localTime);
    for (char *p = text; *p; ++p)
      if (*p == 'A' || *p == 'P' || *p == 'M') *p += 'a' - 'A';
    if (text[0] == '0') memmove(text, text + 1, strlen(text));
  }
  textCentered(text, CENTER, y, size, WHITE);
}

void drawHeader(uint32_t now) {
  // BLE follows the upper-left contour. Navigation at right remains yellow
  // while waiting for a usable fix, then changes to connected blue.
  // Lift the clock above the icon centers so the top row reads as a clear
  // hierarchy instead of four marks sharing one baseline.
  drawBluetoothIcon(ux(68), uy(20), bluetoothEnabled, phoneConnected);
  drawNavigationIcon(ux(172), uy(20), gpsAvailable(now));
  drawClock(now, uy(16));
  drawBatteryIcon(CENTER, uy(35));
}

float navArrowYOffset = 0.0f;
float navArrowScale = 1.0f;

float navArrowX(float x) {
  return 120.0f + (x - 120.0f) * navArrowScale;
}

float navArrowY(float y) {
  return 101.0f + (y - 101.0f) * navArrowScale + navArrowYOffset;
}

void drawNavSegment(float x1, float y1, float x2, float y2, float width = 8.0f) {
  x1 = navArrowX(x1);
  y1 = navArrowY(y1);
  x2 = navArrowX(x2);
  y2 = navArrowY(y2);
  const float sx1 = x1 * UI_SCALE;
  const float sy1 = y1 * UI_SCALE;
  const float sx2 = x2 * UI_SCALE;
  const float sy2 = y2 * UI_SCALE;
  const float dx = sx2 - sx1;
  const float dy = sy2 - sy1;
  const int steps = max(1, int(ceilf(sqrtf(dx * dx + dy * dy))));
  const int radius = max(1, us(width * navArrowScale * 0.5f));
  for (int i = 0; i <= steps; ++i) {
    const float t = float(i) / steps;
    canvas->fillCircle(lroundf(sx1 + dx * t), lroundf(sy1 + dy * t), radius, GREEN);
  }
}

void drawNavCurve(float x0, float y0, float controlX, float controlY,
                  float x1, float y1, float width = 8.0f) {
  float previousX = x0;
  float previousY = y0;
  for (int i = 1; i <= 18; ++i) {
    const float t = float(i) / 18.0f;
    const float inverse = 1.0f - t;
    const float x = inverse * inverse * x0 + 2.0f * inverse * t * controlX + t * t * x1;
    const float y = inverse * inverse * y0 + 2.0f * inverse * t * controlY + t * t * y1;
    drawNavSegment(previousX, previousY, x, y, width);
    previousX = x;
    previousY = y;
  }
}

void drawNavArrowHead(float tipX, float tipY, float directionX, float directionY) {
  const float magnitude = sqrtf(directionX * directionX + directionY * directionY);
  if (magnitude < 0.01f) return;
  tipX = navArrowX(tipX);
  tipY = navArrowY(tipY);
  const float dx = directionX / magnitude;
  const float dy = directionY / magnitude;
  const float nx = -dy;
  const float ny = dx;
  const float baseX = tipX - dx * 20.0f * navArrowScale;
  const float baseY = tipY - dy * 20.0f * navArrowScale;
  canvas->fillTriangle(ux(lroundf(tipX)), uy(lroundf(tipY)),
                       ux(lroundf(baseX + nx * 14.0f * navArrowScale)),
                       uy(lroundf(baseY + ny * 14.0f * navArrowScale)),
                       ux(lroundf(baseX - nx * 14.0f * navArrowScale)),
                       uy(lroundf(baseY - ny * 14.0f * navArrowScale)), GREEN);
}

void drawNavigationArrow(Maneuver maneuver, int yOffset = 0) {
  navArrowYOffset = yOffset;
  navArrowScale = 1.18f;
  const int cx = CENTER;
  const int cy = uy(101 + yOffset);
  canvas->fillCircle(cx, cy, us(48 * navArrowScale), 0x1082);

  if (maneuver == MANEUVER_ARRIVE) {
    drawNavSegment(120, 139, 120, 65);
    drawNavArrowHead(120, 59, 0, -1);
    canvas->fillTriangle(ux(lroundf(navArrowX(124))), uy(lroundf(navArrowY(72))),
                         ux(lroundf(navArrowX(163))), uy(lroundf(navArrowY(86))),
                         ux(lroundf(navArrowX(124))), uy(lroundf(navArrowY(100))), GREEN);
  } else if (maneuver == MANEUVER_ROUNDABOUT) {
    canvas->drawArc(cx, cy, us(31 * navArrowScale), us(24 * navArrowScale), 30, 326, GREEN);
    canvas->drawArc(cx, cy, us(30 * navArrowScale), us(23 * navArrowScale), 30, 326, GREEN);
    canvas->drawArc(cx, cy, us(29 * navArrowScale), us(22 * navArrowScale), 30, 326, GREEN);
    drawNavArrowHead(149, 77, 1, 0.65f);
  } else if (maneuver == MANEUVER_UTURN) {
    drawNavSegment(91, 139, 91, 101);
    drawNavCurve(91, 101, 91, 66, 120, 66);
    drawNavCurve(120, 66, 149, 66, 149, 101);
    drawNavSegment(149, 101, 149, 111);
    drawNavArrowHead(149, 118, 0, 1);
  } else if (maneuver == MANEUVER_SLIGHT_LEFT) {
    drawNavCurve(139, 140, 139, 97, 87, 65);
    drawNavArrowHead(80, 61, -1, -0.62f);
  } else if (maneuver == MANEUVER_SHARP_LEFT) {
    drawNavSegment(139, 140, 139, 101);
    drawNavCurve(139, 101, 139, 77, 111, 77);
    drawNavSegment(111, 77, 81, 77);
    drawNavArrowHead(70, 77, -1, 0);
  } else if (maneuver == MANEUVER_LEFT) {
    drawNavSegment(120, 140, 120, 99);
    drawNavCurve(120, 99, 120, 78, 99, 78);
    drawNavSegment(99, 78, 81, 78);
    drawNavArrowHead(70, 78, -1, 0);
  } else if (maneuver == MANEUVER_SLIGHT_RIGHT) {
    drawNavCurve(101, 140, 101, 97, 153, 65);
    drawNavArrowHead(160, 61, 1, -0.62f);
  } else if (maneuver == MANEUVER_SHARP_RIGHT) {
    drawNavSegment(101, 140, 101, 101);
    drawNavCurve(101, 101, 101, 77, 129, 77);
    drawNavSegment(129, 77, 159, 77);
    drawNavArrowHead(170, 77, 1, 0);
  } else if (maneuver == MANEUVER_RIGHT) {
    drawNavSegment(120, 140, 120, 99);
    drawNavCurve(120, 99, 120, 78, 141, 78);
    drawNavSegment(141, 78, 159, 78);
    drawNavArrowHead(170, 78, 1, 0);
  } else if (maneuver == MANEUVER_STRAIGHT) {
    drawNavSegment(120, 140, 120, 72);
    drawNavArrowHead(120, 59, 0, -1);
  } else {
    textCentered("NAV", CENTER, cy, 5, GREEN);
  }
  navArrowYOffset = 0.0f;
  navArrowScale = 1.0f;
}

void drawWrappedNavigationText(const char *text, int y, int maxLines,
                               uint8_t size = 3) {
  const GFXfont *font = size >= 5 ? &FreeSansBold24pt7b :
                        (size >= 4 ? &FreeSansBold18pt7b : &FreeSans18pt7b);
  canvas->setFont(font);
  canvas->setTextSize(1);
  canvas->setTextColor(WHITE);
  String remaining(text);
  for (int line = 0; line < maxLines && remaining.length(); ++line) {
    int take = remaining.length();
    int16_t x1, y1;
    uint16_t width, height;
    while (take > 1) {
      String candidate = remaining.substring(0, take);
      canvas->getTextBounds(candidate, 0, 0, &x1, &y1, &width, &height);
      if (width <= ux(200)) break;
      --take;
    }
    if (take < int(remaining.length())) {
      const int space = remaining.lastIndexOf(' ', take);
      if (space > 0) take = space;
    }
    String row = remaining.substring(0, take);
    row.trim();
    textCentered(row.c_str(), CENTER, uy(y + line * 27), size, WHITE);
    remaining = remaining.substring(take);
    remaining.trim();
  }
}

bool navigationIsVisible(uint32_t now) {
  bool visible;
  uint32_t received;
  portENTER_CRITICAL(&navMux);
  visible = navVisible;
  received = navReceivedMs;
  // `now` is sampled just before notifications.loop(). A callback handled by
  // that loop can therefore stamp navReceivedMs a few milliseconds later.
  // Treat that small negative age as fresh instead of allowing unsigned
  // subtraction to wrap and expire the instruction immediately.
  const int32_t ageMs = int32_t(now - received);
  if (visible && ageMs >= 0 && uint32_t(ageMs) >= NAV_TIMEOUT_MS) {
    navVisible = false;
    visible = false;
  }
  portEXIT_CRITICAL(&navMux);
  return visible;
}

void drawNavigationPage(uint32_t now) {
  Maneuver maneuver;
  char title[sizeof(navTitle)];
  char message[sizeof(navMessage)];
  char distance[sizeof(navDistance)];
  portENTER_CRITICAL(&navMux);
  maneuver = navManeuver;
  memcpy(title, navTitle, sizeof(title));
  memcpy(message, navMessage, sizeof(message));
  memcpy(distance, navDistance, sizeof(distance));
  portEXIT_CRITICAL(&navMux);
  beginFrame();
  drawHeader(now);
  drawNavigationArrow(maneuver);
  if (distance[0]) textCentered(distance, CENTER, uy(157), 5, GREEN);
  const char *primary = message[0] ? message : title;
  const String street = navigationStreetName(primary);
  drawWrappedNavigationText(street.c_str(), distance[0] ? 185 : 174, 2, 5);
  canvas->fillCircle(CENTER, uy(232), us(4), GREEN);
  endFrame();
}

void drawMushroom(int x, int y) {
  canvas->fillRoundRect(x - us(18), y + us(12), us(36), us(39), us(9), MUSHROOM_TAN);
  canvas->fillEllipse(x, y + us(47), us(22), us(7), 0xC4CC);
  canvas->fillEllipse(x, y, us(43), us(29), MUSHROOM_RED);
  canvas->fillCircle(x - us(22), y - us(5), us(9), WHITE);
  canvas->fillEllipse(x, y - us(15), us(11), us(7), WHITE);
  canvas->fillCircle(x + us(22), y - us(7), us(8), WHITE);
  canvas->fillCircle(x + us(7), y + us(7), us(11), WHITE);
}

void drawRadarWarningRing(uint32_t now);

void beginFrame() { canvas->fillScreen(BLACK); }
void endFrame() {
  if (suppressFrameFlush) {
    suppressedFrameCompleted = true;
    return;
  }
  drawRadarWarningRing(millis());
  // Preserve the original UI/touch timing, but avoid the expensive QSPI
  // transfer when rendering produced exactly the same pixels as last time.
  const uint32_t *pixels = reinterpret_cast<const uint32_t *>(canvas->getFramebuffer());
  constexpr size_t wordCount = (SCREEN_SIZE * SCREEN_SIZE * sizeof(uint16_t)) / sizeof(uint32_t);
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < wordCount; ++i) hash = (hash ^ pixels[i]) * 16777619u;
  if (!hasFramebufferHash || hash != lastFramebufferHash) {
    canvas->flush();
    lastFramebufferHash = hash;
    hasFramebufferHash = true;
  }
}

void endAnimatedFrame() {
  if (suppressFrameFlush) {
    suppressedFrameCompleted = true;
    return;
  }
  drawRadarWarningRing(millis());
  // Animation frames are known to differ. Avoid rereading the entire 434 KB
  // PSRAM framebuffer just to prove that before sending it to the display.
  canvas->flush();
  hasFramebufferHash = false;
}

uint16_t interpolateRgb565(uint16_t top, uint16_t bottom, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const float inverse = 1.0f - amount;
  const uint16_t red = uint16_t(((top >> 11) & 0x1f) * inverse +
                                ((bottom >> 11) & 0x1f) * amount + 0.5f);
  const uint16_t green = uint16_t(((top >> 5) & 0x3f) * inverse +
                                  ((bottom >> 5) & 0x3f) * amount + 0.5f);
  const uint16_t blue = uint16_t((top & 0x1f) * inverse +
                                 (bottom & 0x1f) * amount + 0.5f);
  return (red << 11) | (green << 5) | blue;
}

void fillVerticalGradientRoundRect(int x, int y, int width, int height,
                                   int radius, uint16_t top,
                                   uint16_t bottom) {
  for (int row = 0; row < height; ++row) {
    int inset = 0;
    if (row < radius) {
      const int dy = radius - row;
      inset = radius - int(sqrtf(float(radius * radius - dy * dy)));
    } else if (row >= height - radius) {
      const int dy = row - (height - radius - 1);
      inset = radius - int(sqrtf(float(max(0, radius * radius - dy * dy))));
    }
    const uint16_t color = interpolateRgb565(
        top, bottom, height > 1 ? float(row) / float(height - 1) : 0.0f);
    canvas->drawFastHLine(x + inset, y + row,
                          max(1, width - inset * 2), color);
  }
}

void drawMushroomEmoji(int x, int y) {
  // Compact version of the PedalOne mushroom artwork, sized to sit on the
  // same baseline as the device nickname.
  canvas->fillRoundRect(x-us(5),y+us(1),us(10),us(10),us(3),MUSHROOM_TAN);
  canvas->fillEllipse(x,y-us(2),us(9),us(6),MUSHROOM_RED);
  canvas->fillCircle(x-us(4),y-us(4),us(2),WHITE);
  canvas->fillCircle(x+us(4),y-us(3),us(2),WHITE);
  canvas->fillCircle(x,y-us(6),us(2),WHITE);
}

void drawClownEmoji(int x, int y) {
  // Small native rendering of U+1F921.  The bundled fonts are ASCII-only, so
  // use bold geometric features that remain recognizable at roughly 42 px.
  constexpr uint16_t clownRed = 0xF986;
  constexpr uint16_t clownBlue = 0x367F;
  constexpr uint16_t clownFace = 0xFFDF;
  canvas->fillCircle(x-us(8),y-us(4),us(5),clownRed);
  canvas->fillCircle(x-us(4),y-us(8),us(5),clownRed);
  canvas->fillCircle(x,y-us(9),us(5),clownRed);
  canvas->fillCircle(x+us(4),y-us(8),us(5),clownRed);
  canvas->fillCircle(x+us(8),y-us(4),us(5),clownRed);
  canvas->fillEllipse(x,y+us(1),us(8),us(10),clownFace);
  canvas->fillTriangle(x-us(6),y-us(4),x-us(1),y-us(5),
                       x-us(3),y+us(1),clownBlue);
  canvas->fillTriangle(x+us(6),y-us(4),x+us(1),y-us(5),
                       x+us(3),y+us(1),clownBlue);
  canvas->fillCircle(x-us(3),y-us(2),max(1,us(1)),BLACK);
  canvas->fillCircle(x+us(3),y-us(2),max(1,us(1)),BLACK);
  canvas->fillCircle(x,y+us(2),us(2),clownRed);
  canvas->fillCircle(x,y+us(5),us(4),clownRed);
  canvas->fillEllipse(x,y+us(4),us(3),us(2),clownFace);
}

void drawDeviceIdentityRow(int y) {
  // The AMOLED font set is ASCII-only, so supported emoji are native vector
  // art while the text itself remains strictly the nickname.
  const bool mushroom = deviceEmoji == "\xF0\x9F\x8D\x84"; // U+1F344
  const bool clown = deviceEmoji == "\xF0\x9F\xA4\xA1";    // U+1F921
  const bool downloaded = deviceIcon.availableFor(deviceEmoji);
  const bool hasIcon = downloaded || mushroom || clown;
  const int iconWidth = downloaded ? deviceIcon.width() :
                        (hasIcon ? us(22) : 0);
  const int gap = hasIcon ? us(4) : 0;
  const int maxRowWidth = us(190);
  const GFXfont *fonts[] = {
      &FreeSansBold24pt7b, &FreeSansBold18pt7b, &FreeSansBold12pt7b};
  const GFXfont *chosen = fonts[2];
  int16_t x1=0,y1=0;
  uint16_t width=0,height=0;
  for (const GFXfont *font : fonts) {
    canvas->setFont(font);
    canvas->setTextSize(1);
    canvas->getTextBounds(deviceNickname.c_str(),0,0,&x1,&y1,&width,&height);
    chosen=font;
    if (int(width)+iconWidth+gap<=maxRowWidth) break;
  }
  canvas->setFont(chosen);
  canvas->getTextBounds(deviceNickname.c_str(),0,0,&x1,&y1,&width,&height);
  const int totalWidth=iconWidth+gap+int(width);
  const int startX=CENTER-totalWidth/2;
  if(downloaded)
    canvas->draw16bitRGBBitmap(startX,y-deviceIcon.height()/2,
                              deviceIcon.pixels(),deviceIcon.width(),
                              deviceIcon.height());
  else if(mushroom)drawMushroomEmoji(startX+iconWidth/2,y);
  else if(clown)drawClownEmoji(startX+iconWidth/2,y);
  canvas->setTextColor(WHITE);
  canvas->setCursor(startX+iconWidth+gap-x1,y-(y1+int(height)/2));
  canvas->print(deviceNickname);
}

void textCenteredScaledToWidth(const char *text,int x,int y,int maxWidth,
                               uint8_t maxScale,uint16_t color) {
  canvas->setFont(&FreeSansBold24pt7b);
  uint8_t scale=maxScale;
  int16_t x1,y1;
  uint16_t width,height;
  while(scale>1) {
    canvas->setTextSize(scale);
    canvas->getTextBounds(text,0,0,&x1,&y1,&width,&height);
    if(width<=maxWidth)break;
    --scale;
  }
  canvas->setTextSize(scale);
  canvas->setTextColor(color);
  canvas->getTextBounds(text,0,0,&x1,&y1,&width,&height);
  canvas->setCursor(x-(x1+int(width)/2),y-(y1+int(height)/2));
  canvas->print(text);
  canvas->setTextSize(1);
}

void drawRollingLogoGradientText(const char *text, int x, int y, int maxWidth,
                                 uint8_t maxScale, uint32_t now) {
  const GFXfont *font = &FreeSansBold24pt7b;
  canvas->setFont(font);
  uint8_t scale = maxScale;
  int16_t x1 = 0, y1 = 0;
  uint16_t width = 0, height = 0;
  while (scale > 1) {
    canvas->setTextSize(scale);
    canvas->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
    if (width <= maxWidth) break;
    --scale;
  }
  canvas->setTextSize(scale);
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  int cursorX = x - (x1 + int(width) / 2);
  const int cursorY = y - (y1 + int(height) / 2);
  const int left = cursorX + x1;
  const int top = cursorY + y1;
  const float diagonalSpan = max(1, int(width) + int(height));
  const float motion = fmodf(float(now) / 2200.0f, 1.0f);
  const uint8_t first = pgm_read_byte(&font->first);
  const uint8_t last = pgm_read_byte(&font->last);
  const GFXglyph *glyphs = reinterpret_cast<const GFXglyph *>(
      pgm_read_ptr(&font->glyph));
  const uint8_t *bitmap = reinterpret_cast<const uint8_t *>(
      pgm_read_ptr(&font->bitmap));

  for (const char *p = text; *p; ++p) {
    const uint8_t c = uint8_t(*p);
    if (c < first || c > last) continue;
    const GFXglyph *glyph = glyphs + c - first;
    uint16_t bitmapOffset = pgm_read_word(&glyph->bitmapOffset);
    const uint8_t glyphWidth = pgm_read_byte(&glyph->width);
    const uint8_t glyphHeight = pgm_read_byte(&glyph->height);
    const uint8_t advance = pgm_read_byte(&glyph->xAdvance);
    const int8_t xOffset = int8_t(pgm_read_byte(&glyph->xOffset));
    const int8_t yOffset = int8_t(pgm_read_byte(&glyph->yOffset));
    uint8_t bits = 0;
    uint8_t bit = 0;
    for (uint8_t yy = 0; yy < glyphHeight; ++yy) {
      for (uint8_t xx = 0; xx < glyphWidth; ++xx) {
        if (!(bit++ & 7)) bits = pgm_read_byte(bitmap + bitmapOffset++);
        if (bits & 0x80) {
          const int px = cursorX + (xOffset + xx) * scale;
          const int py = cursorY + (yOffset + yy) * scale;
          float position = (float(px - left) + float(py - top)) / diagonalSpan;
          position = fmodf(position - motion + 2.0f, 1.0f);
          const float blend = 0.5f - 0.5f * cosf(position * 2.0f * PI);
          canvas->fillRect(px, py, scale, scale,
                           interpolateRgb565(LOGO_LIME, LOGO_CYAN, blend));
        }
        bits <<= 1;
      }
    }
    cursorX += advance * scale;
  }
  canvas->setTextSize(1);
}

void drawReadyScreen(uint32_t now) {
  beginFrame();
  drawHeader(now);
  drawDeviceIdentityRow(uy(66));
  // The home action now sits just below center and starts ride selection
  // directly; the former START/MENU intermediary is no longer reachable.
  drawRollingLogoGradientText("Let's Go!", CENTER, CENTER, us(216), 2, now);
  // Keep menu access visible as well as universal: the whole lower touch
  // region still opens the menu, while this frame matches the former home
  // MENU control and makes the action discoverable.
  canvas->drawRoundRect(ux(82), uy(205), us(76), us(28), us(6), WHITE);
  textCentered("MENU", CENTER, uy(219), 2, WHITE);
  endAnimatedFrame();
}

#include "menu_graphics.inc"

void drawMenu(uint32_t now) {
  const bool ride = appState == RIDE_MENU;
  beginFrame();
  drawHeader(now);
  if (appState == MENU) {
    const int frameThickness = menuSelection == 0 ? 4 : 2;
    for (int inset = 0; inset < frameThickness; ++inset)
      canvas->drawRoundRect(ux(20) + inset, uy(80) + inset,
                            us(200) - inset * 2, us(80) - inset * 2,
                            us(10), GREEN);
    textCenteredScaled("START", CENTER, CENTER, 2, WHITE);
    canvas->drawRoundRect(ux(82), uy(205), us(76), us(28), us(6), WHITE);
    textCentered("MENU", CENTER, uy(219), 2, WHITE);
    endFrame();
    return;
  }
  if (ride) {
    textCentered("MENU", CENTER, uy(62), 5, WHITE);
    canvas->drawFastHLine(ux(48), uy(79), ux(144), 0x4208);
    textCentered("Info", ux(70), uy(101), 4, WHITE);
    textCentered("Display", ux(170), uy(101), 4, WHITE);
    drawMenuInfoIcon(ux(70), uy(140), 0.58f);
    canvas->fillCircle(ux(170),uy(140),us(14),WHITE);
    for(int ray=0;ray<8;++ray) {
      const float angle=ray*PI/4;
      wideLine(ux(170)+lroundf(cosf(angle)*us(20)),uy(140)+lroundf(sinf(angle)*us(20)),
               ux(170)+lroundf(cosf(angle)*us(26)),uy(140)+lroundf(sinf(angle)*us(26)),us(2),WHITE);
    }

    canvas->drawFastHLine(ux(48), uy(174), us(144), 0x4208);
    canvas->drawFastVLine(CENTER, uy(179), us(50), 0x4208);
    if (ridePaused) {
      for (int row = 0; row < us(36); ++row) {
        const int half = us(18);
        const int width = us(35) * (half - abs(row - half)) / half;
        canvas->drawFastHLine(ux(55), uy(184) + row, max(1, width),
            interpolateRgb565(GREEN, 0x0240, float(row) / us(35)));
      }
    } else {
      fillVerticalGradientRoundRect(ux(57), uy(184), us(9), us(36), us(4), YELLOW, ORANGE);
      fillVerticalGradientRoundRect(ux(74), uy(184), us(9), us(36), us(4), YELLOW, ORANGE);
    }
    fillVerticalGradientRoundRect(ux(156), uy(184), us(35), us(35), us(9), MENU_PINK, RED);
    endFrame();
    return;
  }
  drawGraphicalMenuPage();

  // Deliberate slide-to-off control. A tap cannot accidentally shut down.
  const int sliderLeft = ux(71);
  const int sliderTop = uy(185);
  const int sliderWidth = us(98);
  const int sliderHeight = us(38);
  const int knobRadius = us(16);
  const int knobMin = ux(90);
  const int knobMax = ux(150);
  const int knobX = powerSliderActive
      ? constrain(int(powerSliderX), knobMin, knobMax) : knobMin;
  fillVerticalGradientRoundRect(sliderLeft, sliderTop, sliderWidth,
                                sliderHeight, sliderHeight / 2,
                                0xAD55, 0x738E);
  canvas->fillCircle(knobX, uy(204), knobRadius, 0xF920);
  canvas->fillCircle(knobX - us(3), uy(200), us(5), 0xFAE8);
  textCentered("OFF", ux(139), uy(204), 4, WHITE);
  endFrame();
}

void drawAncsTest(uint32_t now) {
  const AncsClient::Diagnostics d = notifications.diagnostics();
  beginFrame();
  const char *state = d.state == AncsClient::Ready ? "ANCS READY" :
                      (d.state == AncsClient::Connected ? "SECURING" : "PAIR IPHONE");
  if (!ancsHistoryCount) {
    textCentered(state, CENTER, uy(42), 5,
                 d.state == AncsClient::Ready ? GREEN : BABY_BLUE);
    char line[64];
    snprintf(line, sizeof(line), "RAW %lu   SHOWN %lu",
             static_cast<unsigned long>(d.rawEvents),
             static_cast<unsigned long>(ancsDeliveredCount));
    textCentered(line, CENTER, uy(65), 2, WHITE);
    snprintf(line, sizeof(line), "FILTER %lu  DROP %lu  REM %lu",
             static_cast<unsigned long>(d.filteredEvents),
             static_cast<unsigned long>(d.drops),
             static_cast<unsigned long>(ancsRemovedCount));
    textCentered(line, CENTER, uy(87), 2, WHITE);
    snprintf(line, sizeof(line), "TIMEOUT %lu   RETRY %lu",
             static_cast<unsigned long>(d.timeouts),
             static_cast<unsigned long>(d.retries));
    textCentered(line, CENTER, uy(109), 2,
                 (d.timeouts || d.retries) ? RED : WHITE);
    snprintf(line, sizeof(line), "STRAY %lu   ERROR %lu",
             static_cast<unsigned long>(d.strayData),
             static_cast<unsigned long>(d.parseErrors));
    textCentered(line, CENTER, uy(131), 2,
                 (d.strayData || d.parseErrors) ? RED : WHITE);
    snprintf(line, sizeof(line), "QUEUE %u/32   MAX %u", d.queueDepth,
             d.maxQueueDepth);
    textCentered(line, CENTER, uy(153), 2, WHITE);
    snprintf(line, sizeof(line), "REQ %s   LAT %lums",
             d.requestActive ? "YES" : "NO",
             static_cast<unsigned long>(d.lastGoogleLatencyMs));
    textCentered(line, CENTER, uy(175), 2, WHITE);
    snprintf(line, sizeof(line), "MTU %u   LINK %.1fms", d.mtu,
             d.intervalUnits * 1.25f);
    textCentered(line, CENTER, uy(197), 2, WHITE);
  } else if (ancsHistoryPage < ancsHistoryCount) {
    const AncsHistoryItem &item = ancsHistory[ancsHistoryPage];
    // Notification pages use the full round screen: maneuver above, road name
    // below. Connection diagnostics remain on the empty/ready page only.
    drawNavigationArrow(item.maneuver, -25);
    if (item.distance[0])
      textCentered(item.distance, CENTER, uy(133), 5, GREEN);
    const char *primary = item.message[0] ? item.message : item.title;
    const String street = navigationStreetName(primary);
    drawWrappedNavigationText(street.c_str(), item.distance[0] ? 160 : 150, 2, 5);
    char pageText[32];
    snprintf(pageText, sizeof(pageText), "%u/%u",
             unsigned(ancsHistoryPage + 1), unsigned(ancsHistoryCount));
    textCentered(pageText, CENTER, uy(201), 1, WHITE);
  } else {
    textCentered("HISTORY", CENTER, uy(95), 5, WHITE);
    char countText[32];
    snprintf(countText, sizeof(countText), "%u NOTIFICATIONS",
             unsigned(ancsHistoryCount));
    textCentered(countText, CENTER, uy(122), 2, WHITE);
    canvas->drawRoundRect(ux(60), uy(143), us(120), us(36), us(7), RED);
    textCentered("CLEAR", CENTER, uy(161), 3, RED);
  }
  canvas->drawRoundRect(ux(82), uy(207), us(76), us(27), us(6), WHITE);
  textCentered("EXIT", CENTER, uy(221), 2, WHITE);
  endFrame();
}

void drawRidesList(uint32_t now) {
  beginFrame();
  drawHeader(now);
  textCentered("RIDES", CENTER, uy(63), 4, WHITE);
  if (!rideStorageReady) {
    textCentered("STORAGE N/A", CENTER, uy(133), 3, RED);
  } else if (!savedRideCount) {
    textCentered("NO SAVED RIDES", CENTER, uy(133), 3, WHITE);
  } else {
    char date[24];
    char label[48];
    for (uint8_t i = 0; i < savedRideCount; ++i) {
      formatRideName(savedRides[i], date, sizeof(date));
      snprintf(label, sizeof(label), "%s  %.1f mi", date,
               savedRides[i].distanceMiles);
      const int y = 82 + i * 27;
      if (ridesEditMode) {
        snprintf(label, sizeof(label), "%s  %.1f mi", date,
                 savedRides[i].distanceMiles);
        textCenteredRide(label, ux(103), uy(y), WHITE);
        canvas->drawCircle(ux(211), uy(y), us(9), RED);
        wideLine(ux(207), uy(y - 4), ux(215), uy(y + 4), us(1), RED);
        wideLine(ux(215), uy(y - 4), ux(207), uy(y + 4), us(1), RED);
      } else {
        textCenteredRide(label, CENTER, uy(y), WHITE);
      }
    }
  }
  canvas->drawRoundRect(ux(82), uy(207), us(76), us(27), us(6),
                        ridesEditMode ? RED : WHITE);
  textCentered(ridesEditMode ? "DONE" : "EDIT", CENTER, uy(221), 2,
               ridesEditMode ? RED : WHITE);
  endFrame();
}

void drawSaveRidePrompt(uint32_t now) {
  beginFrame();
  drawHeader(now);
  canvas->fillRoundRect(ux(26), uy(62), us(188), us(125), us(10), 0x1082);
  canvas->drawRoundRect(ux(26), uy(62), us(188), us(125), us(10), WHITE);
  textCentered("SAVE RIDE?", CENTER, uy(89), 4, WHITE);
  canvas->fillRoundRect(ux(52), uy(116), us(54), us(43), us(7), GREEN);
  wideLine(ux(66), uy(138), ux(76), uy(148), us(3), BLACK);
  wideLine(ux(76), uy(148), ux(94), uy(126), us(3), BLACK);
  canvas->fillRoundRect(ux(134), uy(116), us(54), us(43), us(7), RED);
  wideLine(ux(148), uy(127), ux(174), uy(149), us(3), WHITE);
  wideLine(ux(174), uy(127), ux(148), uy(149), us(3), WHITE);
  endFrame();
}

const char *wakeCauseName(esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT0: return "TOUCH";
    case ESP_SLEEP_WAKEUP_EXT1: return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER: return "TIMER";
    case ESP_SLEEP_WAKEUP_GPIO: return "GPIO";
    default: return "NONE";
  }
}

const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_DEEPSLEEP: return "DEEP";
    case ESP_RST_USB: return "USB";
    case ESP_RST_POWERON: return "POWER";
    case ESP_RST_SW: return "SW";
    case ESP_RST_BROWNOUT: return "BROWN";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WDT";
    default: return "OTHER";
  }
}

void drawStatus(uint32_t now) {
  char line[40];
  beginFrame();
  if (statusPage == 0) {
    textCentered("STATUS", CENTER, uy(50), 4, WHITE);

    snprintf(line, sizeof(line), "PedalOS ver: %s", FW_VERSION);
    textCentered(line, CENTER, uy(84), 3, WHITE);

    if (batteryConnected)
      snprintf(line, sizeof(line), "Battery: %d%%", batteryPercent);
    else
      snprintf(line, sizeof(line), "Battery: N/A");
    textCentered(line, CENTER, uy(116), 3, WHITE);

    if (onboardGpsPresent)
      snprintf(line, sizeof(line), "GPS: Local (%u)",
               unsigned(onboardGps.satellites()));
    else
      snprintf(line, sizeof(line), "GPS: %s",
               gpsAvailable(now) ? "OK" : "N/A");
    textCentered(line, CENTER, uy(148), 3, WHITE);

    snprintf(line, sizeof(line), "Bluetooth: %s",
             !bluetoothEnabled ? "Off" :
             (phoneConnected ? "Connected" : "Disconnected"));
    textCentered(line, CENTER, uy(180), 3, WHITE);
  } else {
    drawHeader(now);
    textCentered("DISTANCE", CENTER, uy(57), 4, WHITE);
    if (!odometerRecordInvalid && isfinite(odometerMiles + odometerPendingMiles))
      snprintf(line, sizeof(line), "ODO  %.1f mi", odometerMiles + odometerPendingMiles);
    else snprintf(line, sizeof(line), "ODO  --");
    textCentered(line, CENTER, uy(85), 3, WHITE);

    if (!odometerRecordInvalid && isfinite(tripAMiles + tripAPendingMiles))
      snprintf(line, sizeof(line), "TRIP A  %.1f mi", tripAMiles + tripAPendingMiles);
    else snprintf(line, sizeof(line), "TRIP A  --");
    canvas->drawRoundRect(ux(25), uy(103), us(190), us(31), us(7), 0x4208);
    textCentered(line, CENTER, uy(119), 3, WHITE);

    if (!odometerRecordInvalid && isfinite(tripBMiles + tripBPendingMiles))
      snprintf(line, sizeof(line), "TRIP B  %.1f mi", tripBMiles + tripBPendingMiles);
    else snprintf(line, sizeof(line), "TRIP B  --");
    canvas->drawRoundRect(ux(25), uy(138), us(190), us(31), us(7), 0x4208);
    textCentered(line, CENTER, uy(154), 3, WHITE);

    canvas->drawRoundRect(ux(68), uy(188), us(104), us(36), us(9), RED);
    textCentered("RESET ODO", CENTER, uy(206), 2, WHITE);
  }
  endFrame();
}

void drawOtaUpdate(uint32_t now) {
  char line[16];
  beginFrame();
  drawHeader(now);
  textCentered("FIRMWARE UPDATE", CENTER, uy(78), 4, WHITE);
  if (otaUpdate.rebootPending()) {
    textCentered("VERIFIED", CENTER, uy(126), 4, GREEN);
    textCentered("RESTARTING...", CENTER, uy(166), 3, WHITE);
  } else {
    snprintf(line, sizeof(line), "%u%%", otaUpdate.progressPercent());
    textCentered(line, CENTER, uy(125), 6, GREEN);
    canvas->drawRoundRect(ux(35), uy(157), us(170), us(18), us(6), WHITE);
    const int fillWidth = (us(166) * otaUpdate.progressPercent()) / 100;
    if (fillWidth > 0)
      canvas->fillRoundRect(ux(37), uy(159), fillWidth, us(14), us(4), GREEN);
    textCentered("KEEP PHONE NEARBY", CENTER, uy(202), 2, WHITE);
  }
  endFrame();
}

void drawConfirmation(uint32_t now) {
  beginFrame();
  drawHeader(now);
  if (pendingAction == ACTION_RESET_ODOMETER) {
    canvas->fillRoundRect(ux(25), uy(55), us(190), us(145), us(10), 0x1082);
    canvas->drawRoundRect(ux(25), uy(55), us(190), us(145), us(10), WHITE);
    if (!odometerResetArmed) {
      textCentered("RESET ODO?", CENTER, uy(88), 4, WHITE);
      textCentered("Are you sure?", CENTER, uy(120), 2, WHITE);
      canvas->fillRoundRect(ux(84), uy(148), us(72), us(34), us(7), GREEN);
      textCentered("OK", CENTER, uy(165), 4, BLACK);
    } else {
      textCentered("RESET ODO", CENTER, uy(86), 4, WHITE);
      textCentered("Slide to reset", CENTER, uy(112), 1, WHITE);
      const int sliderLeft = ux(55);
      const int sliderTop = uy(130);
      const int sliderWidth = us(130);
      const int sliderHeight = us(42);
      const int knobMin = ux(77);
      const int knobMax = ux(163);
      const int knobX = odometerResetSliderActive
          ? constrain(int(odometerResetSliderX), knobMin, knobMax) : knobMin;
      fillVerticalGradientRoundRect(sliderLeft, sliderTop, sliderWidth,
                                    sliderHeight, sliderHeight / 2,
                                    0x8410, 0x4208);
      textCentered("RESET", ux(135), uy(151), 2, WHITE);
      canvas->fillCircle(knobX, uy(151), us(18), RED);
      canvas->fillCircle(knobX - us(3), uy(147), us(5), 0xFAE8);
    }
    textCentered("Tap outside to cancel", CENTER, uy(190), 1, WHITE);
    endFrame();
    return;
  }
  if (pendingAction == ACTION_RESET_TRIP_A ||
      pendingAction == ACTION_RESET_TRIP_B) {
    canvas->fillRoundRect(ux(25), uy(55), us(190), us(135), us(10), 0x1082);
    canvas->drawRoundRect(ux(25), uy(55), us(190), us(135), us(10), WHITE);
    textCentered(pendingAction == ACTION_RESET_TRIP_A
                     ? "RESET TRIP A?" : "RESET TRIP B?",
                 CENTER, uy(93), 4, WHITE);
    canvas->fillRoundRect(ux(84), uy(132), us(72), us(36), us(7), GREEN);
    textCentered("OK", CENTER, uy(150), 4, BLACK);
    textCentered("Tap outside to cancel", CENTER, uy(180), 1, WHITE);
    endFrame();
    return;
  }
  canvas->fillRoundRect(ux(25), uy(55), us(190), us(130), us(10), 0x1082);
  canvas->drawRoundRect(ux(25), uy(55), us(190), us(130), us(10), WHITE);
  const char *question = pendingAction == ACTION_END_RIDE ? "END RIDE?" :
      (pendingAction == ACTION_POWER_OFF ? "POWER OFF?" :
       (pendingAction == ACTION_RESET_TRIP_A ? "RESET TRIP A?" :
        (pendingAction == ACTION_RESET_TRIP_B ? "RESET TRIP B?" : "DELETE RIDE?")));
  textCentered(question, CENTER, uy(82), 4, WHITE);
  canvas->fillRoundRect(ux(84), uy(105), us(72), us(36), us(7), GREEN);
  textCentered("YES", CENTER, uy(123), 4, BLACK);
  textCentered("Tap outside to cancel", CENTER, uy(163), 1, WHITE);
  endFrame();
}

void drawBluetoothPage(uint32_t now) {
  beginFrame();drawHeader(now);
  textCentered("Bluetooth",CENTER,uy(80),4,WHITE);
  // Keep the ON/OFF labels outside a compact switch while retaining the
  // full-width page hit target used by the touch handler.
  canvas->fillRoundRect(ux(70),uy(114),us(100),us(42),us(21),
                        bluetoothEnabled ? GREEN : 0x4208);
  canvas->fillCircle(ux(bluetoothEnabled ? 149 : 91),uy(135),us(17),WHITE);
  textCentered("OFF",ux(45),uy(135),2,WHITE);
  textCentered("ON",ux(195),uy(135),2,WHITE);
  endFrame();
}

constexpr float RADAR_RANGE_FEET_PER_METER = 3.28084f;
constexpr int RADAR_RANGE_STEP_FEET = 20;
constexpr int RADAR_RANGE_MIN_FEET = 40;
constexpr int RADAR_RANGE_MAX_FEET = 320;

int radarRangeFeet(uint8_t meters) {
  const int rounded = int(lroundf(
      meters * RADAR_RANGE_FEET_PER_METER / RADAR_RANGE_STEP_FEET)) *
      RADAR_RANGE_STEP_FEET;
  return constrain(rounded, RADAR_RANGE_MIN_FEET, RADAR_RANGE_MAX_FEET);
}

uint8_t radarRangeMetersFromFeet(int feet) {
  return uint8_t(constrain(
      int(lroundf(feet / RADAR_RANGE_FEET_PER_METER)), 10, 100));
}

void drawRadarSetupPage(uint32_t now) {
  const RadarClient::State state = radarClient.state();
  beginFrame();
  drawHeader(now);
  textCentered("RADAR", CENTER, uy(64), 4, WHITE);
  textCentered("LD2451 2F42", CENTER, uy(89), 2, 0xAD55);

  if (state == RadarClient::STATE_SEARCHING) {
    textCentered("Searching...", CENTER, uy(127), 3, YELLOW);
    const float phase = (now % 1200) * 2.0f * PI / 1200.0f;
    canvas->fillCircle(CENTER + lroundf(cosf(phase) * us(24)),
                       uy(154) + lroundf(sinf(phase) * us(10)), us(3), YELLOW);
  } else if (state == RadarClient::STATE_FOUND ||
             state == RadarClient::STATE_CONNECTING) {
    textCentered("Radar Found", CENTER, uy(119), 3, GREEN);
    textCentered("Connecting...", CENTER, uy(148), 3, YELLOW);
  } else if (state == RadarClient::STATE_CONNECTED) {
    textCentered("Radar Connected", CENTER, uy(119), 3, GREEN);
    if (radarClient.configState() == RadarClient::CONFIG_APPLYING) {
      textCentered("Applying saved settings...", CENTER, uy(148), 2, YELLOW);
    } else {
      char profile[40];
      const char *profileDirection = radarSettings.direction == 0 ? "APPROACH" :
          (radarSettings.direction == 1 ? "AWAY" : "BOTH");
      snprintf(profile, sizeof(profile), "%dft  %s  SNR %u",
               radarRangeFeet(radarSettings.maximumRangeMeters), profileDirection,
               radarSettings.snrThreshold);
      textCentered(profile, CENTER, uy(148), 2, WHITE);
      canvas->fillRoundRect(ux(76), uy(174), us(88), us(42), us(9), GREEN);
      textCentered("OK", CENTER, uy(195), 4, BLACK);
    }
  } else if (state == RadarClient::STATE_TIMED_OUT) {
    textCentered("Connection timed out", CENTER, uy(117), 3, RED);
    textCentered("Check power and close", CENTER, uy(143), 2, WHITE);
    textCentered("HLKRadarTool / Bluefy", CENTER, uy(161), 2, WHITE);
    canvas->fillRoundRect(ux(66), uy(184), us(108), us(38), us(9), RED);
    textCentered("OK", CENTER, uy(203), 3, WHITE);
  } else {
    textCentered("Radar Off", CENTER, uy(126), 3, 0xAD55);
  }
  endFrame();
}

void loadRadarSettings() {
  if (!deviceStorageReady) return;
  // Apply the safety-focused radar profile once when this firmware generation
  // first boots. Later changes from the settings page remain persistent.
  constexpr uint8_t RADAR_PROFILE_VERSION = 1;
  if (devicePreferences.getUChar("rprofile", 0) < RADAR_PROFILE_VERSION) {
    radarSettings = RadarClient::Settings{};
    devicePreferences.putUChar("rrange", radarSettings.maximumRangeMeters);
    devicePreferences.putUChar("rdir", radarSettings.direction);
    devicePreferences.putUChar("rminspd", radarSettings.minimumSpeedKmh);
    devicePreferences.putUChar("rhold", radarSettings.noTargetDelaySeconds);
    devicePreferences.putUChar("rtrigger", radarSettings.triggerCount);
    devicePreferences.putUChar("rsnr", radarSettings.snrThreshold);
    devicePreferences.putUChar("rprofile", RADAR_PROFILE_VERSION);
    return;
  }
  radarSettings.maximumRangeMeters = uint8_t(constrain(
      int(devicePreferences.getUChar("rrange", 75)), 10, 100));
  radarSettings.direction = uint8_t(constrain(
      int(devicePreferences.getUChar("rdir", 0)), 0, 2));
  radarSettings.minimumSpeedKmh = uint8_t(constrain(
      int(devicePreferences.getUChar("rminspd", 15)), 0, 120));
  radarSettings.noTargetDelaySeconds =
      devicePreferences.getUChar("rhold", 1);
  radarSettings.triggerCount = uint8_t(constrain(
      int(devicePreferences.getUChar("rtrigger", 2)), 1, 10));
  const uint8_t snr = devicePreferences.getUChar("rsnr", 32);
  radarSettings.snrThreshold =
      (snr == 0 || (snr >= 3 && snr <= 32)) ? snr : 32;
}

void saveRadarSettings() {
  if (!deviceStorageReady) return;
  devicePreferences.putUChar("rrange", radarSettings.maximumRangeMeters);
  devicePreferences.putUChar("rdir", radarSettings.direction);
  devicePreferences.putUChar("rminspd", radarSettings.minimumSpeedKmh);
  devicePreferences.putUChar("rhold", radarSettings.noTargetDelaySeconds);
  devicePreferences.putUChar("rtrigger", radarSettings.triggerCount);
  devicePreferences.putUChar("rsnr", radarSettings.snrThreshold);
}

void adjustRadarSetting(uint8_t row, int delta) {
  if (!delta) return;
  if (row == 0) {
    const int currentFeet = radarRangeFeet(radarSettings.maximumRangeMeters);
    const int nextFeet = constrain(
        currentFeet + delta * RADAR_RANGE_STEP_FEET,
        RADAR_RANGE_MIN_FEET, RADAR_RANGE_MAX_FEET);
    if (nextFeet != currentFeet)
      radarSettings.maximumRangeMeters = radarRangeMetersFromFeet(nextFeet);
  } else if (row == 1) {
    radarSettings.direction = uint8_t(
        (int(radarSettings.direction) + delta + 3) % 3);
  } else if (row == 2) {
    radarSettings.minimumSpeedKmh = uint8_t(constrain(
        int(radarSettings.minimumSpeedKmh) + delta, 0, 120));
  } else if (row == 3) {
    radarSettings.noTargetDelaySeconds = uint8_t(constrain(
        int(radarSettings.noTargetDelaySeconds) + delta, 0, 255));
  } else if (row == 4) {
    radarSettings.triggerCount = uint8_t(constrain(
        int(radarSettings.triggerCount) + delta, 1, 10));
  } else if (row == 5) {
    if (radarSettings.snrThreshold == 0) {
      if (delta > 0) radarSettings.snrThreshold = 3;
    } else {
      const int next = int(radarSettings.snrThreshold) + delta;
      radarSettings.snrThreshold = next < 3
          ? 0 : uint8_t(constrain(next, 3, 32));
    }
  }
  radarClient.setSettings(radarSettings);
  radarSettingsDirty = true;
}

void drawDisplayPage(uint32_t now) {
  char percentage[12];
  snprintf(percentage, sizeof(percentage), "%d%%",
           (normalBrightness * 100 + 127) / 255);
  beginFrame();
  drawHeader(now);
  textCentered("BRIGHTNESS", CENTER, uy(83), 4, WHITE);
  canvas->drawRoundRect(ux(29), uy(116), us(182), us(22), us(5), WHITE);
  const int width = us(176) * normalBrightness / 255;
  if (width) canvas->fillRoundRect(ux(32), uy(119), width, us(16), us(3), VALUE_TEAL);
  textCentered(percentage, CENTER, uy(169), 3, WHITE);
  textCentered("Swipe brightness  -  Tap back", CENTER, uy(211), 1, WHITE);
  endFrame();
}

void drawCountdown(uint32_t now) {
  const uint32_t elapsed = min(uint32_t(3000), now - countdownStartMs);
  const int number = max(1, 3 - int(elapsed / 1000));
  const float remaining = 1.0f - elapsed / 3000.0f;
  const float sweep = 359.0f * remaining;
  // Use the full round face for the countdown. The cancel hint now lives
  // inside the ring instead of occupying a separate lower crescent.
  const int centerY = CENTER;
  const int outerRadius = us(116);
  const int innerRadius = us(103);
  const int middleRadius = (outerRadius + innerRadius) / 2;
  const int capRadius = max(2, (outerRadius - innerRadius) / 2);
  constexpr uint16_t countdownTrack = 0x0300;

  beginFrame();
  canvas->fillArc(CENTER, centerY, outerRadius, innerRadius,
                  0.0f, 359.0f, countdownTrack);

  // Anchor the contracting arc at twelve o'clock. Rounded caps closely match
  // the reference and leave a single bright dot as the countdown reaches 1.
  const float startAngle = 270.0f;
  if (sweep > 0.5f)
    canvas->fillArc(CENTER, centerY, outerRadius, innerRadius,
                    startAngle, startAngle + sweep, START_GREEN);
  const float endRadians = (startAngle + sweep) * PI / 180.0f;
  canvas->fillCircle(CENTER, centerY - middleRadius, capRadius, START_GREEN);
  if (sweep > 0.5f)
    canvas->fillCircle(CENTER + lroundf(cosf(endRadians) * middleRadius),
                       centerY + lroundf(sinf(endRadians) * middleRadius),
                       capRadius, START_GREEN);

  char text[4];
  snprintf(text, sizeof(text), "%d", number);
  canvas->setFont(&Arial_Bold92pt7b);
  canvas->setTextSize(1);
  canvas->setTextColor(WHITE);
  int16_t x1 = 0, y1 = 0;
  uint16_t width = 0, height = 0;
  canvas->getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  canvas->setCursor(CENTER - (x1 + int(width) / 2),
                    centerY - (y1 + int(height) / 2));
  canvas->print(text);
  textCentered("2s press to cancel", CENTER, uy(201), 1, WHITE);
  endAnimatedFrame();
}

void drawRideCanceled(uint32_t now) {
  beginFrame();
  drawHeader(now);
  textCentered("RIDE",CENTER,uy(91),5,WHITE);
  textCentered("CANCELED",CENTER,uy(126),5,WHITE);
  canvas->fillRoundRect(ux(76),uy(166),us(88),us(42),us(9),GREEN);
  textCentered("OK",CENTER,uy(187),4,BLACK);
  endFrame();
}

uint16_t gradeColor(float grade) {
  if (grade < -1.0f) return RGB565_CYAN;
  if (grade < 1.0f) return WHITE;
  if (grade < 4.0f) return GREEN;
  if (grade < 7.0f) return YELLOW;
  if (grade < 10.0f) return ORANGE;
  return RED;
}

uint16_t scaleRgb565(uint16_t color, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const uint16_t red = uint16_t(((color >> 11) & 0x1f) * amount + 0.5f);
  const uint16_t green = uint16_t(((color >> 5) & 0x3f) * amount + 0.5f);
  const uint16_t blue = uint16_t((color & 0x1f) * amount + 0.5f);
  return (red << 11) | (green << 5) | blue;
}

uint16_t blendRgb565(uint16_t first, uint16_t second, float amount) {
  amount = constrain(amount, 0.0f, 1.0f);
  const float inverse = 1.0f - amount;
  const uint16_t red = uint16_t(((first >> 11) & 0x1f) * inverse +
                                ((second >> 11) & 0x1f) * amount + 0.5f);
  const uint16_t green = uint16_t(((first >> 5) & 0x3f) * inverse +
                                  ((second >> 5) & 0x3f) * amount + 0.5f);
  const uint16_t blue = uint16_t((first & 0x1f) * inverse +
                                 (second & 0x1f) * amount + 0.5f);
  return (red << 11) | (green << 5) | blue;
}

uint16_t speedArcColor(float position) {
  position = constrain(position, 0.0f, 1.0f);
  if (position < 0.42f)
    return blendRgb565(GREEN, YELLOW, position / 0.42f);
  if (position < 0.74f)
    return blendRgb565(YELLOW, ORANGE, (position - 0.42f) / 0.32f);
  return blendRgb565(ORANGE, RED, (position - 0.74f) / 0.26f);
}

enum RadarWarningLevel : uint8_t {
  RADAR_WARNING_NONE,
  RADAR_WARNING_YELLOW,
  RADAR_WARNING_ORANGE,
  RADAR_WARNING_RED
};

RadarWarningLevel radarWarningLevel = RADAR_WARNING_NONE;
uint8_t radarWarningCandidate = RADAR_WARNING_NONE;
uint8_t radarWarningCandidateReports = 0;
uint32_t radarWarningCandidateMs = 0;
uint32_t radarWarningLastSeenMs[4] = {};
uint32_t radarWarningLastRevision = 0;

uint8_t radarTargetWarningLevel(const RadarClient::Target &target) {
  if (!target.approaching || target.closingSpeedKmh <
      int8_t(radarSettings.minimumSpeedKmh)) return RADAR_WARNING_NONE;
  if (target.distanceMeters > radarSettings.maximumRangeMeters)
    return RADAR_WARNING_NONE;
  const float closingMph = target.closingSpeedKmh * 0.621371f;
  const float distanceFeet = target.distanceMeters * 3.28084f;
  const float closingFeetPerSecond = closingMph * 1.46667f;
  const float ttc = closingFeetPerSecond > 0.2f
      ? distanceFeet / closingFeetPerSecond : 999.0f;
  if (ttc <= 5.0f || closingMph >= 20.0f) return RADAR_WARNING_RED;
  if (ttc <= 10.0f || closingMph >= 10.0f) return RADAR_WARNING_ORANGE;
  return RADAR_WARNING_YELLOW;
}

uint32_t radarWarningHoldMs(uint8_t level) {
  if (level == RADAR_WARNING_RED) return 1250;
  if (level == RADAR_WARNING_ORANGE) return 1500;
  return 1750;
}

void updateRadarWarningState(uint32_t now) {
  const RadarWarningLevel previous = radarWarningLevel;
  const uint32_t revision = radarClient.targetRevision();
  if (!radarEnabled || radarClient.state() != RadarClient::STATE_CONNECTED) {
    radarWarningLevel = RADAR_WARNING_NONE;
    radarWarningCandidate = RADAR_WARNING_NONE;
    radarWarningCandidateReports = 0;
    memset(radarWarningLastSeenMs, 0, sizeof(radarWarningLastSeenMs));
    radarWarningLastRevision = revision;
    if (previous != radarWarningLevel) previousDrawMs = 0;
    return;
  }

  if (revision != radarWarningLastRevision) {
    radarWarningLastRevision = revision;
    uint8_t rawLevel = RADAR_WARNING_NONE;
    // Moving-away targets remain available when BOTH/AWAY is selected, but
    // only an approaching target can raise a rider warning.
    if (radarSettings.direction != 1) {
      RadarClient::Target targets[RadarClient::MAX_TARGETS] = {};
      const size_t count = radarClient.snapshot(
          targets, RadarClient::MAX_TARGETS, now);
      for (size_t index = 0; index < count; ++index)
        rawLevel = max(rawLevel, radarTargetWarningLevel(targets[index]));
    }

    if (rawLevel) {
      for (uint8_t level = RADAR_WARNING_YELLOW; level <= rawLevel; ++level)
        radarWarningLastSeenMs[level] = now;
    }

    if (rawLevel > radarWarningLevel) {
      const bool consecutive = radarWarningCandidate == rawLevel &&
          uint32_t(now - radarWarningCandidateMs) <= 500;
      radarWarningCandidate = rawLevel;
      radarWarningCandidateReports = consecutive
          ? uint8_t(radarWarningCandidateReports + 1) : 1;
      radarWarningCandidateMs = now;
      if (radarWarningCandidateReports >= 2) {
        radarWarningLevel = RadarWarningLevel(rawLevel);
        radarWarningCandidate = RADAR_WARNING_NONE;
        radarWarningCandidateReports = 0;
      }
    } else {
      radarWarningCandidate = RADAR_WARNING_NONE;
      radarWarningCandidateReports = 0;
    }
  } else if (radarWarningCandidateReports &&
             uint32_t(now - radarWarningCandidateMs) > 500) {
    radarWarningCandidate = RADAR_WARNING_NONE;
    radarWarningCandidateReports = 0;
  }

  if (radarWarningLevel != RADAR_WARNING_NONE &&
      uint32_t(now - radarWarningLastSeenMs[radarWarningLevel]) >=
          radarWarningHoldMs(radarWarningLevel)) {
    RadarWarningLevel retained = RADAR_WARNING_NONE;
    for (int level = int(radarWarningLevel) - 1;
         level >= int(RADAR_WARNING_YELLOW); --level) {
      if (uint32_t(now - radarWarningLastSeenMs[level]) <
          radarWarningHoldMs(uint8_t(level))) {
        retained = RadarWarningLevel(level);
        break;
      }
    }
    radarWarningLevel = retained;
  }

  if (previous != radarWarningLevel) previousDrawMs = 0;
}

void drawRadarWarningRing(uint32_t now) {
  if ((appState != RIDING && appState != RADAR_PREVIEW) ||
      radarWarningLevel == RADAR_WARNING_NONE) return;

  constexpr int outerRadius = SCREEN_SIZE / 2 - 2;
  int layers = 42;
  float intensity = 1.0f;
  uint16_t color = YELLOW;
  if (radarWarningLevel == RADAR_WARNING_YELLOW) {
    const float phase = (now % 2400) * (2.0f * PI / 2400.0f);
    intensity = 0.35f + 0.65f * (0.5f + 0.5f * sinf(phase));
  } else if (radarWarningLevel == RADAR_WARNING_ORANGE) {
    layers = 50;
    intensity = 0.92f;
    color = ORANGE;
  } else {
    layers = 60;
    // A 500 ms period is two complete flashes per second. Keep a faint ring
    // during the off half-cycle so the warning never disappears completely.
    intensity = (now % 500) < 250 ? 1.0f : 0.10f;
    color = RED;
  }

  for (int layer = 0; layer < layers; ++layer) {
    const float inwardFade = 1.0f - float(layer) / float(layers - 1);
    const float strength = intensity * inwardFade * inwardFade;
    canvas->drawCircle(CENTER, CENTER, outerRadius - layer,
                       scaleRgb565(color, strength));
  }
}

void drawLargeValue(const char *value, const char *label,
                    uint16_t color = WHITE) {
  // Use a font rasterized near its final display size instead of enlarging a
  // small bitmap. This preserves far finer curves on the 466-pixel AMOLED.
  canvas->setFont(&Arial_Bold92pt7b);
  canvas->setTextSize(1);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  canvas->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  if (w > 452 || h > 270) {
    canvas->setFont(&Arial_Bold72pt7b);
    canvas->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  }
  canvas->setTextColor(color);
  canvas->setCursor(CENTER - (x1 + int(w) / 2),
                    CENTER - us(10) - (y1 + int(h) / 2));
  canvas->print(value);
  textCentered(label, CENTER, CENTER + us(92), 4, WHITE);
}

void drawRideStat(const char *value, const char *label, int x) {
  textCentered(value, x, uy(196), 2, WHITE);
  textCentered(label, x, uy(216), 2, 0x8410);
}

int visibleRidePages(int pages[MAX_RIDE_PAGE_COUNT]) {
  int count=0;
  pages[count++]=0;
  pages[count++]=1;
  pages[count++]=gpx::PAGE_INDEX;
  if(radarEnabled)pages[count++]=RADAR_PAGE_INDEX;
  return count;
}

int visibleRidePageCount() {
  int pages[MAX_RIDE_PAGE_COUNT];
  return visibleRidePages(pages);
}

int visibleRidePagePosition(int page) {
  int pages[MAX_RIDE_PAGE_COUNT];
  const int count=visibleRidePages(pages);
  for(int i=0;i<count;++i)if(pages[i]==page)return i;
  return 0;
}

void drawRidePageDots() {
  int pages[MAX_RIDE_PAGE_COUNT];
  const int pageCount=visibleRidePages(pages);
  for (int i = 0; i < pageCount; ++i)
    canvas->fillCircle(CENTER + us((2 * i - (pageCount - 1)) * 5),
                       uy(232), us(2.5f),
                       currentPage == pages[i] ? GREEN : 0x4208);
}

void drawLiveMetric(const char *value, const char *name, const char *unit,
                    uint16_t color = WHITE) {
  textCentered(name, CENTER, uy(61), 2, 0x8410);

  canvas->setFont(&Arial_Bold92pt7b);
  canvas->setTextSize(1);
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  canvas->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  if (w > 448 || h > 244) {
    canvas->setFont(&Arial_Bold72pt7b);
    canvas->getTextBounds(value, 0, 0, &x1, &y1, &w, &h);
  }
  canvas->setTextColor(color);
  canvas->setCursor(CENTER - (x1 + int(w) / 2),
                    uy(122) - (y1 + int(h) / 2));
  canvas->print(value);
  textCentered(unit, CENTER, uy(166), 2, color);
}

void drawLiveSpeedMetric(float speed) {
  char value[12];
  snprintf(value, sizeof(value), "%d", int(roundf(speed)));
  drawLiveMetric(value, "CURRENT SPEED", "MPH");
}

int catX(int centerX, int localX, bool right) { return right ? centerX + localX : centerX - localX; }
void catLine(int cx, int cy, int x1, int y1, int x2, int y2, bool right) {
  wideLine(catX(cx, us(x1), right), cy + us(y1),
           catX(cx, us(x2), right), cy + us(y2), us(2), WHITE);
}

void drawCat(uint32_t now, float speed) {
  uint32_t cycle = speed < 5 ? 10000 : (speed > 20 ? 1800 : 4000);
  const uint32_t phase = now % cycle;
  const bool right = phase < cycle / 2;
  const float p = right ? phase / float(cycle / 2) : (cycle - phase) / float(cycle / 2);
  const int x = ux(32 + int(p * 176));
  const int y = uy(174);
  const int frame = (now / 100) % 4;
  const int lift = frame == 2 ? -us(2) : (frame == 0 ? us(1) : 0);
  catLine(x, y + lift, -7, -1, -14, -5, right);
  catLine(x, y + lift, -14, -5, -17, -11, right);
  catLine(x, y + lift, -5, 3, frame & 1 ? -12 : -1, 8, right);
  catLine(x, y + lift, 5, 3, frame & 1 ? 12 : 7, 8, right);
  canvas->fillEllipse(x, y + lift, us(9), us(4), WHITE);
  const int hx = catX(x, us(10), right);
  canvas->fillCircle(hx, y - us(3) + lift, us(4), WHITE);
  canvas->fillTriangle(catX(x, us(7), right), y - us(6) + lift,
                       catX(x, us(8), right), y - us(11) + lift,
                       catX(x, us(11), right), y - us(7) + lift, WHITE);
}

void drawSittingCat(uint32_t now) {
  const int x = CENTER, y = uy(177);
  const int direction = ((now / 1250) % 2) ? -1 : 1;
  wideLine(x - us(5), y + us(5), x - us(14), y + us(8), us(2), WHITE);
  canvas->fillEllipse(x, y, us(7), us(11), WHITE);
  canvas->fillCircle(x - us(5), y + us(7), us(7), WHITE);
  const int hx = x + direction * us(3), hy = y - us(12);
  canvas->fillCircle(hx, hy, us(7), WHITE);
  canvas->fillTriangle(hx - us(6), hy - us(4), hx - us(5), hy - us(11), hx - us(1), hy - us(6), WHITE);
  canvas->fillTriangle(hx + us(1), hy - us(6), hx + us(5), hy - us(11), hx + us(6), hy - us(4), WHITE);
}

void drawCatActivity(uint32_t now) {
  if (currentSpeedMph > 0.1f) { stationarySinceMs = 0; drawCat(now, currentSpeedMph); return; }
  if (!stationarySinceMs) stationarySinceMs = now;
  const uint32_t stopped = now - stationarySinceMs;
  if (stopped < 10000) drawCat(now, 0);
  else if (((stopped - 10000) % 15000) < 5000) drawSittingCat(now);
  else drawCat(now, 2);
}

void drawThreeMetricRidePage(uint32_t seconds) {
  drawLiveSpeedMetric(currentSpeedMph);
  canvas->drawFastHLine(ux(60), uy(173), ux(120), GREEN);
  canvas->drawFastHLine(ux(34), uy(181), ux(172), 0x4208);
  canvas->drawFastVLine(CENTER, uy(186), uy(39), 0x4208);

  char distanceText[16];
  char timeText[16];
  snprintf(distanceText, sizeof(distanceText), distanceMiles<9.995f ? "%.2f" : "%.1f", distanceMiles);
  snprintf(timeText, sizeof(timeText), "%02lu:%02lu:%02lu",
           (unsigned long)(seconds / 3600),
           (unsigned long)((seconds / 60) % 60),
           (unsigned long)(seconds % 60));

  textCentered("DISTANCE", ux(72), uy(190), 1, 0x8410);
  textCentered(distanceText, ux(72), uy(208), 5, WHITE);
  textCentered("MI", ux(72), uy(222), 1, 0x8410);

  textCentered("TIME", ux(160), uy(190), 1, 0x8410);
  textCentered(timeText, ux(160), uy(210), 3, WHITE);
}

void fillSpeedArcSegment(int centerX, int centerY, int innerRadius,
                         int outerRadius, float startDegrees,
                         float endDegrees, uint16_t color) {
  const float start = startDegrees * PI / 180.0f;
  const float end = endDegrees * PI / 180.0f;
  const int outerStartX = centerX + int(roundf(cosf(start) * outerRadius));
  const int outerStartY = centerY + int(roundf(sinf(start) * outerRadius));
  const int outerEndX = centerX + int(roundf(cosf(end) * outerRadius));
  const int outerEndY = centerY + int(roundf(sinf(end) * outerRadius));
  const int innerStartX = centerX + int(roundf(cosf(start) * innerRadius));
  const int innerStartY = centerY + int(roundf(sinf(start) * innerRadius));
  const int innerEndX = centerX + int(roundf(cosf(end) * innerRadius));
  const int innerEndY = centerY + int(roundf(sinf(end) * innerRadius));
  canvas->fillTriangle(outerStartX, outerStartY, outerEndX, outerEndY,
                       innerStartX, innerStartY, color);
  canvas->fillTriangle(outerEndX, outerEndY, innerEndX, innerEndY,
                       innerStartX, innerStartY, color);
}

void drawAnimatedSpeedArcPage(uint32_t now, uint32_t seconds,
                              float routeProgress = -1.0f) {
  constexpr float maximumSpeed = 25.0f;
  constexpr float startDegrees = 160.0f;
  constexpr float sweepDegrees = 220.0f;
  constexpr int segments = 44;
  const int centerY = uy(137);
  const int innerRadius = us(78);
  const int outerRadius = us(93);
  const float safeSpeedMph = isfinite(currentSpeedMph)
      ? max(0.0f, currentSpeedMph) : 0.0f;
  const float safeMaximumSpeedMph = isfinite(rideMaxSpeedMph)
      ? max(0.0f, rideMaxSpeedMph) : 0.0f;
  const float speedPosition = constrain(safeSpeedMph / maximumSpeed,
                                        0.0f, 1.0f);
  const float maximumPosition = constrain(safeMaximumSpeedMph / maximumSpeed,
                                          0.0f, 1.0f);

  for (int i = 0; i < segments; ++i) {
    const float startPosition = float(i) / segments;
    const float endPosition = float(i + 1) / segments;
    const float middle = (startPosition + endPosition) * 0.5f;
    if (startPosition >= speedPosition) continue;
    const float filledEnd = min(endPosition, speedPosition);
    const uint16_t color = speedArcColor(middle);
    fillSpeedArcSegment(CENTER, centerY, innerRadius, outerRadius,
                        startDegrees + sweepDegrees * startPosition,
                        startDegrees + sweepDegrees * filledEnd + 0.35f,
                        color);
  }

  // Six fixed ticks establish the 0-25 mph scale without adding label clutter.
  for (int tick = 0; tick <= 5; ++tick) {
    const float angle = (startDegrees + sweepDegrees * tick / 5.0f) *
                        PI / 180.0f;
    const int x1 = CENTER + int(roundf(cosf(angle) * (innerRadius - us(2))));
    const int y1 = centerY + int(roundf(sinf(angle) * (innerRadius - us(2))));
    const int x2 = CENTER + int(roundf(cosf(angle) * (innerRadius + us(5))));
    const int y2 = centerY + int(roundf(sinf(angle) * (innerRadius + us(5))));
    wideLine(x1, y1, x2, y2, us(1), WHITE);
  }

  const float markerPulse = 1.8f +
      0.7f * (0.5f + 0.5f * sinf((now % 1200) * 2.0f * PI / 1200.0f));
  const float markerAngle = startDegrees + sweepDegrees * maximumPosition;
  fillSpeedArcSegment(CENTER, centerY, innerRadius - us(3),
                      outerRadius + us(3), markerAngle - markerPulse,
                      markerAngle + markerPulse, WHITE);

  char speedText[12];
  char distanceText[16];
  char timeText[16];
  const float displaySpeed=safeSpeedMph;
  const float displayDistance=isfinite(distanceMiles)
      ? max(0.0f,distanceMiles) : 0.0f;
  snprintf(speedText, sizeof(speedText), "%d", int(roundf(displaySpeed)));
  snprintf(distanceText, sizeof(distanceText),
           displayDistance<9.995f ? "%.2f" : "%.1f", displayDistance);
  if(seconds<3600) {
    snprintf(timeText,sizeof(timeText),"%02lu:%02lu",
             (unsigned long)(seconds/60),(unsigned long)(seconds%60));
  } else {
    snprintf(timeText,sizeof(timeText),"%02lu:%02lu:%02lu",
             (unsigned long)(seconds/3600),(unsigned long)((seconds/60)%60),
             (unsigned long)(seconds%60));
  }

  canvas->setFont(&Arial_Bold72pt7b);
  canvas->setTextSize(1);
  canvas->setTextColor(WHITE);
  int16_t speedX,speedY;uint16_t speedW,speedH;
  canvas->getTextBounds(speedText,0,0,&speedX,&speedY,&speedW,&speedH);
  canvas->setCursor(CENTER-(speedX+int(speedW)/2),uy(121)-(speedY+int(speedH)/2));
  canvas->print(speedText);
  textCentered("MPH", CENTER, uy(165), 2, WHITE);
  textCentered("0", ux(30), uy(173), 1, WHITE);
  textCentered("25", ux(210), uy(173), 1, WHITE);

  const int horizonLeft = ux(34);
  const int horizonRight = ux(206);
  const int horizonY = uy(181);
  if (routeProgress < 0.0f) {
    // Preserve the original horizon during Free Ride.
    canvas->drawFastHLine(horizonLeft, horizonY,
                          horizonRight - horizonLeft, 0x4208);
  } else {
    // Reference colors sampled from the supplied artwork and quantized to the
    // display's native RGB565 format.
    constexpr uint16_t routeStartBlue = 0x1A6F;  // #1F4D7C
    constexpr uint16_t routeCyan = 0x6F19;
    constexpr uint16_t routeRemaining = 0xD6BA;  // #D5D5D5
    const int railThickness = us(2);
    const int railTop = horizonY - railThickness / 2;
    const float progress = constrain(routeProgress, 0.0f, 1.0f);
    const int markerX = horizonLeft +
        lroundf((horizonRight - horizonLeft) * progress);

    canvas->fillRect(horizonLeft, railTop,
                     horizonRight - horizonLeft, railThickness,
                     routeRemaining);
    const int completedWidth = markerX - horizonLeft;
    for (int x = horizonLeft; x <= markerX; ++x) {
      const float amount = completedWidth > 0
          ? float(x - horizonLeft) / completedWidth : 1.0f;
      const uint16_t red = uint16_t((routeStartBlue >> 11) + lroundf(
          (((routeCyan >> 11) & 0x1F) - (routeStartBlue >> 11)) * amount));
      const uint16_t green = uint16_t(((routeStartBlue >> 5) & 0x3F) + lroundf(
          (((routeCyan >> 5) & 0x3F) - ((routeStartBlue >> 5) & 0x3F)) * amount));
      const uint16_t blue = uint16_t((routeStartBlue & 0x1F) + lroundf(
          ((routeCyan & 0x1F) - (routeStartBlue & 0x1F)) * amount));
      const uint16_t color = uint16_t((red << 11) | (green << 5) | blue);
      canvas->drawFastVLine(x, railTop, railThickness, color);
    }

    // Solid left-pointing teardrop matching the reference slider marker.
    const int markerRadius = us(6);
    canvas->fillTriangle(markerX - us(10), horizonY,
                         markerX, horizonY - markerRadius,
                         markerX, horizonY + markerRadius, routeCyan);
    canvas->fillCircle(markerX, horizonY, markerRadius, routeCyan);
  }
  canvas->drawFastVLine(CENTER, uy(186), uy(39), 0x4208);
  textCentered("DIST", ux(72), uy(190), 1, 0x8410);
  textCentered(distanceText, ux(72), uy(210), 5, WHITE);
  textCentered("TIME", ux(160), uy(190), 1, 0x8410);
  textCentered(timeText, ux(160), uy(210), 3, WHITE);
}

void drawPausedBadge() {
  if (!ridePaused) return;
  const int left=rideAutoPaused ? ux(60) : ux(73);
  const int width=rideAutoPaused ? us(120) : us(94);
  canvas->fillRoundRect(left, uy(49), width, us(23), us(6), BLACK);
  canvas->drawRoundRect(left, uy(49), width, us(23), us(6), YELLOW);
  textCentered(rideAutoPaused ? "AUTO PAUSED" : "PAUSED",
               CENTER, uy(61), rideAutoPaused ? 1 : 2, YELLOW);
}

#include "shared_map_display.inc"
#include "gpx_display.inc"
#include "pq_map_display.inc"
#include "radar_display.inc"

void drawRidePage(uint32_t now) {
  if (!radarEnabled && currentPage == RADAR_PAGE_INDEX) currentPage = 0;
  if (!ridarEnabled && currentPage == RIDAR_PAGE_INDEX) currentPage = 0;
  if (currentPage == gpx::PAGE_INDEX) {
    if (routeNavigationEnabled) drawLiveGpxPage(now);
    else drawFreeRideMapPage(now);
    return;
  }
  if (currentPage == RADAR_PAGE_INDEX) {
    drawRadarPage(now);
    return;
  }
  if (currentPage == RIDAR_PAGE_INDEX) {
    drawRidarPage(now);
    return;
  }
  const uint32_t seconds = activeRideElapsedMs(now) / 1000;
  beginFrame();
  drawHeader(now);
  if (currentPage == 0) {
    const float routeProgress = routeNavigationEnabled
        ? constrain(max(0.0f, gpxLastProgress) / max(1.0f, gpx::length()),
                    0.0f, 1.0f)
        : -1.0f;
    drawAnimatedSpeedArcPage(now, seconds, routeProgress);
    if(routeNavigationEnabled && !gpxArrived &&
      gpxGuidance.mode()==gpx::OFF_COURSE) {
      if(!gpxOffCourseSinceMs) gpxOffCourseSinceMs=now;
      constexpr uint16_t offCourseColor=0xF81F;
      const bool initialFlash=now-gpxOffCourseSinceMs<OFF_COURSE_FLASH_MS;
      if(initialFlash && (now%1000)<500) {
        canvas->fillRect(ux(48),uy(65),us(144),us(26),offCourseColor);
        textCentered("OFF COURSE",CENTER,uy(78),3,WHITE);
      } else if(!initialFlash) {
        textCentered("OFF COURSE",CENTER,uy(76),2,offCourseColor);
      }
    }
  } else {
    // Three large, vertically spaced values fit inside the round display.
    char value[24];
    textCentered("DISTANCE", CENTER, uy(65), 2, 0xAD55);
    if (isfinite(distanceMiles))
      snprintf(value, sizeof(value), distanceMiles<9.995f ? "%.2f MI" : "%.1f MI", distanceMiles);
    else snprintf(value, sizeof(value), "--");
    textCentered(value, CENTER, uy(87), 5, WHITE);
    canvas->drawFastHLine(ux(53), uy(106), us(134), 0x4208);
    textCentered("AVG SPEED", CENTER, uy(122), 2, 0xAD55);
    if (isfinite(averageSpeedMph)) snprintf(value, sizeof(value), "%.1f MPH", averageSpeedMph);
    else snprintf(value, sizeof(value), "--");
    textCentered(value, CENTER, uy(144), 5, WHITE);
    canvas->drawFastHLine(ux(53), uy(163), us(134), 0x4208);
    textCentered("CLIMB", CENTER, uy(180), 2, 0xAD55);
    if (isfinite(climbFeet)) snprintf(value, sizeof(value), "%.0f FT", climbFeet);
    else snprintf(value, sizeof(value), "--");
    textCentered(value, CENTER, uy(203), 5, WHITE);
  }
  drawRidePageDots();
  drawPausedBadge();
  endAnimatedFrame();
}

// Keep a complete map snapshot in PSRAM while another ride page is visible.
// The tap can present this already-rasterized frame immediately; normal live
// rendering replaces it on the next map update.
constexpr size_t MAP_PREFETCH_BYTES =
    size_t(SCREEN_SIZE) * size_t(SCREEN_SIZE) * sizeof(uint16_t);
uint16_t *mapPrefetchFrame = nullptr;
uint32_t mapPrefetchMs = 0;
uint32_t mapPrefetchFingerprint = 0;
uint32_t mapPrefetchRevision = 0;
bool mapPrefetchRouteMode = false;
bool mapPrefetchValid = false;
uint32_t lastRidePageChangeMs = 0;

bool allocateMapPrefetchFrame() {
  if (mapPrefetchFrame) return true;
  mapPrefetchFrame = static_cast<uint16_t *>(ps_malloc(MAP_PREFETCH_BYTES));
  return mapPrefetchFrame != nullptr;
}

void captureDisplayedMapFrame(uint32_t now) {
  // The framebuffer already contains the complete map that is visible. Save
  // it before changing pages instead of rerasterizing the same large vector
  // map synchronously while the rider is waiting for the next page.
  if (!allocateMapPrefetchFrame()) return;
  memcpy(mapPrefetchFrame, canvas->getFramebuffer(), MAP_PREFETCH_BYTES);
  mapPrefetchMs = now;
  mapPrefetchFingerprint = sharedMap.fingerprint();
  mapPrefetchRevision = sharedMap.revision();
  mapPrefetchRouteMode = routeNavigationEnabled;
  mapPrefetchValid = true;
}

void prepareMapFrame(uint32_t now) {
  if (appState != RIDING || currentPage == gpx::PAGE_INDEX ||
      currentPage == RADAR_PAGE_INDEX || currentPage == RIDAR_PAGE_INDEX ||
      touchPending || touchActive || otaUpdate.active() || sharedMap.active() ||
      gpxLibrary.active() || now - lastRidePageChangeMs < 500 ||
      (mapPrefetchMs && now - mapPrefetchMs < 1000))
    return;
  if (!allocateMapPrefetchFrame()) return;

  boostCpuForMapRender();
  suppressedFrameCompleted = false;
  suppressFrameFlush = true;
  if (routeNavigationEnabled) drawLiveGpxPage(now);
  else drawFreeRideMapPage(now);
  suppressFrameFlush = false;
  mapPrefetchMs = now;
  if (!suppressedFrameCompleted || touchPending || touchActive) return;

  memcpy(mapPrefetchFrame, canvas->getFramebuffer(), MAP_PREFETCH_BYTES);
  mapPrefetchFingerprint = sharedMap.fingerprint();
  mapPrefetchRevision = sharedMap.revision();
  mapPrefetchRouteMode = routeNavigationEnabled;
  mapPrefetchValid = true;
}

bool presentPreparedMapFrame(uint32_t now) {
  if (!mapPrefetchValid || !mapPrefetchFrame ||
      now - mapPrefetchMs > 1500 ||
      mapPrefetchFingerprint != sharedMap.fingerprint() ||
      mapPrefetchRevision != sharedMap.revision() ||
      mapPrefetchRouteMode != routeNavigationEnabled)
    return false;
  boostCpuForMapRender();
  memcpy(canvas->getFramebuffer(), mapPrefetchFrame, MAP_PREFETCH_BYTES);
  drawRadarWarningRing(now);
  canvas->flush();
  hasFramebufferHash = false;
  previousDrawMs = now;
  return true;
}

uint32_t rideFrameIntervalMs() {
  if (radarWarningLevel == RADAR_WARNING_RED ||
      radarWarningLevel == RADAR_WARNING_YELLOW) return 125;
  if (ridePaused) return 1000;
  if (currentPage == RADAR_PAGE_INDEX)
    return radarClient.state() == RadarClient::STATE_CONNECTED ? 100 : 500;
  if (currentPage == RIDAR_PAGE_INDEX)
    return ridarMapZoomAnimating ? 83 : 500;
  if (currentPage == gpx::PAGE_INDEX)
    return routeNavigationEnabled ? gpxMapFrameIntervalMs()
                                  : freeRideMapFrameIntervalMs();
  if (currentPage == 0)
    return currentSpeedMph > 0.5f ? 125 : 500;
  return currentSpeedMph > 0.5f ? 250 : 1000;
}

void loadSummaryRoute() {
  summaryRouteLoaded = true;
  summaryRoutePointCount = 0;
  if (!rideStorageReady || !gpsSampleCount || !summaryGpsPath[0]) return;

  File file = FFat.open(summaryGpsPath, FILE_READ);
  if (!file || !file.seek(summaryGpsOffset)) {
    if (file) file.close();
    return;
  }

  GpsTrackPoint point = {};
  int32_t originLatE7 = 0, originLonE7 = 0;
  bool haveOrigin = false;
  double minimumX = 0, maximumX = 0, minimumY = 0, maximumY = 0;
  double longitudeScale = 1.0;
  uint16_t validCount = 0;
  for (uint16_t i = 0; i < gpsSampleCount; ++i) {
    if (file.read(reinterpret_cast<uint8_t *>(&point), sizeof(point)) != sizeof(point)) break;
    if ((!point.latitudeE7 && !point.longitudeE7) ||
        abs(point.latitudeE7) > 900000000L || abs(point.longitudeE7) > 1800000000L) continue;
    if (!haveOrigin) {
      originLatE7 = point.latitudeE7;
      originLonE7 = point.longitudeE7;
      longitudeScale = cos(originLatE7 * 1.0e-7 * PI / 180.0);
      haveOrigin = true;
    }
    const double x = (point.longitudeE7 - originLonE7) * 1.0e-7 *
                     111320.0 * longitudeScale;
    const double y = (point.latitudeE7 - originLatE7) * 1.0e-7 * 111320.0;
    if (!validCount) minimumX = maximumX = x, minimumY = maximumY = y;
    else {
      minimumX = min(minimumX, x); maximumX = max(maximumX, x);
      minimumY = min(minimumY, y); maximumY = max(maximumY, y);
    }
    ++validCount;
  }
  file.close();
  if (!haveOrigin || !validCount) return;

  const int left = ux(31), right = ux(209), top = uy(82), bottom = uy(186);
  const double spanX = max(1.0, maximumX - minimumX);
  const double spanY = max(1.0, maximumY - minimumY);
  const double scale = min((right - left) / spanX, (bottom - top) / spanY);
  const double usedWidth = spanX * scale;
  const double usedHeight = spanY * scale;
  const double offsetX = left + ((right - left) - usedWidth) * 0.5;
  const double offsetY = top + ((bottom - top) - usedHeight) * 0.5;

  file = FFat.open(summaryGpsPath, FILE_READ);
  if (!file || !file.seek(summaryGpsOffset)) {
    if (file) file.close();
    return;
  }
  for (uint16_t i = 0; i < gpsSampleCount && summaryRoutePointCount < MAX_GPS_SAMPLES; ++i) {
    if (file.read(reinterpret_cast<uint8_t *>(&point), sizeof(point)) != sizeof(point)) break;
    if ((!point.latitudeE7 && !point.longitudeE7) ||
        abs(point.latitudeE7) > 900000000L || abs(point.longitudeE7) > 1800000000L) continue;
    const double x = (point.longitudeE7 - originLonE7) * 1.0e-7 *
                     111320.0 * longitudeScale;
    const double y = (point.latitudeE7 - originLatE7) * 1.0e-7 * 111320.0;
    const int screenX = lround(offsetX + (x - minimumX) * scale);
    const int screenY = lround(offsetY + usedHeight - (y - minimumY) * scale);
    if (summaryRoutePointCount) {
      const RouteScreenPoint &previous = summaryRoutePoints[summaryRoutePointCount - 1];
      if (abs(screenX - previous.x) < 2 && abs(screenY - previous.y) < 2) continue;
    }
    summaryRoutePoints[summaryRoutePointCount++] = {
        int16_t(screenX), int16_t(screenY)};
  }
  file.close();
}

void drawSummaryRoute() {
  if (!summaryRouteLoaded) loadSummaryRoute();
  if (!summaryRoutePointCount) {
    textCentered("NO GPS TRACK", CENTER, uy(132), 3, 0x8410);
  } else if (summaryRoutePointCount == 1) {
    canvas->fillCircle(summaryRoutePoints[0].x, summaryRoutePoints[0].y,
                       us(4), GREEN);
  } else {
    for (uint16_t i = 1; i < summaryRoutePointCount; ++i)
      wideLine(summaryRoutePoints[i - 1].x, summaryRoutePoints[i - 1].y,
               summaryRoutePoints[i].x, summaryRoutePoints[i].y,
               max(1, us(2)), GREEN);
    canvas->fillCircle(summaryRoutePoints[0].x, summaryRoutePoints[0].y,
                       us(3), WHITE);
    const RouteScreenPoint &finish = summaryRoutePoints[summaryRoutePointCount - 1];
    canvas->fillCircle(finish.x, finish.y, us(5), WHITE);
    canvas->fillCircle(finish.x, finish.y, us(2.5f), GREEN);
  }
  canvas->drawRoundRect(ux(82), uy(198), us(76), us(28), us(6), WHITE);
  textCentered("DONE", CENTER, uy(212), 2, WHITE);
}

void drawSummaryDots() {
  for (int i = 0; i < 4; ++i)
    canvas->fillCircle(CENTER + us((i * 14) - 21), uy(232), us(4),
                       summaryPage == i ? GREEN : 0x4208);
}

void drawSummaryPlot(const float *samples, uint16_t count,
                     const char *unit, bool decimals, bool showDone) {
  char text[24];
  const float first = count ? samples[0] : 0.0f;
  float minimum = first;
  float maximum = first;
  for (uint16_t i = 1; i < count; ++i) {
    minimum = min(minimum, samples[i]);
    maximum = max(maximum, samples[i]);
  }
  const float minimumRange = decimals ? 2.0f : 10.0f;
  if (maximum - minimum < minimumRange) {
    const float middle = (maximum + minimum) * 0.5f;
    minimum = middle - minimumRange * 0.5f;
    maximum = middle + minimumRange * 0.5f;
  }
  const int left = ux(28), right = ux(212);
  const int top = uy(91), bottom = uy(181);
  canvas->drawFastHLine(left, top + (bottom - top) / 3, right - left, 0x2104);
  canvas->drawFastHLine(left, top + 2 * (bottom - top) / 3, right - left, 0x2104);
  int previousX = left;
  int previousY = bottom - int((first - minimum) *
                                (bottom - top) / (maximum - minimum));
  for (uint16_t i = 1; i < count; ++i) {
    const int x = left + int((right - left) * i / max(1, int(count - 1)));
    const int y = bottom - int((samples[i] - minimum) *
                                (bottom - top) / (maximum - minimum));
    wideLine(previousX, previousY, x, y, max(1, us(2)), GREEN);
    previousX = x;
    previousY = y;
  }
  if (count <= 1) wideLine(left, previousY, right, previousY, max(1, us(2)), GREEN);
  if (decimals) snprintf(text, sizeof(text), "%.1f %s", maximum, unit);
  else snprintf(text, sizeof(text), "%d %s", int(roundf(maximum)), unit);
  textCentered(text, ux(188), uy(87), 1, GREEN);
  if (decimals) snprintf(text, sizeof(text), "%.1f %s", minimum, unit);
  else snprintf(text, sizeof(text), "%d %s", int(roundf(minimum)), unit);
  textCentered(text, ux(51), uy(186), 1, 0x8410);
  if (showDone) {
    canvas->drawRoundRect(ux(82), uy(198), us(76), us(28), us(6), WHITE);
    textCentered("DONE", CENTER, uy(212), 2, WHITE);
  }
}

void drawSummary(uint32_t now) {
  char text[32];
  beginFrame();
  drawHeader(now);
  if (summaryPage == 0) textCentered("RIDE SUMMARY", CENTER, uy(67), 4, WHITE);
  else if (summaryPage == 1) textCentered("ELEVATION", CENTER, uy(67), 5, WHITE);
  else if (summaryPage == 2) textCentered("SPEED", CENTER, uy(67), 5, WHITE);
  else textCentered("ROUTE", CENTER, uy(67), 5, WHITE);
  if (summaryPage == 0) {
    snprintf(text, sizeof(text), "Time  %lu:%02lu", (unsigned long)(summaryRideSeconds / 3600), (unsigned long)((summaryRideSeconds / 60) % 60));
    textCentered(text, CENTER, uy(104), 3, WHITE);
    snprintf(text, sizeof(text), "Avg  %.1f mph", summaryAverageMph); textCentered(text, CENTER, uy(137), 3, WHITE);
    snprintf(text, sizeof(text), "Distance  %.1f mi", summaryDistanceMiles); textCentered(text, CENTER, uy(170), 3, WHITE);
    snprintf(text, sizeof(text), "Climb  %d ft", summaryClimbFeet); textCentered(text, CENTER, uy(203), 3, WHITE);
  } else if (summaryPage == 1)
    drawSummaryPlot(altitudeSamples, altitudeSampleCount, "ft", false, false);
  else if (summaryPage == 2)
    drawSummaryPlot(speedSamples, altitudeSampleCount, "mph", true, false);
  else
    drawSummaryRoute();
  drawSummaryDots();
  endFrame();
}

void wakeDisplay(uint32_t now) {
  lastActivityMs = now;
  zeroSpeedSinceMs = now;
  if (displayAutoDimmed) { displayAutoDimmed = false; display->setBrightness(normalBrightness); }
}

// Legacy callers now go straight to the full options menu. The old home
// START/MENU intermediary can no longer be entered.
void openMenu() {
  appState = OPTIONS_MENU;
  menuSelection = 0;
  powerSliderActive = false;
  powerSliderX = ux(90);
  previousDrawMs = 0;
}
void openHome() {
  appState = READY;
  menuSelection = 0;
  ridesEditMode = false;
  routeSelectionForStart = false;
  powerSliderActive = false;
  powerSliderX = ux(90);
  previousDrawMs = 0;
}
void openOptionsMenu() {
  appState = OPTIONS_MENU;
  // Return to the page that launched a submenu. openHome() resets this to
  // the first page for a fresh visit from the home screen.
  menuSelection = constrain(menuSelection, 0, 3);
  powerSliderActive = false;
  powerSliderX = ux(90);
  previousDrawMs = 0;
}
void openRideMenu() { appState = RIDE_MENU; menuSelection = 0; previousDrawMs = 0; }
void openDisplayPage(uint32_t now, AppState returnState = RIDE_MENU) {
  displayReturnState = returnState;
  appState = DISPLAY_PAGE;
  wakeDisplay(now);
  previousDrawMs = 0;
}
void startCountdown(uint32_t now) {
  appState = COUNTDOWN;
  countdownStartMs = now;
  previousDrawMs = 0;
  // The tap (or multi-touch hold) that selected the ride can remain reported
  // by the CST9217 after the destination screen appears.  Never let that old
  // contact become the countdown's two-second cancel hold: first require a
  // completely released frame, then pollTouch() applies its bounce lockout.
  touchActive = false;
  demoStartHoldActive = false;
  touchBlockedUntilRelease = true;
  ignoreTouchUntilMs = 0;
}

uint32_t activeRideElapsedMs(uint32_t now) {
  uint32_t elapsed = now - rideStartMs;
  if (ridePaused) {
    const uint32_t currentPauseMs = now - ridePausedAtMs;
    elapsed = elapsed > currentPauseMs ? elapsed - currentPauseMs : 0;
  }
  return elapsed;
}

void finishRidePause(uint32_t now) {
  if (!ridePaused) return;
  const uint32_t pausedMs = now - ridePausedAtMs;
  // Shift every absolute ride clock forward so the paused interval is absent
  // from duration, minute averages, GPS timestamps, and average speed.
  rideStartMs += pausedMs;
  minuteAccumulatorMs += pausedMs;
  nextAltitudeSampleMs += pausedMs;
  ridePreviousMs = now;
  nextGpsSampleMs = now;
  ridePausedAtMs = 0;
  ridePaused = false;
  rideAutoPaused = false;
  autoPauseStationarySinceMs = 0;
  autoResumeMovingSinceMs = 0;
  autoResumeLastLocationMs = 0;

  resetGpsSpeedFilter(now);
  stationarySinceMs = 0;
  zeroSpeedSinceMs = now;
  lastAltitudePacketMs = 0;
  gradeSampleHead = gradeSampleCount = 0;
  lastGradeSampleMs = 0;
  liveGradePercent = 0.0f;
  liveGradeValid = false;
  gradeMotionActive = false;
}

void beginRidePause(uint32_t now,bool automatic) {
  if(ridePaused)return;
  // Preserve the partial rolling minute up to the exact pause instant.
  accumulateMinuteAverages(now);
  ridePaused=true;
  rideAutoPaused=automatic;
  ridePausedAtMs=now;
  ridePreviousMs=now;
  resetGpsSpeedFilter(now);
  liveGradeValid=false;
  gradeMotionActive=false;
  autoPauseStationarySinceMs=0;
  autoResumeMovingSinceMs=0;
  autoResumeLastLocationMs=0;
  previousDrawMs=0;
}

void toggleRidePause(uint32_t now) {
  if (ridePaused) {
    finishRidePause(now);
  } else {
    beginRidePause(now,false);
  }
  appState = RIDING;
  previousDrawMs = 0;
}

void storeMinuteAverage() {
  if (!minuteWeightedMs || altitudeSampleCount >= MAX_ALTITUDE_SAMPLES) return;
  altitudeSamples[altitudeSampleCount] = altitudeMinuteWeightedSum / minuteWeightedMs;
  speedSamples[altitudeSampleCount] = speedMinuteWeightedSum / minuteWeightedMs;
  ++altitudeSampleCount;
  altitudeMinuteWeightedSum = 0.0f;
  speedMinuteWeightedSum = 0.0f;
  minuteWeightedMs = 0;
}

void accumulateMinuteAverages(uint32_t now) {
  while (minuteAccumulatorMs < now) {
    const uint32_t segmentEnd = min(now, nextAltitudeSampleMs);
    const uint32_t span = segmentEnd - minuteAccumulatorMs;
    const float relativeAltitude = altitudeFusionInitialized ? filteredElevationFt : 0.0f;
    altitudeMinuteWeightedSum += relativeAltitude * span;
    speedMinuteWeightedSum += currentSpeedMph * span;
    minuteWeightedMs += span;
    minuteAccumulatorMs = segmentEnd;
    if (minuteAccumulatorMs == nextAltitudeSampleMs) {
      storeMinuteAverage();
      nextAltitudeSampleMs += 60000;
    }
  }
}

bool calculateGradeRegression(uint8_t firstOffset, uint8_t sampleCount,
                              float &gradePercent) {
  if (sampleCount < 3 || firstOffset + sampleCount > gradeSampleCount) return false;

  const uint8_t firstIndex =
      (gradeSampleHead + GRADE_SAMPLE_CAPACITY - gradeSampleCount + firstOffset) %
      GRADE_SAMPLE_CAPACITY;
  const GradeSample &first = gradeSamples[firstIndex];
  float sumX = 0.0f;
  float sumY = 0.0f;
  float sumXX = 0.0f;
  float sumXY = 0.0f;
  for (uint8_t i = 0; i < sampleCount; ++i) {
    const uint8_t index = (firstIndex + i) % GRADE_SAMPLE_CAPACITY;
    const float x = gradeSamples[index].distanceFt - first.distanceFt;
    const float y = gradeSamples[index].elevationFt - first.elevationFt;
    sumX += x;
    sumY += y;
    sumXX += x * x;
    sumXY += x * y;
  }
  const float denominator = sampleCount * sumXX - sumX * sumX;
  if (fabsf(denominator) < 1.0f) return false;
  gradePercent = constrain(100.0f *
      (sampleCount * sumXY - sumX * sumY) / denominator, -30.0f, 30.0f);
  return true;
}

void updateLiveGrade(uint32_t now, float gradeElevationFt) {
  if (!altitudeFusionInitialized || !gradeMotionActive) {
    liveGradeValid = false;
    return;
  }
  if (lastGradeSampleMs && now - lastGradeSampleMs < 750) return;

  GradeSample &sample = gradeSamples[gradeSampleHead];
  sample.distanceFt = distanceMiles * 5280.0f;
  sample.elevationFt = gradeElevationFt;
  sample.timeMs = now;
  gradeSampleHead = (gradeSampleHead + 1) % GRADE_SAMPLE_CAPACITY;
  if (gradeSampleCount < GRADE_SAMPLE_CAPACITY) ++gradeSampleCount;
  lastGradeSampleMs = now;

  if (gradeSampleCount < 3) {
    liveGradeValid = false;
    return;
  }

  const uint8_t newestIndex =
      (gradeSampleHead + GRADE_SAMPLE_CAPACITY - 1) % GRADE_SAMPLE_CAPACITY;
  const GradeSample &newest = gradeSamples[newestIndex];
  uint8_t firstOffset = 0;
  while (firstOffset + 1 < gradeSampleCount) {
    const uint8_t index =
        (gradeSampleHead + GRADE_SAMPLE_CAPACITY - gradeSampleCount + firstOffset) %
        GRADE_SAMPLE_CAPACITY;
    const GradeSample &candidate = gradeSamples[index];
    if (newest.timeMs - candidate.timeMs <= GRADE_STABLE_WINDOW_MS) break;
    ++firstOffset;
  }

  const uint8_t usableCount = gradeSampleCount - firstOffset;
  const uint8_t firstIndex =
      (gradeSampleHead + GRADE_SAMPLE_CAPACITY - gradeSampleCount + firstOffset) %
      GRADE_SAMPLE_CAPACITY;
  const GradeSample &first = gradeSamples[firstIndex];
  // A time floor prevents a fast vehicle from compressing the calculation
  // into just a few noisy seconds. At cycling speed this normally represents
  // roughly 60-300 feet, while driving tests use a longer distance naturally.
  if (usableCount < 3 || newest.timeMs - first.timeMs < 8000 ||
      newest.distanceFt - first.distanceFt < 60.0f) {
    liveGradeValid = false;
    return;
  }

  float measuredGrade = 0.0f;
  if (!calculateGradeRegression(firstOffset, usableCount, measuredGrade)) {
    liveGradeValid = false;
    return;
  }

  // The long regression window makes the normal display steady, but it also
  // contains several seconds of uphill samples after a crest. Use a short
  // regression as a one-sided high-pass/crest detector: it may pull grade down
  // quickly, but cannot make a new climb appear more aggressively.
  uint8_t fastFirstOffset = gradeSampleCount - 1;
  while (fastFirstOffset > 0) {
    const uint8_t previousOffset = fastFirstOffset - 1;
    const uint8_t index =
        (gradeSampleHead + GRADE_SAMPLE_CAPACITY - gradeSampleCount +
         previousOffset) % GRADE_SAMPLE_CAPACITY;
    if (newest.timeMs - gradeSamples[index].timeMs > GRADE_FAST_WINDOW_MS) break;
    fastFirstOffset = previousOffset;
  }
  const uint8_t fastCount = gradeSampleCount - fastFirstOffset;
  const uint8_t fastFirstIndex =
      (gradeSampleHead + GRADE_SAMPLE_CAPACITY - gradeSampleCount +
       fastFirstOffset) % GRADE_SAMPLE_CAPACITY;
  const GradeSample &fastFirst = gradeSamples[fastFirstIndex];
  float fastGrade = measuredGrade;
  const bool fastValid = fastCount >= 3 &&
      newest.timeMs - fastFirst.timeMs >= 2000 &&
      newest.distanceFt - fastFirst.distanceFt >= 18.0f &&
      calculateGradeRegression(fastFirstOffset, fastCount, fastGrade);
  const bool crestDetected = fastValid &&
      fastGrade < measuredGrade - 1.0f &&
      (!liveGradeValid || fastGrade < liveGradePercent - 0.5f);
  const float targetGrade = crestDetected ? fastGrade : measuredGrade;
  const float response = crestDetected ? 0.65f : 0.25f;
  liveGradePercent = liveGradeValid
      ? liveGradePercent + response * (targetGrade - liveGradePercent)
      : targetGrade;
  liveGradeValid = true;
}

void startRide(uint32_t now) {
  gpxTurnPreview.reset();
  gpxPageAlert.reset();gpxArrived=false;
  gpxLastProgress=-1;gpxRecoveryProgress=0;gpxLiveBearing=NAN;
  gpxOffCourseStartRideMeters=-1;gpxOffCourseStartProgress=0;
  gpxOffCourseSinceMs=0;
  gpxGuidance.reset();gpxFixReceived=0;
  appState = RIDING; currentPage = 0; resetGpsSpeedFilter(now);
  ridePaused = false; rideAutoPaused=false; ridePausedAtMs = 0;
  autoPauseStationarySinceMs=autoResumeMovingSinceMs=0;
  autoResumeLastLocationMs=0;
  rideMaxSpeedMph = 0; distanceMiles = 0; averageSpeedMph = 0;
  climbFeet = 0; filteredElevationFt = 0; climbAnchorFt = 0;
  altitudeFusionInitialized = false;
  barometerInitialized = false;
  gpsAltitudeInitialized = false;
  gpsAltitudeDriftCorrectionFt = 0.0f;
  lastAltitudePacketMs = 0;
  gradeSampleHead = gradeSampleCount = 0;
  lastGradeSampleMs = 0;
  liveGradePercent = 0.0f;
  liveGradeValid = false;
  gradeMotionActive = false;
  altitudeSampleCount = 0;
  nextAltitudeSampleMs = now + 60000;
  minuteAccumulatorMs = now;
  minuteWeightedMs = 0;
  altitudeMinuteWeightedSum = 0.0f;
  speedMinuteWeightedSum = 0.0f;
  gpsSampleCount = 0;
  gpsSamplesSinceFlush = 0;
  nextGpsSampleMs = now;
  if (activeGpsFile) activeGpsFile.close();
  if (!demoRideActive && rideStorageReady) {
    FFat.remove(ACTIVE_GPS_PATH);
    activeGpsFile = FFat.open(ACTIVE_GPS_PATH, FILE_WRITE);
  }
  rideStartEpoch = 0;
  viewingSavedRide = false;
  portENTER_CRITICAL(&locationMux);
  if (hasLocation && latestLocation.timestamp > 0 &&
      timestampIsFresh(now, lastLocationMs, LOCATION_TIMEOUT_MS))
    rideStartEpoch = latestLocation.timestamp;
  portEXIT_CRITICAL(&locationMux);
  rideStartMs = ridePreviousMs = now; stationarySinceMs = 0; zeroSpeedSinceMs = now;
  displayAutoDimmed = false; display->setBrightness(normalBrightness);
  previousDrawMs = 0;
  if (!demoRideActive) notifyPhone("riding");
}

bool startDemoRide(uint32_t now) {
  demoPreviousRouteId = gpxLibrary.selectedID;
  if (!gpxLibrary.select(PQ_LOOP_ROUTE_ID, false)) {
    Serial.println("Demo start failed: PQ Loop route unavailable");
    return false;
  }
  demoRideActive = true;
  demoRouteMeters = 0.0f;
  demoTargetSpeedMph = 14.0f;
  demoNextSpeedTargetMs = now + 5000;
  demoRandomState ^= now | 1u;
  routeSelectionForStart = false;
  routeNavigationEnabled = true;
  startRide(now);
  currentSpeedMph = 12.0f;
  lastActivityMs = now;
  Serial.println("PQ Loop demo ride started");
  return true;
}

void stopDemoRide() {
  demoRideActive = false;
  demoStartHoldActive = false;
  currentSpeedMph = 0.0f;
  routeNavigationEnabled = false;
  if (demoPreviousRouteId && demoPreviousRouteId != gpxLibrary.selectedID)
    gpxLibrary.select(demoPreviousRouteId, false);
  demoPreviousRouteId = 0;
  portENTER_CRITICAL(&locationMux);
  hasLocation = false;
  lastLocationMs = 0;
  portEXIT_CRITICAL(&locationMux);
  openHome();
}

void updateDemoRideData(uint32_t now) {
  const float deltaSeconds = min(0.25f, (now - ridePreviousMs) * 0.001f);
  ridePreviousMs = now;
  if (ridePaused) {
    currentSpeedMph = 0.0f;
    liveGradeValid = false;
    gradeMotionActive = false;
    return;
  }

  if (demoRouteMeters >= gpx::length()) {
    currentSpeedMph = 0.0f;
    gpxArrived = true;
  } else {
    if (int32_t(now - demoNextSpeedTargetMs) >= 0) {
      // Xorshift gives deterministic, inexpensive target changes. A long
      // low-pass transition turns those targets into natural speed drift.
      demoRandomState ^= demoRandomState << 13;
      demoRandomState ^= demoRandomState >> 17;
      demoRandomState ^= demoRandomState << 5;
      demoTargetSpeedMph = 8.0f + (demoRandomState % 1401) * 0.01f;
      demoNextSpeedTargetMs = now + 4500 + (demoRandomState % 5000);
    }
    const float alpha = 1.0f - expf(-deltaSeconds / 3.5f);
    currentSpeedMph += alpha * (demoTargetSpeedMph - currentSpeedMph);
    currentSpeedMph = constrain(currentSpeedMph, 8.0f, 22.0f);
    demoRouteMeters = min(gpx::length(), demoRouteMeters +
        currentSpeedMph * 0.44704f * deltaSeconds);
  }

  const auto position = gpx::sample(demoRouteMeters);
  const float course = gpx::heading(demoRouteMeters);
  float courseDegrees = fmodf(course * 180.0f / PI + 360.0f, 360.0f);
  const time_t wallClock = time(nullptr);
  LocationPacket packet = {};
  packet.version = 1;
  packet.sequence = uint16_t(now / 200);
  packet.timestamp = wallClock > 1700000000 ? uint32_t(wallClock) : 0;
  packet.latitudeE7 = lround((gpx::lat0 +
      position.north / (gpx::RAD * gpx::EARTH)) * 1.0e7);
  packet.longitudeE7 = lround((gpx::lon0 +
      position.east / (gpx::RAD * gpx::EARTH * gpx::lonScale)) * 1.0e7);
  packet.speedCms = uint16_t(lroundf(currentSpeedMph / 0.02236936f));
  packet.courseDeg100 = uint16_t(lroundf(courseDegrees * 100.0f)) % 36000;
  packet.accuracyCm = 100;
  packet.flags = (1 << 0) | (1 << 2);  // Valid speed and course.
  portENTER_CRITICAL(&locationMux);
  latestLocation = packet;
  lastLocationMs = now;
  hasLocation = true;
  portEXIT_CRITICAL(&locationMux);

  distanceMiles = demoRouteMeters / 1609.344f;
  const float elapsedHours = activeRideElapsedMs(now) / 3600000.0f;
  averageSpeedMph = elapsedHours > 0.0f ? distanceMiles / elapsedHours : 0.0f;
  rideMaxSpeedMph = max(rideMaxSpeedMph, currentSpeedMph);
  liveGradeValid = false;
  gradeMotionActive = false;
  lastActivityMs = now;
}

void prepareRideEnd(uint32_t now) {
  if (demoRideActive) {
    stopDemoRide();
    return;
  }
  finishRidePause(now);
  accumulateMinuteAverages(now);
  storeMinuteAverage();  // Keep the final partial minute for short rides.
  summaryRideSeconds = activeRideElapsedMs(now) / 1000;
  summaryDistanceMiles = distanceMiles; summaryAverageMph = averageSpeedMph;
  summaryClimbFeet = int(roundf(climbFeet)); summaryPage = 0;
  summaryGpsPath[0] = '\0';
  summaryGpsOffset = 0;
  summaryRouteLoaded = false;
  saveOdometers();
  viewingSavedRide = false;
  if (activeGpsFile) activeGpsFile.flush();
  appState = SAVE_RIDE_PROMPT;
  previousDrawMs = 0;
}

void completeRideEnd(bool saveRide) {
  if (saveRide) saveCurrentRide();
  else {
    if (activeGpsFile) activeGpsFile.close();
    strncpy(summaryGpsPath, ACTIVE_GPS_PATH, sizeof(summaryGpsPath) - 1);
    summaryGpsPath[sizeof(summaryGpsPath) - 1] = '\0';
    summaryGpsOffset = 0;
    summaryRouteLoaded = false;
  }
  appState = SUMMARY;
  previousDrawMs = 0; notifyPhone("summary");
}

bool rideSessionIsActive() {
  return appState == RIDING || appState == RIDE_MENU ||
      (appState == DISPLAY_PAGE && displayReturnState == RIDE_MENU) ||
      (appState == STATUS_PAGE && statusReturnState == RIDE_MENU) ||
      (appState == RADAR_SETTINGS && radarSettingsReturnState == RIDING);
}

void preserveRideTailForShutdown(uint32_t now) {
  if (!ridePaused) accumulateMinuteAverages(now);
  storeMinuteAverage();
  if (activeGpsFile) activeGpsFile.flush();
}

bool autoSaveRideBeforeHardPowerOff(uint32_t elapsedRideMs) {
  if (demoRideActive || distanceMiles <= AUTO_SAVE_RIDE_MIN_MILES) return false;
  if (!rideStorageReady) {
    Serial.println("Auto-save skipped: ride storage unavailable");
    return false;
  }

  summaryRideSeconds = elapsedRideMs / 1000;
  summaryDistanceMiles = distanceMiles;
  summaryAverageMph = averageSpeedMph;
  summaryClimbFeet = int(roundf(climbFeet));
  summaryPage = 0;
  summaryGpsPath[0] = '\0';
  summaryGpsOffset = 0;
  summaryRouteLoaded = false;
  viewingSavedRide = false;
  saveOdometers();
  saveCurrentRide();

  const bool saved = summaryGpsPath[0] != '\0';
  Serial.printf(saved
                    ? "Auto-saved %.2f mi ride before hard power-off\n"
                    : "Auto-save failed for %.2f mi ride\n",
                summaryDistanceMiles);
  return saved;
}

void updateRideData(uint32_t now) {
  if (demoRideActive) {
    updateDemoRideData(now);
    return;
  }
  LocationPacket packet;
  bool available;
  uint32_t received;
  portENTER_CRITICAL(&locationMux);
  packet = latestLocation; available = hasLocation; received = lastLocationMs;
  portEXIT_CRITICAL(&locationMux);
  const bool fresh = available &&
      timestampIsFresh(now, received, LOCATION_TIMEOUT_MS);
  // RMC speed is measured from GNSS Doppler and remains useful when the
  // position HDOP is unavailable or temporarily poor.  Do not gate velocity
  // on accuracyCm: doing so held the speedometer at zero on otherwise valid
  // fixes and allowed the inactivity timer to put an active ride to sleep.
  const bool speedSampleValid = fresh && (packet.flags & 1) &&
      packet.speedCms <= 4470;  // 100 mph sanity ceiling.
  float rawSpeedMph = speedSampleValid
      ? packet.speedCms * 0.02236936f : 0.0f;
  if(rawSpeedMph<0.7f)rawSpeedMph=0.0f;

  if (ridePaused) {
    // Manual pause remains manual. Only an automatically paused ride watches
    // for sustained real movement and resumes itself.
    if(rideAutoPaused && fresh && received!=autoResumeLastLocationMs) {
      autoResumeLastLocationMs=received;
      if(rawSpeedMph>=AUTO_RESUME_MIN_SPEED_MPH) {
        if(!autoResumeMovingSinceMs)autoResumeMovingSinceMs=now;
        if(now-autoResumeMovingSinceMs>=AUTO_RESUME_CONFIRM_MS) {
          finishRidePause(now);
          lastActivityMs=now;
          previousDrawMs=0;
        }
      } else {
        autoResumeMovingSinceMs=0;
      }
    } else if(!fresh) {
      autoResumeMovingSinceMs=0;
      autoResumeLastLocationMs=0;
    }
    if(ridePaused) {
      ridePreviousMs = now;
      currentSpeedMph = 0.0f;
      liveGradeValid = false;
      gradeMotionActive = false;
      return;
    }
  }
  const float deltaHours = (now - ridePreviousMs) / 3600000.0f;
  ridePreviousMs = now;

  // Update the GPS target once per receiver sample. The old fixed 0.35 filter
  // ran on every main-loop pass, applying hundreds of corrections to the same
  // packet and turning each 1 Hz GPS change into an immediate visible jump.
  if (!fresh) {
    if (gpsSpeedHadFreshFix) resetGpsSpeedFilter(now);
  } else {
    gpsSpeedHadFreshFix = true;
    if (speedSampleValid && packet.sequence != gpsSpeedLastSequence) {
      const uint32_t sampleIntervalMs = gpsSpeedLastSampleMs
          ? max(uint32_t(100), received - gpsSpeedLastSampleMs) : 1000;
      gpsSpeedLastSequence = packet.sequence;
      gpsSpeedLastSampleMs = received;
      gpsSpeedLastValidMs = now;

      if (!gpsSpeedLocked) {
        // Do not expose the first value produced while the receiver is still
        // settling. Two nearby samples establish a real Doppler speed lock.
        if (!gpsSpeedCandidateCount ||
            fabsf(rawSpeedMph - gpsSpeedCandidateMph) > 3.0f) {
          gpsSpeedCandidateMph = rawSpeedMph;
          gpsSpeedCandidateCount = 1;
          gpsSpeedCandidateStartMs = now;
        } else {
          gpsSpeedCandidateMph +=
              (rawSpeedMph - gpsSpeedCandidateMph) /
              float(gpsSpeedCandidateCount + 1);
          ++gpsSpeedCandidateCount;
          if (gpsSpeedCandidateCount >= 3 &&
              now - gpsSpeedCandidateStartMs >= 800) {
            gpsSpeedTargetMph = gpsSpeedCandidateMph < 0.7f
                ? 0.0f : gpsSpeedCandidateMph;
            gpsSpeedLocked = true;
          }
        }
      } else {
        // A bicycle cannot change speed arbitrarily between receiver fixes.
        // Bound a single bad sample while still allowing brisk acceleration.
        const float maximumChange = max(
            1.5f, 6.0f * (sampleIntervalMs * 0.001f));
        gpsSpeedTargetMph += constrain(rawSpeedMph - gpsSpeedTargetMph,
                                       -maximumChange, maximumChange);
        if (gpsSpeedTargetMph < 0.7f) gpsSpeedTargetMph = 0.0f;
      }
    } else if (gpsSpeedLastValidMs &&
               now - gpsSpeedLastValidMs > 2500) {
      gpsSpeedTargetMph = 0.0f;
      gpsSpeedLocked = false;
      gpsSpeedCandidateCount = 0;
      gpsSpeedCandidateStartMs = 0;
    }
  }

  if (!isfinite(gpsSpeedTargetMph) || !isfinite(currentSpeedMph))
    resetGpsSpeedFilter(now);

  const uint32_t speedFilterElapsedMs = gpsSpeedFilterMs
      ? min(uint32_t(250), now - gpsSpeedFilterMs) : 0;
  gpsSpeedFilterMs = now;
  if (speedFilterElapsedMs) {
    const float alpha = 1.0f - expf(-speedFilterElapsedMs / 550.0f);
    currentSpeedMph += alpha * (gpsSpeedTargetMph - currentSpeedMph);
  }
  if (gpsSpeedTargetMph == 0.0f && currentSpeedMph < 0.1f)
    currentSpeedMph = 0.0f;
  if (!rideStartEpoch && fresh && packet.timestamp > 0) {
    rideStartEpoch = packet.timestamp - activeRideElapsedMs(now) / 1000;
  }
  if (now >= nextGpsSampleMs) {
    if (fresh && activeGpsFile && gpsSampleCount < MAX_GPS_SAMPLES &&
        (packet.latitudeE7 != 0 || packet.longitudeE7 != 0)) {
      GpsTrackPoint point = {};
      point.elapsedSeconds = activeRideElapsedMs(now) / 1000;
      point.latitudeE7 = packet.latitudeE7;
      point.longitudeE7 = packet.longitudeE7;
      point.altitudeDm = packet.altitudeDm;
      point.speedCms = packet.speedCms;
      if (activeGpsFile.write(reinterpret_cast<const uint8_t *>(&point), sizeof(point)) == sizeof(point)) {
        ++gpsSampleCount;
        // Commit filesystem metadata once per minute. The track itself never
        // occupies a large RAM buffer; at most the final minute is vulnerable
        // to an abrupt battery disconnect.
        if (++gpsSamplesSinceFlush >= 6) {
          activeGpsFile.flush();
          gpsSamplesSinceFlush = 0;
        }
      }
    }
    // One position per interval; do not duplicate a stale packet to catch up.
    nextGpsSampleMs = now + 10000;
  }
  // Count motion from the current receiver sample.  The three-sample display
  // lock and low-pass filter are presentation filters and must not delay the
  // safety-critical inactivity timer.
  if (speedSampleValid && rawSpeedMph >= 1.0f) lastActivityMs = now;
  // Require three continuous minutes of fresh, stationary GPS samples. A
  // missing fix never counts as stationary, and movement resets the timer.
  if(fresh && rawSpeedMph<=AUTO_PAUSE_MAX_SPEED_MPH) {
    if(!autoPauseStationarySinceMs)autoPauseStationarySinceMs=now;
    if(now-autoPauseStationarySinceMs>=AUTO_PAUSE_STATIONARY_MS) {
      beginRidePause(now,true);
      return;
    }
  } else {
    autoPauseStationarySinceMs=0;
  }
  rideMaxSpeedMph = max(rideMaxSpeedMph, currentSpeedMph);
  // Use hysteresis so a rider hovering near 3 mph on a steep climb does not
  // make the grade alternate between a number and "--". Do not change this
  // state during a brief packet gap; the grade sample timeout below handles
  // genuinely stale data.
  if (fresh) {
    if (gradeMotionActive) {
      if (currentSpeedMph < 2.0f) gradeMotionActive = false;
    } else if (currentSpeedMph >= 3.0f) {
      gradeMotionActive = true;
    }
  }
  const double distanceIncrement = currentSpeedMph * deltaHours;
  if (!isfinite(distanceIncrement) || distanceIncrement < 0.0 ||
      distanceIncrement > 0.25) {
    Serial.println("Ignored invalid ride-distance increment");
    resetGpsSpeedFilter(now);
    return;
  }
  if (!isfinite(distanceMiles)) distanceMiles = 0.0f;
  distanceMiles += distanceIncrement;
  if (!isfinite(odometerPendingMiles)) odometerPendingMiles = 0.0;
  if (!isfinite(tripAPendingMiles)) tripAPendingMiles = 0.0;
  if (!isfinite(tripBPendingMiles)) tripBPendingMiles = 0.0;
  odometerPendingMiles += distanceIncrement;
  tripAPendingMiles += distanceIncrement;
  tripBPendingMiles += distanceIncrement;
  if (odometerPendingMiles >= 1.0) saveOdometers();
  const float seconds = activeRideElapsedMs(now) / 1000.0f;
  if (seconds > 0.1f) averageSpeedMph = distanceMiles / (seconds / 3600.0f);
  // Process altitude only once per received iOS packet. Re-filtering the same
  // sample every loop allowed a single bad reading to become a huge climb.
  if (fresh && received != lastAltitudePacketMs) {
    const float packetSeconds = lastAltitudePacketMs
        ? max(0.1f, (received - lastAltitudePacketMs) / 1000.0f) : 1.0f;
    lastAltitudePacketMs = received;
    bool useBarometer = false;
    bool useGps = false;

    if (packet.version >= 2 && (packet.flags & (1 << 3))) {
      const float raw = packet.barometricRelativeAltitudeCm * 0.0328084f;
      if (!barometerInitialized) {
        const float reference = altitudeFusionInitialized ? filteredElevationFt : 0.0f;
        barometerBaselineFt = raw - reference;
        lastRawBarometerFt = raw;
        filteredBarometerRelativeFt = reference;
        barometerInitialized = true;
        useBarometer = true;
      } else {
        const float maximumJump = max(8.0f, packetSeconds * 8.0f);
        if (fabsf(raw - lastRawBarometerFt) <= maximumJump) {
          lastRawBarometerFt = raw;
          const float relative = raw - barometerBaselineFt;
          const float filterAlpha =
              1.0f - expf(-packetSeconds / BAROMETER_FILTER_TAU_SECONDS);
          filteredBarometerRelativeFt +=
              filterAlpha * (relative - filteredBarometerRelativeFt);
          useBarometer = true;
        }
      }
    }

    if (packet.flags & (1 << 1)) {
      const float raw = packet.altitudeDm * 0.328084f;
      if (!gpsAltitudeInitialized) {
        const float reference = altitudeFusionInitialized ? filteredElevationFt : 0.0f;
        gpsAltitudeBaselineFt = raw - reference;
        lastRawGpsAltitudeFt = raw;
        filteredGpsRelativeFt = reference;
        gpsAltitudeInitialized = true;
        useGps = true;
      } else {
        const float maximumJump = max(40.0f, packetSeconds * 35.0f);
        if (fabsf(raw - lastRawGpsAltitudeFt) <= maximumJump) {
          lastRawGpsAltitudeFt = raw;
          const float relative = raw - gpsAltitudeBaselineFt;
          const float filterAlpha =
              1.0f - expf(-packetSeconds / GPS_ALTITUDE_FILTER_TAU_SECONDS);
          filteredGpsRelativeFt +=
              filterAlpha * (relative - filteredGpsRelativeFt);
          useGps = true;
        }
      }
    }

    if (useBarometer || useGps) {
      // Barometric altitude supplies the short-term shape. GPS altitude is too
      // noisy for a packet-by-packet blend, so use it only to remove slow
      // pressure/weather drift. A time-based coefficient makes the correction
      // independent of the phone packet rate.
      if (barometerInitialized && gpsAltitudeInitialized && useGps) {
        const float correctionAlpha =
            1.0f - expf(-packetSeconds / GPS_ALTITUDE_CORRECTION_TAU_SECONDS);
        const float correctedBarometer =
            filteredBarometerRelativeFt + gpsAltitudeDriftCorrectionFt;
        gpsAltitudeDriftCorrectionFt += correctionAlpha *
            (filteredGpsRelativeFt - correctedBarometer);
      }

      // Once available, barometer remains the primary elevation source. Holding
      // its last value across an occasional rejected sample avoids the jumps
      // caused by switching between 80/20 and single-source blends.
      const float fused = barometerInitialized
          ? filteredBarometerRelativeFt + gpsAltitudeDriftCorrectionFt
          : filteredGpsRelativeFt;
      if (!altitudeFusionInitialized) {
        filteredElevationFt = climbAnchorFt = fused;
        altitudeFusionInitialized = true;
      } else {
        filteredElevationFt = fused;
        const float change = filteredElevationFt - climbAnchorFt;
        // Accumulate only ascent. A negative move resets the local baseline so
        // climbing after a descent is still counted, but descent is never added.
        // While stopped, track the baseline without adding climb so pressure or
        // slow GPS corrections cannot accumulate stationary ascent.
        if (!gradeMotionActive) {
          climbAnchorFt = filteredElevationFt;
        } else if (change >= CLIMB_DEADBAND_FT) {
          climbFeet += change;
          climbAnchorFt = filteredElevationFt;
        } else if (change <= -CLIMB_DEADBAND_FT) {
          climbAnchorFt = filteredElevationFt;
        }
      }
      // Grade deliberately uses only the fast barometric channel. If barometer
      // packets stop, the existing timeout changes the display to "--" rather
      // than silently substituting noisy GPS altitude.
      if (useBarometer) updateLiveGrade(now, filteredBarometerRelativeFt);
    }
  }

  if (!gradeMotionActive ||
      (lastGradeSampleMs && now - lastGradeSampleMs > 5000))
    liveGradeValid = false;

  accumulateMinuteAverages(now);
}

void updateAutoBrightness(uint32_t now) {
  if (currentSpeedMph > 0.1f) {
    zeroSpeedSinceMs = 0;
    if (displayAutoDimmed) { displayAutoDimmed = false; display->setBrightness(normalBrightness); }
  } else {
    if (!zeroSpeedSinceMs) zeroSpeedSinceMs = now;
    if (!displayAutoDimmed && now - zeroSpeedSinceMs >= 30000) {
      displayAutoDimmed = true; display->setBrightness(min(normalBrightness, uint8_t(32)));
    }
  }
}

Gesture pollTouch(uint32_t now) {
  // A countdown-cancel hold must end completely before the confirmation can
  // accept input. Once released, add a quiet window for controller bounce.
  if(touchBlockedUntilRelease) {
    const bool shouldPoll=touchPending || touchActive ||
        now-lastTouchPollMs>=TOUCH_POLL_MS;
    if(shouldPoll) {
      if(touchPending) {
        noInterrupts();touchPending=false;interrupts();
      }
      lastTouchPollMs=now;
      int16_t ignoredX[2],ignoredY[2];
      const uint8_t points=touch.getPoint(ignoredX,ignoredY,2);
      if(!points) {
        touchBlockedUntilRelease=false;
        touchActive=false;
        demoStartHoldActive=false;
        ignoreTouchUntilMs=now+COUNTDOWN_CANCEL_TOUCH_LOCKOUT_MS;
      }
    }
    return GESTURE_NONE;
  }
  // Some CST9217 releases produce a second short contact report after a menu
  // tap. Consume those reports without blocking animation or BLE work.
  if (int32_t(ignoreTouchUntilMs - now) > 0) {
    if (touchPending || touchActive) {
      noInterrupts();
      touchPending = false;
      interrupts();
      int16_t ignoredX[2], ignoredY[2];
      touch.getPoint(ignoredX, ignoredY, 2);
    }
    touchActive = false;
    powerSliderActive = false;
    odometerResetSliderActive = false;
    demoStartHoldActive = false;
    return GESTURE_NONE;
  }
  const bool periodicPoll = touchActive && now - lastTouchPollMs >= TOUCH_POLL_MS;
  if (touchPending || periodicPoll) {
    if (touchPending) {
      noInterrupts(); touchPending = false; interrupts();
    }
    lastTouchPollMs = now;
    int16_t x[2], y[2];
    const uint8_t points = touch.getPoint(x, y, 2);
    if (points) {
      // Display rotation 1 is 90 degrees clockwise. Convert the touch
      // coordinates back into the rotated UI coordinate system.
      for (uint8_t i = 0; i < points; ++i) {
        const int16_t unrotatedX = x[i];
        x[i] = y[i];
        y[i] = SCREEN_SIZE - 1 - unrotatedX;
      }
      if (appState == START_ROUTE_MENU && points >= 2) {
        const bool point0InColumn = x[0] >= ux(26) && x[0] <= ux(214);
        const bool point1InColumn = x[1] >= ux(26) && x[1] <= ux(214);
        const bool point0Gpx = point0InColumn && y[0] >= uy(75) && y[0] <= uy(133);
        const bool point1Gpx = point1InColumn && y[1] >= uy(75) && y[1] <= uy(133);
        const bool point0Free = point0InColumn && y[0] >= uy(137) && y[0] <= uy(195);
        const bool point1Free = point1InColumn && y[1] >= uy(137) && y[1] <= uy(195);
        const bool bothChoices = (point0Gpx && point1Free) ||
                                 (point1Gpx && point0Free);
        if (bothChoices) {
          if (!demoStartHoldActive) {
            demoStartHoldActive = true;
            demoStartHoldMs = now;
          } else if (now - demoStartHoldMs >= 2000) {
            demoStartHoldActive = false;
            touchActive = false;
            touchBlockedUntilRelease = true;
            previousDrawMs = 0;
            return GESTURE_START_DEMO;
          }
        } else {
          demoStartHoldActive = false;
        }
      } else {
        demoStartHoldActive = false;
      }
      // A touch wakes the panel immediately; no completed swipe is required.
      wakeDisplay(now);
      if (!touchActive) {
        touchActive = true; touchStartX = touchLastX = x[0];
        touchStartY = touchLastY = y[0]; touchStartMs = now;
        touchAdjustedBrightness = false;
        if (appState == OPTIONS_MENU &&
            x[0] >= ux(63) && x[0] <= ux(112) &&
            y[0] >= uy(176) && y[0] <= uy(232)) {
          powerSliderActive = true;
          powerSliderStartX = x[0];
          powerSliderX = x[0];
          previousDrawMs = 0;
        }
        if (appState == CONFIRM_PAGE &&
            pendingAction == ACTION_RESET_ODOMETER && odometerResetArmed &&
            x[0] >= ux(48) && x[0] <= ux(106) &&
            y[0] >= uy(116) && y[0] <= uy(184)) {
          odometerResetSliderActive = true;
          odometerResetSliderStartX = x[0];
          odometerResetSliderX = x[0];
          previousDrawMs = 0;
        }
      } else { touchLastX = x[0]; touchLastY = y[0]; }
      touchLastMs = now;

      if (powerSliderActive) {
        powerSliderX = x[0];
        previousDrawMs = 0;
      }
      if (odometerResetSliderActive) {
        odometerResetSliderX = x[0];
        previousDrawMs = 0;
      }

      // The brightness bar is a direct-manipulation control.  Its enlarged
      // vertical hit area makes it easy to use while riding or wearing gloves.
      if (appState == DISPLAY_PAGE && y[0] >= uy(98) && y[0] <= uy(158)) {
        const int left = ux(29);
        const int right = ux(211);
        const int level = constrain(int(x[0]), left, right) - left;
        normalBrightness = uint8_t((level * 255L) / (right - left));
        displayAutoDimmed = false;
        zeroSpeedSinceMs = now;
        display->setBrightness(normalBrightness);
        previousDrawMs = 0;
        touchAdjustedBrightness = true;
      }
    } else if (touchActive) {
      demoStartHoldActive = false;
      touchLastMs = now - TOUCH_RELEASE_MS;
    }
  }
  if (!touchActive || now - touchLastMs < TOUCH_RELEASE_MS) return GESTURE_NONE;
  touchActive = false;
  if (powerSliderActive) {
    const int sliderDx = touchLastX - touchStartX;
    const int sliderDy = touchLastY - touchStartY;
    // A bottom-edge upward flick remains the universal Back gesture even
    // when it begins on the slide-to-off thumb.
    if (sliderDy <= -SWIPE_THRESHOLD &&
        abs(sliderDx) <= SWIPE_VERTICAL_LIMIT) {
      powerSliderActive = false;
      previousDrawMs = 0;
      return GESTURE_UP;
    }
    const bool completed = powerSliderX >= ux(145) &&
                           powerSliderX - powerSliderStartX >= us(42);
    powerSliderActive = false;
    previousDrawMs = 0;
    return completed ? GESTURE_POWER_OFF : GESTURE_NONE;
  }
  if (odometerResetSliderActive) {
    const int sliderDx = touchLastX - touchStartX;
    const int sliderDy = touchLastY - touchStartY;
    if (sliderDy <= -SWIPE_THRESHOLD &&
        abs(sliderDx) <= SWIPE_VERTICAL_LIMIT) {
      odometerResetSliderActive = false;
      previousDrawMs = 0;
      return GESTURE_UP;
    }
    const bool completed = odometerResetSliderX >= ux(158) &&
                           odometerResetSliderX - odometerResetSliderStartX >= us(60);
    odometerResetSliderActive = false;
    previousDrawMs = 0;
    return completed ? GESTURE_ODOMETER_RESET : GESTURE_NONE;
  }
  if (touchCancelConsumed) {
    touchCancelConsumed=false;
    return GESTURE_NONE;
  }
  if (touchAdjustedBrightness) {
    touchAdjustedBrightness = false;
    return GESTURE_NONE;
  }
  const int dx = touchLastX - touchStartX;
  const int dy = touchLastY - touchStartY;
  // The completed-ride screens must be easy to leave from any page. Accept a
  // shorter and more diagonal upward flick here than the generic navigation
  // threshold, then let the main state handler return to Home/Rides.
  if (appState == SUMMARY && dy <= -us(14) && abs(dx) <= us(105))
    return GESTURE_UP;
  // Back must be available from the whole round display, not only when the
  // gesture starts in the lower 45 percent.
  if (dy <= -SWIPE_THRESHOLD && abs(dx) <= SWIPE_VERTICAL_LIMIT)
    return GESTURE_UP;
  const int horizontalThreshold = (appState == ANCS_TEST ||
                                   appState == OPTIONS_MENU) ? 24 : SWIPE_THRESHOLD;
  if (abs(dx) >= horizontalThreshold && abs(dy) <= SWIPE_VERTICAL_LIMIT)
    return dx < 0 ? GESTURE_LEFT : GESTURE_RIGHT;
  if (abs(dx) < 30 && abs(dy) < 30 && now - touchStartMs < LONG_PRESS_MS) {
    lastTapX = touchLastX;
    lastTapY = touchLastY;
    // Reserve the outer contour for paging. Icon taps remain in the middle
    // of each half, so navigating never opens a menu item accidentally.
    if (appState == OPTIONS_MENU && lastTapY >= uy(43) &&
        lastTapY <= uy(181)) {
      if (lastTapX <= ux(40)) return GESTURE_RIGHT;
      if (lastTapX >= ux(200)) return GESTURE_LEFT;
      if (lastTapY >= uy(157) && lastTapX < ux(90))
        return GESTURE_RIGHT;
      if (lastTapY >= uy(157) && lastTapX > ux(150))
        return GESTURE_LEFT;
    }
    // An automatic GPX preview used to claim every tap before the side-page
    // zones were checked. That made left/right taps dismiss first and change
    // pages only on a second touch. Preserve center-tap dismissal, but let a
    // side tap advance immediately; the main loop already closes the preview
    // before changing pages for these two gestures.
    if (appState == RIDING && (gpxPageAlert.active || gpxTurnPreview.active)) {
      if (lastTapX < SCREEN_SIZE / 3) return GESTURE_RIGHT;
      if (lastTapX >= SCREEN_SIZE * 2 / 3) return GESTURE_LEFT;
      return GESTURE_TAP;
    }
    // Free Ride zoom occupies the right third, which is normally the next-page
    // tap zone. Claim these two large controls before page navigation.
    if(appState==RIDING && !routeNavigationEnabled && currentPage==gpx::PAGE_INDEX &&
       freeRideMapZoomControl(lastTapX,lastTapY))return GESTURE_TAP;
    // During a ride, reserve a generous strip along the bottom edge for the
    // menu. Check it before the left/right page zones so the corner portions
    // of the strip behave consistently too.
    if (appState == RIDING && lastTapY >= SCREEN_SIZE * 78 / 100)
      return GESTURE_TAP;
    // On the ride screen, the outer thirds are large previous/next targets.
    if ((appState == RIDING || appState == SUMMARY) && lastTapX < SCREEN_SIZE / 3)
      return GESTURE_RIGHT;
    if ((appState == RIDING || appState == SUMMARY) &&
        lastTapX >= SCREEN_SIZE * 2 / 3) return GESTURE_LEFT;
    // ANCS history also accepts the outer thirds as large previous/next
    // targets. This complements swiping on a small round touch surface.
    if (appState == ANCS_TEST && ancsHistoryCount &&
        lastTapX < SCREEN_SIZE / 3) return GESTURE_RIGHT;
    if (appState == ANCS_TEST && ancsHistoryCount &&
        lastTapX >= SCREEN_SIZE * 2 / 3) return GESTURE_LEFT;
    return GESTURE_TAP;
  }
  return GESTURE_NONE;
}

bool physicalLongPress(uint32_t now) {
  const bool down = digitalRead(BOOT_BUTTON_PIN) == LOW;
  if (down && !buttonWasDown) { buttonDownMs = now; buttonLongHandled = false; }
  buttonWasDown = down;
  if (down && !buttonLongHandled && now - buttonDownMs >= LONG_PRESS_MS) {
    buttonLongHandled = true;
    return true;
  }
  return false;
}

void showBrandSplash(const char *word, uint32_t durationMs) {
  beginFrame();
  textCenteredScaled(word, CENTER, uy(61), 2, WHITE);
  drawMushroom(CENTER, uy(166));
  endFrame();
  delay(durationMs);
}

constexpr uint32_t SPLASH_DOT_COMPLETE_MS = 360;
constexpr uint32_t SPLASH_RING_COMPLETE_MS = 1100;
constexpr uint32_t SPLASH_RING_LOCK_MS = 1300;
constexpr uint32_t SPLASH_NOTCH_COMPLETE_MS = 1430;
constexpr uint32_t SPLASH_P_COMPLETE_MS = 1750;
constexpr uint32_t SPLASH_TEXT_COMPLETE_MS = 2100;
// Hold the finished iOS-style mark for two full seconds, then let the outer
// ring expand beyond the panel as the logo fades into the app UI.
constexpr uint32_t SPLASH_HOLD_END_MS = SPLASH_TEXT_COMPLETE_MS + 2000;
constexpr uint32_t SPLASH_END_MS = SPLASH_HOLD_END_MS + 650;
constexpr uint16_t SPLASH_LIME = LOGO_LIME;
constexpr uint16_t SPLASH_CYAN = LOGO_CYAN;

float splashProgress(uint32_t elapsed, uint32_t start, uint32_t end) {
  if (elapsed <= start) return 0.0f;
  if (elapsed >= end) return 1.0f;
  const float value = float(elapsed - start) / float(end - start);
  return value * value * (3.0f - 2.0f * value);
}

uint16_t splashGradientColor(int x, int y, float opacity = 1.0f) {
  // Match the logo artwork: lime across the upper-left, transitioning through
  // green to cyan down the right and along the bottom.
  const float horizontal = constrain(float(x) / float(SCREEN_SIZE - 1), 0.0f, 1.0f);
  const float vertical = constrain(float(y) / float(SCREEN_SIZE - 1), 0.0f, 1.0f);
  const float amount = constrain(horizontal * 0.35f + vertical * 0.65f, 0.0f, 1.0f);
  return scaleRgb565(blendRgb565(SPLASH_LIME, SPLASH_CYAN, amount), opacity);
}

void splashPoint(float angleDegrees, float radius, int &x, int &y) {
  const float radians = angleDegrees * DEG_TO_RAD;
  x = CENTER + lroundf(cosf(radians) * radius);
  y = CENTER + lroundf(sinf(radians) * radius);
}

// Retained as the prior hardware-only sprocket treatment for easy comparison.
void drawSplashGearLegacy(float progress, float opacity) {
  progress = constrain(progress, 0.0f, 1.0f);
  if (progress <= 0.0f || opacity <= 0.0f) return;

  // Scale the sprocket out to the round panel's usable edge. The tooth tips
  // stop five pixels short of the 233 px display radius so antialiasing and
  // panel-to-panel alignment do not clip the outer silhouette.
  constexpr float innerRadius = 179.0f;
  constexpr float rootRadius = 203.0f;
  constexpr float toothRadius = 228.0f;
  constexpr float segmentDegrees = 3.0f;
  const float revealedDegrees = 360.0f * progress;

  // Draw the continuous sprocket band clockwise from twelve o'clock. Short
  // filled quadrilaterals let the color follow the diagonal artwork gradient.
  for (float degree = 0.0f; degree < revealedDegrees; degree += segmentDegrees) {
    const float next = fminf(degree + segmentDegrees, revealedDegrees);
    int outer1X, outer1Y, outer2X, outer2Y;
    int inner1X, inner1Y, inner2X, inner2Y;
    splashPoint(-90.0f + degree, rootRadius, outer1X, outer1Y);
    splashPoint(-90.0f + next, rootRadius, outer2X, outer2Y);
    splashPoint(-90.0f + degree, innerRadius, inner1X, inner1Y);
    splashPoint(-90.0f + next, innerRadius, inner2X, inner2Y);
    const uint16_t color = splashGradientColor(
        (outer1X + outer2X + inner1X + inner2X) / 4,
        (outer1Y + outer2Y + inner1Y + inner2Y) / 4, opacity);
    canvas->fillTriangle(outer1X, outer1Y, outer2X, outer2Y,
                         inner2X, inner2Y, color);
    canvas->fillTriangle(outer1X, outer1Y, inner2X, inner2Y,
                         inner1X, inner1Y, color);
  }

  // Eighteen broad teeth reproduce the supplied PedalOne icon. Each tooth is
  // revealed when the clockwise ring reaches its angular position.
  constexpr uint8_t toothCount = 18;
  for (uint8_t tooth = 0; tooth < toothCount; ++tooth) {
    const float revealAt = (float(tooth) + 0.72f) / float(toothCount);
    if (progress < revealAt) continue;
    const float centerAngle = -90.0f + tooth * (360.0f / toothCount);
    int root1X, root1Y, tip1X, tip1Y, tip2X, tip2Y, root2X, root2Y;
    splashPoint(centerAngle - 7.0f, rootRadius - 1.0f, root1X, root1Y);
    splashPoint(centerAngle - 4.7f, toothRadius, tip1X, tip1Y);
    splashPoint(centerAngle + 4.7f, toothRadius, tip2X, tip2Y);
    splashPoint(centerAngle + 7.0f, rootRadius - 1.0f, root2X, root2Y);
    int colorX, colorY;
    splashPoint(centerAngle, toothRadius - 7.0f, colorX, colorY);
    const uint16_t color = splashGradientColor(colorX, colorY, opacity);
    canvas->fillTriangle(root1X, root1Y, tip1X, tip1Y, tip2X, tip2Y, color);
    canvas->fillTriangle(root1X, root1Y, tip2X, tip2Y, root2X, root2Y, color);
  }
}

void drawSplashGear(float progress, float opacity, float rotationDegrees = 0.0f,
                    float notchOpacity = 1.0f, float scale = 1.0f) {
  progress = constrain(progress, 0.0f, 1.0f);
  if (progress <= 0.0f || opacity <= 0.0f) return;

  // The iOS logo uses a smooth segmented ring, rather than individual gear
  // teeth: lime across the upper-left, a short white indicator at twelve
  // o'clock, and cyan across the lower-right. Four narrow black breaks make
  // the mark read as a modern circular progress control.
  // Fill the round panel: the outer edge stops only five pixels inside the
  // 233 px display radius, leaving just enough margin for clean raster edges.
  // Preserve the stronger ring weight as the mark grows toward the bezel.
  const float innerRadius = 190.0f * scale;
  const float outerRadius = 228.0f * scale;
  constexpr float segmentDegrees = 2.0f;
  struct RingSegment { float start; float end; bool white; };
  static constexpr RingSegment segments[] = {
      {3.0f, 23.0f, true},     // short white top indicator
      {32.0f, 90.0f, false},   // upper-right lime/green
      {99.0f, 180.0f, false},  // lower-right cyan
      {189.0f, 270.0f, false}, // lower-left cyan/green
      {279.0f, 357.0f, false}, // upper-left lime
  };
  const float revealedDegrees = 360.0f * progress;

  for (const RingSegment &segment : segments) {
    if (revealedDegrees <= segment.start) continue;
    if (segment.white && notchOpacity <= 0.0f) continue;
    const float revealedEnd = min(segment.end, revealedDegrees);
    for (float degree = segment.start; degree < revealedEnd;
         degree += segmentDegrees) {
      const float next = min(degree + segmentDegrees, revealedEnd);
      int outer1X, outer1Y, outer2X, outer2Y;
      int inner1X, inner1Y, inner2X, inner2Y;
      splashPoint(-90.0f + degree + rotationDegrees, outerRadius, outer1X, outer1Y);
      splashPoint(-90.0f + next + rotationDegrees, outerRadius, outer2X, outer2Y);
      splashPoint(-90.0f + degree + rotationDegrees, innerRadius, inner1X, inner1Y);
      splashPoint(-90.0f + next + rotationDegrees, innerRadius, inner2X, inner2Y);
      const uint16_t color = segment.white
          ? scaleRgb565(WHITE, opacity * notchOpacity)
          : splashGradientColor(
              (outer1X + outer2X + inner1X + inner2X) / 4,
              (outer1Y + outer2Y + inner1Y + inner2Y) / 4, opacity);
      canvas->fillTriangle(outer1X, outer1Y, outer2X, outer2Y,
                           inner2X, inner2Y, color);
      canvas->fillTriangle(outer1X, outer1Y, inner2X, inner2Y,
                           inner1X, inner1Y, color);
    }
  }
}

void drawSplashGradientStroke(int x1, int y1, int x2, int y2,
                              int radius, float opacity) {
  const float dx = float(x2 - x1);
  const float dy = float(y2 - y1);
  const int steps = max(1, int(sqrtf(dx * dx + dy * dy) / 2.0f));
  for (int step = 0; step <= steps; ++step) {
    const float amount = float(step) / float(steps);
    const int x = lroundf(float(x1) + dx * amount);
    const int y = lroundf(float(y1) + dy * amount);
    canvas->fillCircle(x, y, radius, splashGradientColor(x, y, opacity));
  }
}

void drawSplashGradientArc(int centerX, int centerY, int radius,
                           float startDegrees, float endDegrees,
                           int strokeRadius, float opacity) {
  const float step = endDegrees >= startDegrees ? 2.0f : -2.0f;
  for (float degree = startDegrees;
       step > 0.0f ? degree <= endDegrees : degree >= endDegrees;
       degree += step) {
    const float radians = degree * DEG_TO_RAD;
    const int x = centerX + lroundf(cosf(radians) * radius);
    const int y = centerY + lroundf(sinf(radians) * radius);
    canvas->fillCircle(x, y, strokeRadius,
                       splashGradientColor(x, y, opacity));
  }
}

void drawPedalOneP(float opacity) {
  if (opacity <= 0.0f) return;
  constexpr int strokeRadius = 14;

  // The large left-facing arrow is the beginning of the P's upper stroke,
  // matching the supplied ground-truth icon rather than a separate ornament.
  const uint16_t arrowColor = splashGradientColor(184, 141, opacity);
  canvas->fillTriangle(164, 141, 199, 113, 199, 169, arrowColor);
  drawSplashGradientStroke(198, 141, 250, 141, strokeRadius, opacity);
  drawSplashGradientArc(250, 184, 43, -90.0f, 90.0f,
                        strokeRadius, opacity);
  drawSplashGradientStroke(250, 227, 225, 227, strokeRadius, opacity);
  drawSplashGradientArc(225, 259, 32, -90.0f, -180.0f,
                        strokeRadius, opacity);
  drawSplashGradientStroke(193, 259, 193, 276, strokeRadius, opacity);
  const uint16_t tailColor = splashGradientColor(190, 278, opacity);
  canvas->fillTriangle(179, 276, 207, 262, 179, 292, tailColor);
}

void drawPedalOneWordmark(float opacity) {
  if (opacity <= 0.0f) return;
  textCentered("PEDAL", CENTER, 318, 5, scaleRgb565(WHITE, opacity));
  textCentered("O", CENTER - 48, 359, 4,
               splashGradientColor(CENTER - 48, 359, opacity));
  textCentered("N", CENTER, 359, 4,
               splashGradientColor(CENTER, 359, opacity));
  textCentered("E", CENTER + 48, 359, 4,
               splashGradientColor(CENTER + 48, 359, opacity));
}

void drawPedalOneSplashFrame(uint32_t elapsed) {
  beginFrame();
  const float dotIn = splashProgress(elapsed, 160, SPLASH_DOT_COMPLETE_MS);
  const float dotOut = 1.0f - splashProgress(
      elapsed, SPLASH_RING_COMPLETE_MS, SPLASH_P_COMPLETE_MS);
  const float dotOpacity = dotIn * dotOut;
  if (dotOpacity > 0.0f) {
    const int radius = 2 + lroundf(3.0f * dotIn);
    canvas->fillCircle(CENTER, CENTER, radius,
                       splashGradientColor(CENTER, CENTER, dotOpacity));
  }

  const float ringProgress = splashProgress(
      elapsed, SPLASH_DOT_COMPLETE_MS, SPLASH_RING_COMPLETE_MS);
  // The iOS ring arrives thirty degrees clockwise from its final alignment,
  // then eases into place. The white notch is deliberately absent while the
  // ring is moving and appears only after that mechanical-looking lock.
  const float ringRotation = 30.0f * (1.0f - splashProgress(
      elapsed, SPLASH_DOT_COMPLETE_MS, SPLASH_RING_LOCK_MS));
  const float notchOpacity = splashProgress(
      elapsed, SPLASH_RING_LOCK_MS, SPLASH_NOTCH_COMPLETE_MS);
  const float exitProgress = splashProgress(
      elapsed, SPLASH_HOLD_END_MS, SPLASH_END_MS);
  // Accelerate toward the viewer: once the ring's inner edge has passed the
  // 233 px panel radius, the whole sprocket cleanly disappears off-screen.
  const float exitScale = 1.0f + 0.85f * exitProgress;
  const float logoOpacity = 1.0f - exitProgress;
  drawSplashGear(ringProgress, 1.0f, ringRotation, notchOpacity, exitScale);

  const float pOpacity = splashProgress(
      elapsed, SPLASH_NOTCH_COMPLETE_MS, SPLASH_P_COMPLETE_MS);
  drawPedalOneP(pOpacity * logoOpacity);

  const float textOpacity = splashProgress(
      elapsed, SPLASH_P_COMPLETE_MS, SPLASH_TEXT_COMPLETE_MS);
  drawPedalOneWordmark(textOpacity * logoOpacity);
  endAnimatedFrame();
}

uint32_t showPedalOneStartupSplashIntro() {
  constexpr uint32_t frameIntervalMs = 33;  // Approximately 30 FPS.
  const uint32_t started = millis();
  uint32_t nextFrame = started;
  while (true) {
    const uint32_t now = millis();
    const uint32_t elapsed = now - started;
    if (elapsed >= SPLASH_TEXT_COMPLETE_MS) break;
    if (int32_t(now - nextFrame) < 0) {
      delay(nextFrame - now);
      continue;
    }
    drawPedalOneSplashFrame(elapsed);
    nextFrame += frameIntervalMs;
    if (int32_t(now - nextFrame) >= 0) nextFrame = now + 1;
  }
  // Leave the complete logo on the panel while the remaining peripherals and
  // BLE services initialize. The framebuffer remains visible without redraws.
  drawPedalOneSplashFrame(SPLASH_TEXT_COMPLETE_MS);
  return millis();
}

void finishPedalOneStartupSplash(uint32_t logoReadyMs) {
  constexpr uint32_t frameIntervalMs = 33;  // Approximately 30 FPS.
  constexpr uint32_t holdMs = SPLASH_HOLD_END_MS - SPLASH_TEXT_COMPLETE_MS;
  while (millis() - logoReadyMs < holdMs) delay(5);

  const uint32_t exitStarted = millis();
  uint32_t nextFrame = exitStarted;
  while (true) {
    const uint32_t now = millis();
    const uint32_t exitElapsed = now - exitStarted;
    if (exitElapsed >= SPLASH_END_MS - SPLASH_HOLD_END_MS) break;
    if (int32_t(now - nextFrame) < 0) {
      delay(nextFrame - now);
      continue;
    }
    drawPedalOneSplashFrame(SPLASH_HOLD_END_MS + exitElapsed);
    nextFrame += frameIntervalMs;
    if (int32_t(now - nextFrame) >= 0) nextFrame = now + 1;
  }
}

void showPedalOneStartupSplash() {
  const uint32_t logoReadyMs = showPedalOneStartupSplashIntro();
  finishPedalOneStartupSplash(logoReadyMs);
}

void showShutdownSplash() { showBrandSplash("BYO", 1000); }

void prepareTouchWakePin(bool resetController = false) {
  // The CST9217 IRQ is open-drain and active low. A normal GPIO pull-up is not
  // guaranteed to remain enabled when the digital domain powers down, which
  // lets the line float low and immediately wake deep sleep. Hand GPIO 11 to
  // the RTC domain and keep its pull-up active for both sleep tiers.
  detachInterrupt(digitalPinToInterrupt(TP_INT));
  if (resetController) {
    // Manual Off follows a touch confirmation. Resetting clears delayed CST9217
    // contact reports that can arrive after the BYO splash and wake EXT0.
    touch.reset();
    delay(100);
  }
  int16_t releaseX[2], releaseY[2];
  const uint32_t requiredQuietMs = resetController ? 1000 : 250;
  const uint32_t releaseDeadlineMs = millis() + 5000;
  uint32_t quietSinceMs = millis();
  while (millis() - quietSinceMs < requiredQuietMs &&
         int32_t(releaseDeadlineMs - millis()) > 0) {
    noInterrupts();
    const bool pending = touchPending;
    touchPending = false;
    interrupts();
    // Read even after a short IRQ pulse has returned high. Otherwise the
    // controller can retain that report and assert IRQ again during sleep.
    const uint8_t points = touch.getPoint(releaseX, releaseY, 2);
    if (pending || points || digitalRead(TP_INT) == LOW)
      quietSinceMs = millis();
    delay(10);
  }
  touchPending = false;
  touchActive = false;

  rtc_gpio_init(GPIO_NUM_11);
  rtc_gpio_set_direction(GPIO_NUM_11, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_dis(GPIO_NUM_11);
  rtc_gpio_pullup_en(GPIO_NUM_11);
}

void restoreTouchWakePin() {
  rtc_gpio_deinit(GPIO_NUM_11);
  pinMode(TP_INT, INPUT_PULLUP);
  touchPending = false;
  touchActive = false;
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInterrupt, FALLING);
}

void armHardPowerOffTimer(uint64_t timeoutUs) {
  // A timer wake is deliberately used as the second tier: the ESP32 stays in
  // normal touch-wake sleep until this deadline, then boots just long enough
  // to ask the PMIC for a true rail shutdown.
  esp_sleep_enable_timer_wakeup(timeoutUs);
}

uint64_t remainingHardPowerOffUs(uint32_t now) {
  const uint32_t elapsed=nonNegativeElapsedMs(now,lastActivityMs);
  return elapsed>=HARD_POWER_OFF_MS ? 1000ULL : uint64_t(HARD_POWER_OFF_MS-elapsed)*1000ULL;
}

bool readPhysicalPowerButton(bool &pressed) {
  Wire.beginTransmission(IO_EXPANDER_ADDRESS);
  Wire.write(IO_EXPANDER_INPUT_REGISTER);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(IO_EXPANDER_ADDRESS, uint8_t(1), true) != 1)
    return false;
  pressed = (Wire.read() & POWER_BUTTON_MASK) != 0;
  return true;
}

void servicePhysicalPowerButton(uint32_t now) {
  if (!pmicAvailable || now - lastPowerButtonPollMs < POWER_BUTTON_POLL_MS)
    return;
  lastPowerButtonPollMs = now;

  bool pressed = false;
  if (!readPhysicalPowerButton(pressed)) return;

  if (pressed) {
    if (!physicalPowerButtonDown) {
      physicalPowerButtonDown = true;
      physicalPowerButtonDownMs = now;
      gpsBackupAttemptedForPowerButton = false;
      gpsBackupPreparedForPowerButton = false;
      rideAutoSavedForPowerButton = false;
    }
    if (!gpsBackupAttemptedForPowerButton &&
        now - physicalPowerButtonDownMs >= POWER_BUTTON_GPS_BACKUP_HOLD_MS) {
      gpsBackupAttemptedForPowerButton = true;
      if (rideSessionIsActive() && distanceMiles > AUTO_SAVE_RIDE_MIN_MILES) {
        preserveRideTailForShutdown(now);
        rideAutoSavedForPowerButton =
            autoSaveRideBeforeHardPowerOff(activeRideElapsedMs(now));
      }
      if (!onboardGpsPresent) return;
      Serial.println("PWR hold: preparing LC76G backup before PMIC cutoff");
      gpsBackupPreparedForPowerButton = onboardGps.prepareForHardPowerOff();
      Serial.println(gpsBackupPreparedForPowerButton
                         ? "PWR hold: GPS backup command accepted"
                         : "PWR hold: GPS backup command failed");
      // prepareForHardPowerOff includes Quectel's one-second settling delay.
      lastPowerButtonPollMs = millis();
    }
    return;
  }

  if (physicalPowerButtonDown && gpsBackupPreparedForPowerButton) {
    // The user released before the AXP2101 hardware cutoff. Reset the receiver
    // out of its pending backup state and resume normal staged configuration.
    Serial.println("PWR hold released: resuming LC76G");
    onboardGpsPresent = onboardGps.begin(Wire, millis());
  }
  if (physicalPowerButtonDown && rideAutoSavedForPowerButton) {
    routeNavigationEnabled = false;
    ridePaused = false;
    rideAutoPaused = false;
    currentSpeedMph = 0.0f;
    openHome();
    notifyPhone("ready");
    Serial.println("PWR hold released after ride auto-save; returned Home");
  }
  physicalPowerButtonDown = false;
  physicalPowerButtonDownMs = 0;
  gpsBackupAttemptedForPowerButton = false;
  gpsBackupPreparedForPowerButton = false;
  rideAutoSavedForPowerButton = false;
}

bool requestPmicHardPowerOff() {
  if (!pmicAvailable) return false;
  // USB insertion is the intended wake path once PWR is inaccessible. Do not
  // command PMIC shutdown while VBUS is already present: that can immediately
  // re-power the board instead of producing a stable off state.
  if (power.isVbusIn()) {
    Serial.println("Hard power-off skipped: USB-C is present");
    return false;
  }
  Serial.println("Inactivity deadline: PMIC hard power-off");
  // Timer-only boot has not initialized GPS. Detect its bridge so the backup
  // command is sent before PMIC removes the main GPS supply.
  if(!onboardGps.detected())onboardGps.begin(Wire,millis());
  if(onboardGps.detected() && !onboardGps.prepareForHardPowerOff())
    Serial.println("GPS backup command failed; backup state is unconfirmed");
  power.disableBattDetection();
  power.disableBattVoltageMeasure();
  power.disableVbusVoltageMeasure();
  // With USB absent, BATFET-off leaves only the PMIC RTC domain powered. A
  // later USB-C insertion is the hardware power-on event.
  power.disableBATFET();
  delay(20);
  power.shutdown();
  delay(100);  // Normally unreachable: PMIC rails have just been removed.
  return true;
}

void shutDownAfterTimerBoot() {
  // Timer wake initializes only I2C, PMIC, and the GPS backup command.
  Wire.begin(IIC_SDA, IIC_SCL);
  pmicAvailable = power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!pmicAvailable) {
    Serial.println("PMIC unavailable on hard-off timer wake");
    return;
  }
  requestPmicHardPowerOff();
}

void enterDeepSleep(bool manualShutdown = false) {
  wifiConfig.stop();
  riderNetwork.pause();
  radarClient.stopForSleep();
  if (manualShutdown && rideSessionIsActive() &&
      distanceMiles > AUTO_SAVE_RIDE_MIN_MILES) {
    const uint32_t shutdownMs = millis();
    preserveRideTailForShutdown(shutdownMs);
    autoSaveRideBeforeHardPowerOff(activeRideElapsedMs(shutdownMs));
  }
  saveOdometers();
  if (activeGpsFile) {
    activeGpsFile.flush();
    activeGpsFile.close();
  }
  showShutdownSplash();
  display->setBrightness(0);
  display->displayOff();
  onboardGps.prepareForSleep();

  // Do not let the tap that selected YES satisfy the wake condition, and keep
  // the active-low IRQ pulled high after the digital GPIO domain powers down.
  prepareTouchWakePin(manualShutdown);

  // The CST9217 stays powered because its active-low IRQ on RTC GPIO 11 is the
  // only configured wake source. Do not deinitialize NimBLE here: when a phone
  // is connected, deinit can free the server's peer map while the NimBLE host
  // task is processing the disconnect event. Deep sleep powers the Bluetooth
  // controller and digital domain down without requiring software teardown,
  // and wake performs a normal cold boot through setup().
  if (pmicAvailable) {
    power.disableBattDetection();
    power.disableBattVoltageMeasure();
    power.disableVbusVoltageMeasure();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_11, 0);
  armHardPowerOffTimer(manualShutdown ? HARD_POWER_OFF_MANUAL_US
                                      : remainingHardPowerOffUs(millis()));
  esp_deep_sleep_start();
}

void enterLowPowerSleep(bool manualShutdown = false) {
  enterDeepSleep(manualShutdown);
}

void enterInactiveSleep() {
  wifiConfig.stop();
  riderNetwork.pause();
  radarClient.stopForSleep();
  // During an active ride, retain RAM and the open GPS log so the same ride
  // can continue after touch wake. NimBLE must be stopped before manual light
  // sleep; otherwise ESP-IDF may reject sleep and return immediately.
  const uint32_t sleepStartMs = millis();
  preserveRideTailForShutdown(sleepStartMs);
  const uint32_t retainedRideElapsedMs = activeRideElapsedMs(sleepStartMs);
  saveOdometers();
  if (activeGpsFile) activeGpsFile.flush();
  showShutdownSplash();
  display->setBrightness(0);
  display->displayOff();
  onboardGps.prepareForSleep();

  prepareTouchWakePin();

  // The BLE server is initialized even when the user has disabled radio
  // advertising, so always tear it down before retained light sleep.
  notifications.endForLightSleep();
  statusCharacteristic = nullptr;
  deviceNameCharacteristic = nullptr;
  phoneConnected = false;

  if (pmicAvailable) {
    power.disableBattDetection();
    power.disableBattVoltageMeasure();
    power.disableVbusVoltageMeasure();
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_11, 0);
  armHardPowerOffTimer(remainingHardPowerOffUs(millis()));
  const esp_err_t sleepResult = esp_light_sleep_start();
  const esp_sleep_wakeup_cause_t lightSleepWakeCause = esp_sleep_get_wakeup_cause();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  bool automaticallySavedRide = false;
  if (lightSleepWakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    automaticallySavedRide =
        autoSaveRideBeforeHardPowerOff(retainedRideElapsedMs);
    if (requestPmicHardPowerOff()) {
      // requestPmicHardPowerOff normally removes the rails. If a board-level
      // wiring change prevented that, do not resume an abandoned ride.
      return;
    }
  }
  restoreTouchWakePin();
  Serial.printf("Light sleep returned: %s, wake=%d\n",
                esp_err_to_name(sleepResult), int(lightSleepWakeCause));

  const uint32_t wakeMs = millis();
  onboardGps.resumeAfterLightSleep(wakeMs);
  if (onboardGpsPresent) {
    portENTER_CRITICAL(&locationMux);
    hasLocation = false;
    lastLocationMs = 0;
    bleBarometerAvailable = false;
    lastBleBarometerMs = 0;
    portEXIT_CRITICAL(&locationMux);
    lastOnboardGpsSequence = 0;
  }

  // Recreate the BLE server and characteristics after the light-sleep wake.
  // The phone can reconnect while the retained ride resumes from the same
  // distance, timer, samples, and active GPS file.
  setupBluetooth();
  radarClient.begin(RADAR_DEVICE_NAME);
  radarClient.setSettings(radarSettings);
  radarClient.setEnabled(radarEnabled,millis());

  if (pmicAvailable) {
    power.enableBattDetection();
    power.enableBattVoltageMeasure();
    power.enableVbusVoltageMeasure();
    Serial.printf("PMIC PWR hardware-off hold: %u seconds\n",
                  4u + 2u * power.getPowerKeyPressOffTime());
    updateBattery(millis(), true);
  }
  // CO5300 display RAM survives light sleep and still contains the BYO
  // shutdown frame. Replace it while emission is disabled so that frame can
  // never flash when the panel is turned back on.
  canvas->fillScreen(BLACK);
  canvas->flush();
  hasFramebufferHash = false;
  display->displayOn();
  display->setBrightness(normalBrightness);
  displayAutoDimmed = false;
  // Original startup splash fallback (kept intentionally):
  // showBrandSplash("HIO", 2000);
  showPedalOneStartupSplash();

  // USB-C intentionally prevents the PMIC from cutting power. If the
  // unattended ride was already finalized at the 90-minute deadline, return
  // to Home rather than resuming a ride whose GPS log has been archived.
  if (automaticallySavedRide) {
    demoRideActive = false;
    routeNavigationEnabled = false;
    ridePaused = false;
    rideAutoPaused = false;
    resetGpsSpeedFilter(wakeMs);
    openHome();
    lastActivityMs = wakeMs;
    zeroSpeedSinceMs = wakeMs;
    notifyPhone("ready");
    return;
  }

  // Resume the same ride without integrating the sleeping interval as motion
  // or backfilling it with fake minute/GPS samples. Re-baseline the phone's
  // altitude sources onto the retained fused elevation on their first packet.
  ridePreviousMs = wakeMs;
  minuteAccumulatorMs = wakeMs;
  nextAltitudeSampleMs = wakeMs + 60000;
  nextGpsSampleMs = wakeMs;
  resetGpsSpeedFilter(wakeMs);
  barometerInitialized = false;
  gpsAltitudeInitialized = false;
  lastAltitudePacketMs = 0;
  gradeSampleHead = gradeSampleCount = 0;
  lastGradeSampleMs = 0;
  liveGradePercent = 0.0f;
  liveGradeValid = false;
  gradeMotionActive = false;
  touchPending = false;
  touchActive = false;
  lastActivityMs = wakeMs;
  zeroSpeedSinceMs = lastActivityMs;
  previousDrawMs = 0;
}

void moveSelection(int direction, uint32_t now) {
  wakeDisplay(now);
  // CST9217 can report a short second contact after release. Without the
  // lockout, that ghost tap advances another page and makes the first target
  // page look as though it was skipped.
  ignoreTouchUntilMs = now + PAGE_TOUCH_LOCKOUT_MS;
  if (appState == RIDING) {
    int pages[MAX_RIDE_PAGE_COUNT];
    const int pageCount=visibleRidePages(pages);
    const int position=visibleRidePagePosition(currentPage);
    const int previousPage=currentPage;
    const int nextPage=pages[(position+direction+pageCount)%pageCount];
    if(previousPage==gpx::PAGE_INDEX && nextPage!=gpx::PAGE_INDEX)
      captureDisplayedMapFrame(now);
    currentPage=nextPage;
    lastRidePageChangeMs=now;
    if (currentPage == gpx::PAGE_INDEX) presentPreparedMapFrame(now);
  }
  else if (appState == SUMMARY) summaryPage = (summaryPage + direction + 4) % 4;
  else if (appState == ANCS_TEST && ancsHistoryCount) {
    const int next = int(ancsHistoryPage) + direction;
    ancsHistoryPage = constrain(next, 0, int(ancsHistoryCount));
  }
  else if (appState == MENU || appState == OPTIONS_MENU || appState == RIDE_MENU) {
    const int count = appState == MENU ? 1 : (appState == OPTIONS_MENU ? 4 : 2);
    menuSelection = (menuSelection + direction + count) % count;
  } else if (appState == DISPLAY_PAGE) {
    int next = int(normalBrightness) + direction * 16;
    normalBrightness = constrain(next, 0, 255);
    displayAutoDimmed = false; display->setBrightness(normalBrightness);
  }
  previousDrawMs = 0;
}

void requestConfirmation(PendingAction action, AppState returnState) {
  pendingAction = action;
  confirmReturnState = returnState;
  odometerResetArmed = false;
  odometerResetSliderActive = false;
  appState = CONFIRM_PAGE;
  previousDrawMs = 0;
}

void activate(uint32_t now) {
  // Prevent a release/bounce from activating the mostly-black area of the
  // destination screen and immediately navigating back.
  ignoreTouchUntilMs = now + MENU_TOUCH_LOCKOUT_MS;
  if(appState==RIDE_CANCELED) {
    openHome();
  } else if (appState == RIDING) {
    if(!routeNavigationEnabled && currentPage==gpx::PAGE_INDEX) {
      const int zoom=freeRideMapZoomControl(lastTapX,lastTapY);
      if(zoom) {adjustFreeRideMapZoom(zoom,now);return;}
    }
    if (lastTapY >= SCREEN_SIZE * 78 / 100) openRideMenu();
  } else if (appState == CONFIRM_PAGE) {
    const PendingAction action = pendingAction;
    if (action == ACTION_RESET_ODOMETER) {
      if (odometerResetArmed) {
        // A short/incomplete slider touch produces no gesture. Any separate
        // tap on the surrounding popup or black area cancels safely.
        odometerResetArmed = false;
        odometerResetSliderActive = false;
        pendingAction = ACTION_NONE;
        appState = confirmReturnState;
        previousDrawMs = 0;
        return;
      }
      const bool ok = lastTapX >= ux(84) && lastTapX <= ux(156) &&
                      lastTapY >= uy(144) && lastTapY <= uy(186);
      if (ok) {
        odometerResetArmed = true;
        odometerResetSliderX = ux(77);
        ignoreTouchUntilMs = now + MENU_TOUCH_LOCKOUT_MS;
        previousDrawMs = 0;
      } else {
        pendingAction = ACTION_NONE;
        appState = confirmReturnState;
        previousDrawMs = 0;
      }
      return;
    }
    const bool tripReset = action == ACTION_RESET_TRIP_A ||
                           action == ACTION_RESET_TRIP_B;
    const bool yes = lastTapX >= ux(84) && lastTapX <= ux(156) &&
                     lastTapY >= uy(tripReset ? 128 : 105) &&
                     lastTapY <= uy(tripReset ? 174 : 141);
    pendingAction = ACTION_NONE;
    if (!yes) {
      if (action == ACTION_DELETE_RIDE) pendingRideDeleteIndex = -1;
      appState = confirmReturnState;
      previousDrawMs = 0;
    }
    else if (action == ACTION_END_RIDE) prepareRideEnd(now);
    else if (action == ACTION_POWER_OFF) enterLowPowerSleep(true);
    else if (action == ACTION_DELETE_RIDE) {
      if (pendingRideDeleteIndex >= 0) deleteSavedRide(pendingRideDeleteIndex);
      appState = RIDES_LIST;
      previousDrawMs = 0;
    }
    else {
      resetTrip(action == ACTION_RESET_TRIP_A);
      appState = STATUS_PAGE;
      statusPage = 1;
      previousDrawMs = 0;
    }
  } else if (appState == READY) {
    const bool onLetsGo = lastTapX >= ux(20) && lastTapX <= ux(220) &&
                          lastTapY >= uy(96) && lastTapY <= uy(158);
    if (onLetsGo) {
      appState = START_ROUTE_MENU;
      previousDrawMs = 0;
    }
  }
  else if (appState == MENU) {
    const bool onStart = lastTapX >= ux(20) && lastTapX <= ux(220) &&
                         lastTapY >= uy(80) && lastTapY <= uy(160);
    // Keep the visual button compact, but provide a generous touch target.
    const bool onMenu = lastTapX >= ux(60) && lastTapX <= ux(180) &&
                        lastTapY >= uy(185) && lastTapY <= uy(239);
    if (onStart) { appState=START_ROUTE_MENU;previousDrawMs=0; }
    else if (onMenu) openOptionsMenu();
    else { appState = READY; previousDrawMs = 0; }
  } else if (appState == OPTIONS_MENU) {
    const bool inIcons = lastTapY >= uy(42) && lastTapY <= uy(156);
    const bool left = inIcons && lastTapX >= ux(18) && lastTapX <= ux(117);
    const bool right = inIcons && lastTapX >= ux(123) && lastTapX <= ux(222);
    const bool onRadarLabel = menuSelection == 3 && left;
    if (onRadarLabel && radarClient.state() == RadarClient::STATE_CONNECTED) {
      radarSettings = radarClient.settings();
      radarSettingsDirty = false;
      radarSettingsReturnState = OPTIONS_MENU;
      appState = RADAR_SETTINGS;
      previousDrawMs = 0;
      return;
    }
    if (onRadarLabel) {
      if (!bluetoothEnabled) {
        bluetoothEnabled = true;
        notifications.setEnabled(true);
        if (deviceStorageReady) devicePreferences.putBool("ble_on", true);
      }
      // Discovery is provisional. Enable/persist the ride page only after
      // connection succeeds, and reset a previous failed attempt first.
      radarEnabled = false;
      radarClient.setEnabled(false, now);
      radarClient.setEnabled(true, now);
      appState = RADAR_SETUP;
      previousDrawMs = 0;
      return;
    }
    int hit = -1;
    // Existing action IDs: Info, Routes, Bluetooth, History, Display, ODO.
    static const int actions[3][2] = {{0,2},{1,3},{4,5}};
    if(menuSelection < 3 && (left || right))
      hit = actions[menuSelection][right ? 1 : 0];
    // Dots are also direct page targets; icon taps open their named action.
    if(lastTapY >= uy(160) && lastTapY <= uy(180) &&
       lastTapX >= ux(90) && lastTapX <= ux(150)) {
      menuSelection=constrain((lastTapX-ux(92))/us(14),0,3);
      previousDrawMs=0;
      return;
    }
    // Touching the slider without dragging it far enough leaves this menu in
    // place. Black space beside it retains the universal Home action.
    const bool onPowerSlider = lastTapX >= ux(63) && lastTapX <= ux(177) &&
                               lastTapY >= uy(176) && lastTapY <= uy(232);
    if (onPowerSlider) return;
    if (hit < 0) { openHome(); return; }
    if (hit == 0) {
      statusReturnState = OPTIONS_MENU;
      statusPage = 0;
      appState = STATUS_PAGE;
      previousDrawMs = 0;
    } else if (hit == 1) {
      routeSelectionForStart=false;selectedRouteRow=0; appState = ROUTES_LIST; previousDrawMs = 0;
    } else if (hit == 2) { appState=BLUETOOTH_PAGE;previousDrawMs=0; }
    else if (hit == 3) {
      ridesEditMode = false;
      appState = RIDES_LIST;
      previousDrawMs = 0;
    } else if (hit == 4) openDisplayPage(now, OPTIONS_MENU);
    else if (hit == 5) {
      statusReturnState = OPTIONS_MENU;
      statusPage = 1;
      appState = STATUS_PAGE;
      previousDrawMs = 0;
    }
  } else if (appState == ANCS_TEST) {
    const bool exit = lastTapX >= ux(65) && lastTapX <= ux(175) &&
                      lastTapY >= uy(194) && lastTapY <= uy(239);
    if (exit) {
      notifications.setAcceptAll(false);
      openOptionsMenu();
    } else if (ancsHistoryCount && ancsHistoryPage == ancsHistoryCount &&
               lastTapX >= ux(50) && lastTapX <= ux(190) &&
               lastTapY >= uy(132) && lastTapY <= uy(190)) {
      ancsHistoryCount = 0;
      ancsHistoryPage = 0;
      previousDrawMs = 0;
    }
  } else if (appState == SAVE_RIDE_PROMPT) {
    const bool save = lastTapX >= ux(43) && lastTapX <= ux(115) &&
                      lastTapY >= uy(105) && lastTapY <= uy(170);
    const bool discard = lastTapX >= ux(125) && lastTapX <= ux(197) &&
                         lastTapY >= uy(105) && lastTapY <= uy(170);
    if (save) completeRideEnd(true);
    else if (discard) completeRideEnd(false);
  } else if (appState == RIDES_LIST) {
    const bool editButton = lastTapX >= ux(65) && lastTapX <= ux(175) &&
                            lastTapY >= uy(198) && lastTapY <= uy(239);
    if (editButton) {
      ridesEditMode = !ridesEditMode;
      previousDrawMs = 0;
      return;
    }
    int hit = -1;
    for (uint8_t i = 0; i < savedRideCount; ++i) {
      if (abs(lastTapY - uy(82 + i * 27)) <= uy(13)) {
        hit = i;
        break;
      }
    }
    if (ridesEditMode && hit >= 0 && lastTapX >= ux(188)) {
      pendingRideDeleteIndex = hit;
      requestConfirmation(ACTION_DELETE_RIDE, RIDES_LIST);
    } else if (!ridesEditMode && hit >= 0) loadSavedRide(hit);
    else openOptionsMenu();
  } else if (appState == RIDE_MENU) {
    const bool onStatus = lastTapX>=ux(20) && lastTapX<ux(120) &&
                          lastTapY>=uy(88) && lastTapY<uy(174);
    const bool onBrightness = lastTapX>=ux(120) && lastTapX<=ux(220) &&
                              lastTapY>=uy(88) && lastTapY<uy(174);
    const bool onPauseResume = lastTapX >= ux(8) && lastTapX <= ux(119) &&
                               lastTapY >= uy(174);
    const bool onStop = lastTapX >= ux(121) && lastTapX <= ux(232) &&
                        lastTapY >= uy(174);
    if (onStatus) {
      statusReturnState = RIDE_MENU;
      statusPage = 0;
      appState = STATUS_PAGE;
      previousDrawMs = 0;
    } else if (onBrightness) {
      openDisplayPage(now);
    } else if (onPauseResume) {
      toggleRidePause(now);
    } else if (onStop) {
      requestConfirmation(ACTION_END_RIDE, RIDE_MENU);
    } else {
      appState = RIDING;
      previousDrawMs = 0;
    }
  } else if (appState == STATUS_PAGE) {
    if (statusPage == 1) {
      const bool onTripA = lastTapX >= ux(20) && lastTapX <= ux(220) &&
                           lastTapY >= uy(96) && lastTapY <= uy(137);
      const bool onTripB = lastTapX >= ux(20) && lastTapX <= ux(220) &&
                           lastTapY > uy(137) && lastTapY <= uy(176);
      if (onTripA || onTripB) {
        requestConfirmation(onTripA ? ACTION_RESET_TRIP_A : ACTION_RESET_TRIP_B,
                            STATUS_PAGE);
        return;
      }
      const bool onResetOdo = lastTapX >= ux(60) && lastTapX <= ux(180) &&
                              lastTapY >= uy(178) && lastTapY <= uy(232);
      if (onResetOdo) {
        requestConfirmation(ACTION_RESET_ODOMETER, STATUS_PAGE);
        return;
      }
    }
    if (statusReturnState == RIDE_MENU) {
      openRideMenu();
    } else if (statusReturnState == OPTIONS_MENU) openOptionsMenu();
    else openMenu();
  } else if (appState == DISPLAY_PAGE) {
    if (displayReturnState == OPTIONS_MENU) openOptionsMenu();
    else openRideMenu();
  }
  else if (appState == SUMMARY) {
    const bool done = summaryPage == 3 &&
        lastTapX >= ux(70) && lastTapX <= ux(170) &&
        lastTapY >= uy(190) && lastTapY <= uy(232);
    if (done) {
      if (!viewingSavedRide && rideStorageReady &&
          strcmp(summaryGpsPath, ACTIVE_GPS_PATH) == 0)
        FFat.remove(ACTIVE_GPS_PATH);
      appState = viewingSavedRide ? RIDES_LIST : READY;
      if (viewingSavedRide) ridesEditMode = false;
      previousDrawMs = 0;
      if (!viewingSavedRide) notifyPhone("ready");
    } else {
      if (lastTapX >= CENTER && summaryPage < 3) ++summaryPage;
      else if (lastTapX < CENTER && summaryPage > 0) --summaryPage;
      previousDrawMs = 0;
    }
  }
}

void setup() {
  bootWakeCause = esp_sleep_get_wakeup_cause();
  bootResetReason = esp_reset_reason();
  Serial.begin(115200);
  Serial.printf("Boot wake=%s (%d), reset=%s (%d)\n",
                wakeCauseName(bootWakeCause), int(bootWakeCause),
                resetReasonName(bootResetReason), int(bootResetReason));
  if (bootWakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    shutDownAfterTimerBoot();
  }
  setCpuFrequencyMhz(CPU_BOOST_MHZ);
  activeCpuMhz = CPU_BOOST_MHZ;
  // Mount ride storage before the framebuffer and BLE stack reserve memory.
  // If the known-bad wear-levelling metadata is encountered, 2.0.21 performs
  // its guarded one-time rebuild and restores the five backed-up ride files.
  rideStorageReady = initializeRideStorage();
  if (rideStorageReady) {
    scanSavedRides();
    Serial.printf("FFat ready: %u rides, %u/%u bytes used\n",
                  unsigned(savedRideCount), unsigned(FFat.usedBytes()),
                  unsigned(FFat.totalBytes()));
  } else {
    Serial.println("FFat initialization failed; ride storage unavailable");
  }
  Wire.begin(IIC_SDA, IIC_SCL);
  pmicAvailable = power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (pmicAvailable) {
    power.enableBattDetection();
    power.enableBattVoltageMeasure();
    power.enableVbusVoltageMeasure();
    updateBattery(millis(), true);
  } else {
    Serial.println("AXP2101 initialization failed; battery gauge unavailable");
  }
  touch.setPins(TP_RESET, TP_INT);
  if (!touch.begin(Wire, CST92XX_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("CST9217 initialization failed");
    while (true) delay(1000);
  }
  Wire.setBufferSize(1100);
  Wire.setTimeOut(100);
  touch.setMaxCoordinates(SCREEN_SIZE, SCREEN_SIZE);
  touch.setMirrorXY(true, true);
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), onTouchInterrupt, FALLING);
  if (!canvas->begin()) {
    Serial.println("Display/canvas initialization failed");
    while (true) delay(1000);
  }
  display->setBrightness(normalBrightness);
  // Original startup splash fallback (kept intentionally):
  // showBrandSplash("HIO", 2000);
  const uint32_t splashLogoReadyMs = showPedalOneStartupSplashIntro();
  onboardGpsPresent = onboardGps.begin(Wire, millis());
  Serial.printf("Location source: %s\n",
                onboardGpsPresent ? "onboard LC76G" : "BLE GPS relay");
  setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
  tzset();
  gpxLibrary.restore(rideStorageReady,FW_VERSION);
  loadOdometers();
  loadDeviceNickname();
  loadRadarSettings();
  if(deviceStorageReady)bluetoothEnabled=devicePreferences.getBool("ble_on",true);
  if(deviceStorageReady)radarEnabled=devicePreferences.getBool("radar_on",false);
  if(radarEnabled)bluetoothEnabled=true;
  ridarEnabled=false;
  riderNetwork.setEnabled(false);
  setupBluetooth();
  radarClient.setSettings(radarSettings);
  radarClient.begin(RADAR_DEVICE_NAME);
  radarClient.setEnabled(radarEnabled,millis());
  // Initialization ran behind the completed logo. Exit only after everything
  // needed by the home screen is ready, so there is no blank interstitial.
  finishPedalOneStartupSplash(splashLogoReadyMs);
  lastActivityMs = millis();
  drawReadyScreen(millis());
  if (PEDALONE_GPX_DEMO_BOOT) {
    gpxSimulation.reset(millis()); appState = GPX_DEMO; previousDrawMs = 0;
    Serial.printf("GPX playback: %.3f mi at 40 mph\n", gpx::length()/1609.344f);
  }
}

void loop() {
  uint32_t now = millis();
  updateDynamicCpuClock(now);
  servicePhysicalPowerButton(now);
  now = millis();
  // Read touch before radio, storage, radar, map, or GPS housekeeping. These
  // services are cooperative and some perform blocking work; once a contact
  // is pending, held, or completed, defer them for this loop so UI input wins.
  const bool otaOwnsUi=otaUpdate.active() || otaUpdate.rebootPending();
  const Gesture gesture=otaOwnsUi ? GESTURE_NONE : pollTouch(now);
  const auto touchHasPriority=[&]() {
    return touchPending || touchActive || gesture!=GESTURE_NONE;
  };
  if(!touchHasPriority())
    wifiConfig.loop(otaCanStart() && !otaUpdate.active() &&
                    !otaUpdate.rebootPending(),phoneConnected);
  if(bluetoothEnabled && !touchHasPriority()) {
    notifications.loop();
    otaUpdate.loop(phoneConnected);
  }
  if(!touchHasPriority()) radarClient.service(now);
  updateRadarWarningState(now);
  const bool bulkTransferActive = otaUpdate.active() || sharedMap.active() ||
      gpxLibrary.active();
  if (bulkTransferActive != otaLinkFast) {
    otaLinkFast = bulkTransferActive;
    notifications.setOtaMode(otaLinkFast);
  }
  if(!touchHasPriority()) updateBattery(now);
  const bool routeCanChange = (appState==READY || appState==MENU || appState==OPTIONS_MENU ||
      appState==GPX_DEMO || appState==ROUTES_LIST || appState==START_ROUTE_MENU) && !otaUpdate.active() && !otaUpdate.rebootPending();
  if(bluetoothEnabled && !touchHasPriority()) gpxLibrary.loop(routeCanChange);
  if(bluetoothEnabled && !touchHasPriority()) sharedMap.loop(routeCanChange);
  // Map packages, GPX files, and previews can cross the inactivity limit.
  // Treat every queued or active transfer packet as user activity so neither
  // sleep tier can interrupt the file while the phone is sending it.
  if (sharedMap.active() || gpxLibrary.active()) lastActivityMs = now;
  if(gpxLibrary.changed) {
    gpxLibrary.changed=false; gpxSimulation.reset(now);
    gpxLastProgress=-1;gpxRecoveryProgress=0;gpxLiveBearing=NAN;
    gpxOffCourseStartRideMeters=-1;gpxOffCourseStartProgress=0;
    gpxOffCourseSinceMs=0;
    gpxGuidance.reset();gpxFixReceived=0;
    previousDrawMs=0;
  }
  LocationPacket ridarLocation{};
  uint32_t ridarLocationMs=0;
  bool ridarLocationValid=false;
  portENTER_CRITICAL(&locationMux);
  ridarLocation=latestLocation;
  ridarLocationMs=lastLocationMs;
  ridarLocationValid=hasLocation;
  portEXIT_CRITICAL(&locationMux);
  ridarLocationValid=ridarLocationValid &&
      timestampIsFresh(now,ridarLocationMs,LOCATION_TIMEOUT_MS) &&
      ridarLocation.latitudeE7>=-850000000 && ridarLocation.latitudeE7<=850000000 &&
      ridarLocation.longitudeE7>=-1800000000 && ridarLocation.longitudeE7<=1800000000;
  riderNetwork.setEnabled(ridarEnabled);
  const bool ridarIconAvailable=deviceIcon.availableFor(deviceEmoji);
  const bool ridersChanged=!touchHasPriority() && riderNetwork.service(
      wifiConfig.busy() || otaUpdate.active() || otaUpdate.rebootPending(),
      ridarLocationValid,ridarLocation.latitudeE7,ridarLocation.longitudeE7,
      deviceNickname,
      ridarIconAvailable ? deviceIcon.pixels() : nullptr,
      ridarIconAvailable ? deviceIcon.width() : 0,
      ridarIconAvailable ? deviceIcon.height() : 0,
      ridarIconAvailable ? deviceIcon.pixelChecksum() : 0,now);
  if(ridersChanged && appState==RIDING && currentPage==RIDAR_PAGE_INDEX)
    previousDrawMs=0;
  if (otaUpdate.active() || otaUpdate.rebootPending()) {
    if (!previousDrawMs || now - previousDrawMs >= 250) {
      previousDrawMs = now;
      drawOtaUpdate(now);
    }
    lastActivityMs = now;
    delay(5);
    return;
  }
  // Keep acquisition running on route selection and playback screens too:
  // those modal screens return before the normal ride drawing path.
  if (onboardGpsPresent && !touchHasPriority()) {
    onboardGps.service(millis());
    publishOnboardGpsFix(millis());
  }
  now=millis(); // GPS reads may have published a fix after this loop began.
  if (gesture == GESTURE_TAP && appState == RIDING &&
      currentPage == RADAR_PAGE_INDEX &&
      lastTapY >= SCREEN_SIZE * 80 / 100) {
    radarSettings = radarClient.settings();
    radarSettingsDirty = false;
    radarSettingsReturnState = RIDING;
    appState = RADAR_SETTINGS;
    previousDrawMs = 0;
    return;
  }
  if (gesture == GESTURE_TAP && lastTapY >= SCREEN_SIZE * 80 / 100) {
    const bool menuAlreadyOpen = appState == MENU ||
        appState == OPTIONS_MENU || appState == RIDE_MENU;
    const bool modalScreen = appState == CONFIRM_PAGE ||
        appState == SAVE_RIDE_PROMPT || appState == RADAR_PREVIEW ||
        appState == RIDE_CANCELED || appState == RADAR_SETUP;
    const bool odometerResetButton = appState == STATUS_PAGE && statusPage == 1 &&
        lastTapX >= ux(60) && lastTapX <= ux(180) &&
        lastTapY >= uy(178) && lastTapY <= uy(232);
    const bool rideListEditButton = appState == RIDES_LIST &&
        lastTapX >= ux(65) && lastTapX <= ux(175) &&
        lastTapY >= uy(198) && lastTapY <= uy(239);
    const bool routeStartButton = appState == ROUTES_LIST &&
        routeSelectionForStart && lastTapX >= ux(66) && lastTapX <= ux(174) &&
        lastTapY >= uy(182) && lastTapY <= uy(222);
    const bool summaryDoneButton = appState == SUMMARY && summaryPage == 3 &&
        lastTapX >= ux(70) && lastTapX <= ux(170) &&
        lastTapY >= uy(190) && lastTapY <= uy(232);
    const bool ancsExitButton = appState == ANCS_TEST &&
        lastTapX >= ux(65) && lastTapX <= ux(175) &&
        lastTapY >= uy(194) && lastTapY <= uy(239);
    const bool radarSetupButton = appState == RADAR_SETUP &&
        lastTapX >= ux(55) && lastTapX <= ux(185) &&
        lastTapY >= uy(168) && lastTapY <= uy(230);
    const bool radarSettingsApply = appState == RADAR_SETTINGS &&
        lastTapX >= ux(55) && lastTapX <= ux(185) &&
        lastTapY >= uy(184) && lastTapY <= uy(235);
    const bool reservedControl = odometerResetButton || rideListEditButton ||
        routeStartButton || summaryDoneButton || ancsExitButton ||
        radarSetupButton || radarSettingsApply;
    if (!menuAlreadyOpen && !modalScreen && !reservedControl) {
      const bool rideContext = appState == RIDING ||
          (appState == DISPLAY_PAGE && displayReturnState == RIDE_MENU) ||
          (appState == STATUS_PAGE && statusReturnState == RIDE_MENU);
      ignoreTouchUntilMs = now + MENU_TOUCH_LOCKOUT_MS;
      if (rideContext) {
        openRideMenu();
      } else {
        if (appState == ANCS_TEST) notifications.setAcceptAll(false);
        if (appState == COUNTDOWN) {
          routeNavigationEnabled = false;
          routeSelectionForStart = false;
        }
        openOptionsMenu();
      }
      return;
    }
  }
  if(appState==BLUETOOTH_PAGE) {
    if(gesture==GESTURE_UP) {openOptionsMenu();return;}
    if(gesture==GESTURE_TAP || gesture==GESTURE_LEFT || gesture==GESTURE_RIGHT) {
      if(gesture==GESTURE_TAP && (lastTapY<uy(105) || lastTapY>uy(165))) {
        openOptionsMenu();return;
      }
      const bool enabled=gesture==GESTURE_RIGHT || (gesture==GESTURE_TAP && lastTapX>=CENTER);
      if(enabled!=bluetoothEnabled) {
        bluetoothEnabled=enabled;
        if(deviceStorageReady)devicePreferences.putBool("ble_on",enabled);
        notifications.setEnabled(enabled);
        if(!enabled) {
          phoneConnected=false;
          radarEnabled=false;
          radarClient.setEnabled(false,now);
          if(deviceStorageReady)devicePreferences.putBool("radar_on",false);
        }
      }
      previousDrawMs=0;
    }
    if(!previousDrawMs || now-previousDrawMs>=1000) {
      previousDrawMs=now;drawBluetoothPage(now);
    }
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS)enterLowPowerSleep();
    delay(5);return;
  }
  if(appState==RADAR_SETUP) {
    const RadarClient::State state=radarClient.state();
    const bool swipeBack=gesture==GESTURE_UP || gesture==GESTURE_LEFT ||
                         gesture==GESTURE_RIGHT;
    const bool inButton=lastTapX>=ux(55) && lastTapX<=ux(185) &&
                        lastTapY>=uy(168) && lastTapY<=uy(230);
    const bool timedOutOK=gesture==GESTURE_TAP && inButton &&
                          state==RadarClient::STATE_TIMED_OUT;
    if(swipeBack || timedOutOK) {
      radarEnabled=state==RadarClient::STATE_CONNECTED;
      if(!radarEnabled)radarClient.setEnabled(false,now);
      if(deviceStorageReady)devicePreferences.putBool("radar_on",radarEnabled);
      ignoreTouchUntilMs=now+MENU_TOUCH_LOCKOUT_MS;
      openOptionsMenu();menuSelection=3;return;
    }
    if(state==RadarClient::STATE_CONNECTED &&
       radarClient.configState()!=RadarClient::CONFIG_APPLYING) {
      radarEnabled=true;
      if(deviceStorageReady)devicePreferences.putBool("radar_on",true);
      ignoreTouchUntilMs=now+MENU_TOUCH_LOCKOUT_MS;
      openOptionsMenu();menuSelection=3;return;
    }
    if(!previousDrawMs || now-previousDrawMs>=100) {
      previousDrawMs=now;drawRadarSetupPage(now);
    }
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS)enterLowPowerSleep();
    delay(5);return;
  }
  if(appState==RADAR_SETTINGS) {
    const bool settingsDuringRide = radarSettingsReturnState == RIDING;
    if(settingsDuringRide) {
      updateRideData(now);
      updateAutoBrightness(now);
      if(routeNavigationEnabled)updateGpxPosition(now);
    }
    const bool liveTestTap = gesture == GESTURE_TAP &&
        lastTapX > ux(RADAR_SETTINGS_CONTROL_RIGHT) &&
        lastTapY < uy(184);
    if(liveTestTap && !settingsDuringRide && radarEnabled) {
      appState=RADAR_PREVIEW;
      previousDrawMs=0;
      return;
    }
    if(gesture==GESTURE_UP) {
      if(settingsDuringRide) {
        appState=RIDING;
        if(!radarEnabled && currentPage==RADAR_PAGE_INDEX)currentPage=0;
        previousDrawMs=0;
      } else {
        openOptionsMenu();
      }
      return;
    }
    if(gesture==GESTURE_TAP) {
      const bool onApply=lastTapX>=ux(55) && lastTapX<=ux(185) &&
                         lastTapY>=uy(184) && lastTapY<=uy(235);
      if(onApply) {
        saveRadarSettings();
        radarClient.setSettings(radarSettings);
        radarClient.applySettings(radarSettings,now);
        radarSettingsDirty=false;
        previousDrawMs=0;
      } else {
        const bool steadyTap=abs(touchLastX-touchStartX)<=us(8) &&
                             abs(touchLastY-touchStartY)<=us(8);
        const bool insideControls=
            lastTapX>=ux(RADAR_SETTINGS_CONTROL_LEFT) &&
            lastTapX<=ux(RADAR_SETTINGS_CONTROL_RIGHT);
        if(steadyTap && insideControls) {
          for(uint8_t row=0;row<6;++row) {
            const int centerY=uy(79+row*19);
            if(abs(lastTapY-centerY)<=us(9)) {
              adjustRadarSetting(row,lastTapX<CENTER ? -1 : 1);
              previousDrawMs=0;
              break;
            }
          }
        }
      }
    }
    if(!previousDrawMs || now-previousDrawMs>=200) {
      previousDrawMs=now;drawRadarSettingsPage(now);
    }
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS) {
      if(settingsDuringRide)enterInactiveSleep();
      else enterLowPowerSleep();
    }
    delay(5);return;
  }
  if(appState==RADAR_PREVIEW) {
    if(!radarEnabled) {openOptionsMenu();return;}
    const bool bottomTap=gesture==GESTURE_TAP &&
        lastTapY>=SCREEN_SIZE*80/100;
    const bool settingsTap=gesture==GESTURE_TAP &&
        lastTapX<ux(RADAR_SETTINGS_CONTROL_LEFT) &&
        lastTapY<SCREEN_SIZE*80/100;
    if(gesture==GESTURE_UP || settingsTap || bottomTap) {
      radarSettings=radarClient.settings();
      radarSettingsDirty=false;
      radarSettingsReturnState=OPTIONS_MENU;
      appState=RADAR_SETTINGS;
      previousDrawMs=0;
      return;
    }
    const uint32_t refreshMs=radarClient.state()==RadarClient::STATE_CONNECTED
        ? 100 : 500;
    if(!previousDrawMs || now-previousDrawMs>=refreshMs) {
      previousDrawMs=now;drawRadarPage(now);
    }
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS)
      enterLowPowerSleep();
    delay(5);return;
  }
  if(appState==START_ROUTE_MENU) {
    if(gesture==GESTURE_START_DEMO) {
      startDemoRide(now);
      return;
    }
    if(gesture==GESTURE_UP) {openMenu();return;}
    if(gesture==GESTURE_TAP) {
      const bool inButtonColumn=lastTapX>=ux(26) && lastTapX<=ux(214);
      if(inButtonColumn && lastTapY>=uy(80) && lastTapY<=uy(128)) {
        ignoreTouchUntilMs=now+MENU_TOUCH_LOCKOUT_MS;routeSelectionForStart=true;selectedRouteRow=0;appState=ROUTES_LIST;previousDrawMs=0;
      } else if(inButtonColumn && lastTapY>=uy(142) && lastTapY<=uy(190)) {
        routeNavigationEnabled=false;resetFreeRideMap(now);startCountdown(now);
      } else {
        openMenu();
      }
    }
    if(appState==START_ROUTE_MENU && (!previousDrawMs || now-previousDrawMs>=500)) {
      previousDrawMs=now;drawStartRouteMenu();
    }
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS)enterLowPowerSleep();
    delay(5);return;
  }
  if(appState==ROUTES_LIST) {
    if(lastActivityMs && now-lastActivityMs>=AUTO_POWER_OFF_MS) {enterLowPowerSleep();return;}
    const int count=max(1,int(gpxLibrary.routeCount));
    selectedRouteRow=constrain(selectedRouteRow,0,count-1);
    if(gesture==GESTURE_UP) {
      appState=routeSelectionForStart ? START_ROUTE_MENU : OPTIONS_MENU;
      previousDrawMs=0;
      return;
    }
    if(gesture==GESTURE_TAP) {
      const bool onStart=gpxLibrary.routeCount && routeSelectionForStart &&
          lastTapY>=uy(182) && lastTapY<=uy(222) &&
          lastTapX>=ux(66) && lastTapX<=ux(174);
      const bool onLeft=lastTapX<=ux(58) && lastTapY>=uy(88) && lastTapY<uy(174);
      const bool onRight=lastTapX>=ux(182) && lastTapY>=uy(88) && lastTapY<uy(174);
      if(onStart) {
        const bool ok=gpxLibrary.select(gpxLibrary.routes[selectedRouteRow].id);
        if(ok) {
          routeNavigationEnabled=true;routeSelectionForStart=false;startCountdown(now);
        }
      } else if(onLeft || onRight) {
        selectedRouteRow=(selectedRouteRow+(onLeft ? count-1 : 1))%count;
      } else {
        appState=routeSelectionForStart ? START_ROUTE_MENU : OPTIONS_MENU;
        previousDrawMs=0;
      }
      previousDrawMs=0;return;
    }
    if(gesture!=GESTURE_NONE)previousDrawMs=0;
    if(!previousDrawMs || now-previousDrawMs>=250) {previousDrawMs=now;drawRouteLibrary(now);}
    delay(5);return;
  }
  // Playback has its own clock and never enters the ride recorder or odometer path.
  if (appState == GPX_DEMO) {
    gpxSimulation.tick(now);
    if (gesture == GESTURE_UP) { appState = READY; previousDrawMs = 0; return; }
    if (gesture == GESTURE_TAP) {
      if (gpxSimulation.finished()) gpxSimulation.reset(now);
      else gpxSimulation.paused = !gpxSimulation.paused;
      previousDrawMs = 0;
    }
    lastActivityMs = now;
    if (!previousDrawMs || now-previousDrawMs >= 83) {
      previousDrawMs = now;
      drawGpxRoute(gpxSimulation.meters, true, true, 0);
    }
    static uint32_t reportMs = 0;
    if (now-reportMs >= 10000 && Serial && Serial.availableForWrite()>=160) {
      reportMs = now;
      const auto position = gpx::sample(gpxSimulation.meters);
      Serial.printf("GPX SIM %.1fm / %.1fm speed=%.1fmph east=%.1f north=%.1f paused=%d\n",
          gpxSimulation.meters, gpx::length(),
          gpxSimulation.paused || gpxSimulation.finished() ? 0.0 : 40.0,
          position.east, position.north, gpxSimulation.paused);
    }
    delay(5);
    return;
  }
  const bool navigationActive = !(routeNavigationEnabled && (appState==RIDING || appState==RIDE_MENU)) &&
      appState != ANCS_TEST &&
      appState != SAVE_RIDE_PROMPT && appState != CONFIRM_PAGE &&
      appState != RIDE_CANCELED && appState != OPTIONS_MENU &&
      navigationIsVisible(now);

  if (appState == CONFIRM_PAGE && pendingAction == ACTION_RESET_ODOMETER &&
      odometerResetArmed && gesture == GESTURE_ODOMETER_RESET) {
    resetOdometer();
    odometerResetArmed = false;
    odometerResetSliderActive = false;
    pendingAction = ACTION_NONE;
    appState = STATUS_PAGE;
    statusPage = 1;
    ignoreTouchUntilMs = now + MENU_TOUCH_LOCKOUT_MS;
    previousDrawMs = 0;
  // Summary swipe-up is always an exit gesture, even if an ANCS navigation
  // card happens to be visible over the summary at that moment.
  } else if (appState == SUMMARY && gesture == GESTURE_UP) {
    ignoreTouchUntilMs = now + MENU_TOUCH_LOCKOUT_MS;
    if (!viewingSavedRide && rideStorageReady &&
        strcmp(summaryGpsPath, ACTIVE_GPS_PATH) == 0)
      FFat.remove(ACTIVE_GPS_PATH);
    openHome();
    if (!viewingSavedRide) notifyPhone("ready");
  } else if (appState == OPTIONS_MENU && gesture == GESTURE_POWER_OFF) {
    enterLowPowerSleep(true);
    return;
  } else if (appState==RIDING && gpxPageAlert.active && gesture==GESTURE_TAP) {
    gpxPageAlert.dismiss(currentPage);previousDrawMs=0;
  } else if (appState==RIDING && gpxPageAlert.active &&
             (gesture==GESTURE_LEFT || gesture==GESTURE_RIGHT)) {
    gpxPageAlert.active=false;moveSelection(gesture==GESTURE_LEFT ? 1 : -1,now);
  } else if (appState==RIDING && gpxTurnPreview.active && gesture==GESTURE_TAP) {
    gpxTurnPreview.dismiss(currentPage);
    previousDrawMs=0;
  } else if (appState==RIDING && gpxTurnPreview.active &&
             (gesture==GESTURE_LEFT || gesture==GESTURE_RIGHT)) {
    gpxTurnPreview.active=false;
    moveSelection(gesture==GESTURE_LEFT ? 1 : -1,now);
  } else if (appState == RIDING &&
             (gesture == GESTURE_LEFT || gesture == GESTURE_RIGHT)) {
    // Horizontal swipes and the existing outer-third taps share the same
    // direction gestures. Handle them before ANCS overlays so page switching
    // stays immediate in every ride view.
    moveSelection(gesture == GESTURE_LEFT ? 1 : -1, now);
  } else if (navigationActive &&
             (gesture == GESTURE_TAP || gesture == GESTURE_UP)) {
    portENTER_CRITICAL(&navMux);
    navVisible = false;
    portEXIT_CRITICAL(&navMux);
    if (gesture == GESTURE_UP && appState != RIDING && appState != RIDE_MENU)
      openHome();
    else previousDrawMs = 0;
  } else if (!navigationActive && gesture == GESTURE_UP) {
    if (appState == SAVE_RIDE_PROMPT) {
      // Back must never lose a completed ride; use the safe action.
      completeRideEnd(true);
    } else if (appState == ANCS_TEST) {
      notifications.setAcceptAll(false);
      openOptionsMenu();
    } else if (appState == RIDE_CANCELED) {
      openHome();
    } else if (appState == CONFIRM_PAGE) {
      appState = confirmReturnState;
      pendingAction = ACTION_NONE;
      odometerResetArmed = false;
      odometerResetSliderActive = false;
      previousDrawMs = 0;
    } else {
    const bool rideContext = appState == RIDING || appState == RIDE_MENU ||
        (appState == DISPLAY_PAGE && displayReturnState == RIDE_MENU) ||
        (appState == STATUS_PAGE && statusReturnState == RIDE_MENU);
    if (appState == READY) {previousDrawMs=0;}
    else if (appState == MENU) openHome();
    else if (appState == RIDE_MENU) {appState=RIDING;previousDrawMs=0;}
    else if (rideContext) openRideMenu();
    else if (appState == OPTIONS_MENU) openHome();
    else if (appState == RIDES_LIST) {ridesEditMode=false;openOptionsMenu();}
    else if (appState == STATUS_PAGE) {
      if(statusReturnState==RIDE_MENU)openRideMenu();
      else if(statusReturnState==OPTIONS_MENU)openOptionsMenu();
      else openMenu();
    } else if (appState == DISPLAY_PAGE) {
      if(displayReturnState==RIDE_MENU)openRideMenu();
      else openOptionsMenu();
    } else if (appState == SUMMARY) {
      if (!viewingSavedRide && rideStorageReady &&
          strcmp(summaryGpsPath, ACTIVE_GPS_PATH) == 0) FFat.remove(ACTIVE_GPS_PATH);
      appState=viewingSavedRide ? RIDES_LIST : READY;
      if(viewingSavedRide)ridesEditMode=false;
      previousDrawMs=0;
      if(!viewingSavedRide)notifyPhone("ready");
    } else if (appState == COUNTDOWN) {
      routeNavigationEnabled=false;
      routeSelectionForStart=false;
      appState=RIDE_CANCELED;
      previousDrawMs=0;
    } else openMenu();
    }
  } else if (!navigationActive && gesture == GESTURE_LEFT) moveSelection(+1, now);
  else if (!navigationActive && gesture == GESTURE_RIGHT) moveSelection(-1, now);
  else if (!navigationActive && gesture == GESTURE_TAP) activate(now);

  if (appState == RIDE_MENU ||
      (appState == DISPLAY_PAGE && displayReturnState == RIDE_MENU) ||
      (appState == STATUS_PAGE && statusReturnState == RIDE_MENU) || appState == RIDING)
    updateRideData(now);
  if (appState == COUNTDOWN) {
    if (touchActive && nonNegativeElapsedMs(now,touchStartMs)>=2000) {
      touchCancelConsumed=false;
      touchBlockedUntilRelease=true;
      routeNavigationEnabled=false;
      routeSelectionForStart=false;
      appState=RIDE_CANCELED;
      previousDrawMs=0;
    } else if (!touchActive && now-countdownStartMs>=3000) {
      // A hold begun near the end gets its full two seconds before starting.
      startRide(now);
    }
  }

  const bool activeRide = appState == RIDING || appState == RIDE_MENU ||
      (appState == DISPLAY_PAGE && displayReturnState == RIDE_MENU) ||
      (appState == STATUS_PAGE && statusReturnState == RIDE_MENU);
  if (activeRide) {
    updateAutoBrightness(now);
    if(routeNavigationEnabled)updateGpxPosition(now);
    else if(appState==RIDING &&
            (currentPage==gpx::PAGE_INDEX || currentPage==RIDAR_PAGE_INDEX)) {
      float unusedCourse=NAN;
      updateGpxLiveFix(now,unusedCourse);
    }
    if(appState==RIDING && routeNavigationEnabled && gpxFixAvailable && !ridePaused) {
      const int previousPage=currentPage;
      const bool off=gpxGuidance.mode()==gpx::OFF_COURSE && !gpxArrived;
      if(gpxPageAlert.pending(off,gpxArrived) && gpxTurnPreview.active)gpxTurnPreview.dismiss(currentPage);
      gpxPageAlert.update(off,gpxArrived,currentPage);
      const auto cue=gpx::nextCue(max(gpxLastProgress,gpxTurnPreview.handledThrough));
      if(!gpxPageAlert.active && !gpxArrived)
        gpxTurnPreview.update(gpxLastProgress,cue,gpxGuidance.mode()==gpx::ON_ROUTE,currentPage);
      if(currentPage!=previousPage)previousDrawMs=0;
    }
  }

  // Build no more than one hidden map frame per second. The renderer checks
  // the touch interrupt throughout and abandons preparation immediately when
  // the rider touches the display.
  if (!navigationActive) prepareMapFrame(now);

  if (lastActivityMs && now - lastActivityMs >= AUTO_POWER_OFF_MS) {
    if (activeRide) enterInactiveSleep();
    else enterLowPowerSleep();
    return;
  }

  // Static screens redraw once per second for the clock. Touch/state changes
  // set previousDrawMs to zero and therefore still redraw immediately.
  uint32_t interval = 1000;
  // Ten frames per second keeps the rolling logo gradient visibly animated
  // while halving the expensive full-frame AMOLED transfers.
  if (appState == READY) interval = 100;
  else if (appState == COUNTDOWN) interval = 33;
  else if (navigationActive) interval = 1000;
  else if (appState == RIDING) interval = rideFrameIntervalMs();
  const bool deferLiveMapForTouch = appState == RIDING &&
      (currentPage == gpx::PAGE_INDEX ||
       currentPage == RIDAR_PAGE_INDEX) &&
      touchActive;
  const bool deferReadyAnimationForTouch=appState==READY && touchHasPriority();
  if (!deferLiveMapForTouch && !deferReadyAnimationForTouch &&
      (!previousDrawMs || now - previousDrawMs >= interval)) {
    previousDrawMs = now;
    const bool drawingLiveMap = appState == RIDING &&
        (currentPage == gpx::PAGE_INDEX ||
         currentPage == RIDAR_PAGE_INDEX);
    if (drawingLiveMap) boostCpuForMapRender();
    if (navigationActive) drawNavigationPage(now);
    else if (appState == READY) drawReadyScreen(now);
    else if (appState == MENU || appState == OPTIONS_MENU || appState == RIDE_MENU) drawMenu(now);
    else if (appState == RIDES_LIST) drawRidesList(now);
    else if (appState == ANCS_TEST) drawAncsTest(now);
    else if (appState == STATUS_PAGE) drawStatus(now);
    else if (appState == DISPLAY_PAGE) drawDisplayPage(now);
    else if (appState == COUNTDOWN) drawCountdown(now);
    else if (appState == RIDE_CANCELED) drawRideCanceled(now);
    else if (appState == RIDING) drawRidePage(now);
    else if (appState == CONFIRM_PAGE) drawConfirmation(now);
    else if (appState == SAVE_RIDE_PROMPT) drawSaveRidePrompt(now);
    else if (appState == BLUETOOTH_PAGE) drawBluetoothPage(now);
    else if (appState == START_ROUTE_MENU) drawStartRouteMenu();
    else if (appState == ROUTES_LIST) drawRouteLibrary(now);
    else if (appState == SUMMARY) drawSummary(now);
  }

  delay(5);
}
