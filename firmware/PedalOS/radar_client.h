#pragma once

#include <Arduino.h>
#include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>

class RadarScanCallbacks;

class RadarClient {
 public:
  static constexpr size_t MAX_TARGETS = 4;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 60000;

  enum State : uint8_t {
    STATE_DISABLED,
    STATE_SEARCHING,
    STATE_FOUND,
    STATE_CONNECTING,
    STATE_CONNECTED,
    STATE_TIMED_OUT
  };

  struct Target {
    int8_t angleDegrees = 0;
    uint8_t distanceMeters = 0;
    int8_t closingSpeedKmh = 0;  // Positive approaching, negative moving away.
    uint8_t snr = 0;
    bool approaching = true;
  };

  struct Settings {
    uint8_t maximumRangeMeters = 75;
    uint8_t direction = 0;  // 0 approaching, 1 away, 2 both.
    uint8_t minimumSpeedKmh = 15;
    uint8_t noTargetDelaySeconds = 1;
    uint8_t triggerCount = 2;
    uint8_t snrThreshold = 32;
  };

  enum ConfigState : uint8_t {
    CONFIG_IDLE,
    CONFIG_APPLYING,
    CONFIG_APPLIED,
    CONFIG_FAILED
  };

  void begin(const char *deviceName);
  void setEnabled(bool enabled, uint32_t now);
  void service(uint32_t now);
  void stopForSleep();
  void setSettings(const Settings &settings) { settings_ = settings; }
  bool applySettings(const Settings &settings, uint32_t now);

  bool enabled() const { return enabled_; }
  State state() const { return state_; }
  ConfigState configState() const { return configState_; }
  const Settings &settings() const { return settings_; }
  uint32_t stateChangedMs() const { return stateChangedMs_; }
  const char *deviceName() const { return deviceName_.c_str(); }
  size_t snapshot(Target *output, size_t capacity, uint32_t now) const;
  uint32_t targetRevision() const;

 private:
  friend class RadarScanCallbacks;

  static RadarClient *instance_;
  static void scanComplete(BLEScanResults results);
  static int gapEvent(ble_gap_event *event, void *arg);
  static int serviceDiscovered(uint16_t connHandle,
                               const ble_gatt_error *error,
                               const ble_gatt_svc *service, void *arg);
  static int characteristicDiscovered(uint16_t connHandle,
                                      const ble_gatt_error *error,
                                      const ble_gatt_chr *characteristic,
                                      void *arg);
  static int notificationEnabled(uint16_t connHandle,
                                 const ble_gatt_error *error,
                                 ble_gatt_attr *attribute, void *arg);

  void onAdvertisement(BLEAdvertisedDevice &device);
  void onScanComplete();
  void onDisconnected();
  void failConnection(const char *reason, uint32_t now);
  void setState(State state, uint32_t now);
  void startScan(uint32_t now);
  bool startConnection(uint32_t now);
  void scheduleRetry(uint32_t now);
  void consume(const uint8_t *data, size_t length, uint32_t now);
  void parseFrame(const uint8_t *frame, size_t length, uint32_t now);
  void clearTargets(uint32_t now);
  bool sendCommand(uint16_t command, const uint8_t *payload,
                   size_t payloadLength);
  void serviceConfiguration(uint32_t now);

  String deviceName_;
  BLEScan *scan_ = nullptr;
  ble_addr_t targetAddress_ = {};
  uint16_t connHandle_ = BLE_HS_CONN_HANDLE_NONE;
  uint16_t serviceStart_ = 0;
  uint16_t serviceEnd_ = 0;
  uint16_t notifyHandle_ = 0;
  uint16_t writeHandle_ = 0;
  volatile State state_ = STATE_DISABLED;
  volatile uint32_t stateChangedMs_ = 0;
  bool enabled_ = false;
  bool scanning_ = false;
  bool connectPending_ = false;
  bool connecting_ = false;
  bool haveTargetAddress_ = false;
  uint32_t attemptStartedMs_ = 0;
  uint32_t retryAtMs_ = 0;
  Settings settings_;
  volatile ConfigState configState_ = CONFIG_IDLE;
  uint8_t configStep_ = 0;
  uint32_t nextConfigWriteMs_ = 0;
  uint8_t receiveBuffer_[160] = {};
  size_t receiveLength_ = 0;
  mutable portMUX_TYPE targetMux_ = portMUX_INITIALIZER_UNLOCKED;
  Target targets_[MAX_TARGETS] = {};
  size_t targetCount_ = 0;
  uint32_t targetRevision_ = 0;
};
