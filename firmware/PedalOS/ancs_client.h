#pragma once
#ifndef PEDALONE_ANCS_CLIENT_H
#define PEDALONE_ANCS_CLIENT_H

#include <Arduino.h>
#include <BLEDevice.h>

struct AncsNotification {
  uint32_t uid = 0;
  uint8_t eventFlags = 0;
  uint8_t category = 0;
  uint8_t categoryCount = 0;
  String appId;
  String title;
  String subtitle;
  String message;
};

class AncsClient {
 public:
  enum State { Disconnected, Connected, Ready };
  struct Diagnostics {
    State state = Disconnected;
    bool requestActive = false;
    uint16_t mtu = 23;
    uint16_t intervalUnits = 0;
    uint8_t queueDepth = 0;
    uint8_t maxQueueDepth = 0;
    uint32_t rawEvents = 0;
    uint32_t googleEvents = 0;
    uint32_t filteredEvents = 0;
    uint32_t drops = 0;
    uint32_t timeouts = 0;
    uint32_t retries = 0;
    uint32_t strayData = 0;
    uint32_t parseErrors = 0;
    uint32_t lastGoogleLatencyMs = 0;
    uint32_t freeHeap = 0;
  };
  using StateCallback = void (*)(State);
  using NotificationCallback = void (*)(const AncsNotification &);
  using RemovedCallback = void (*)(uint32_t uid);

  void begin(const char *deviceName);
  void endForLightSleep();
  void loop();
  void setStateCallback(StateCallback cb) { stateCallback_ = cb; }
  void setNotificationCallback(NotificationCallback cb) { notificationCallback_ = cb; }
  void setRemovedCallback(RemovedCallback cb) { removedCallback_ = cb; }
  void setAcceptAll(bool enabled) { acceptAll_ = enabled; }
  BLEServer *server() const { return server_; }
  Diagnostics diagnostics();
  void startAdvertising();
  // Updates the GAP name immediately and the local name used the next time
  // advertising starts. The GATT nickname characteristic is managed by the
  // application so the stored value remains the single source of truth.
  void setDeviceName(const String &deviceName);
  static AncsClient *instance_;
  void onConnected(uint16_t handle);
  void onDisconnected();
  void onAuthenticated(bool success);

 private:
  enum FetchStage : uint8_t { FetchAppId, FetchDetails };
  struct PendingEvent {
    uint32_t uid = 0;
    uint32_t receivedMs = 0;
    uint8_t eventFlags = 0;
    uint8_t category = 0;
    uint8_t categoryCount = 0;
    uint8_t retries = 0;
  };
  static constexpr uint8_t QUEUE_SIZE = 32;

  static int gapEvent(struct ble_gap_event *event, void *arg);
  static int mtuComplete(uint16_t conn, const struct ble_gatt_error *error,
                         uint16_t mtu, void *arg);
  static int serviceDiscovered(uint16_t conn, const struct ble_gatt_error *error,
                               const struct ble_gatt_svc *service, void *arg);
  static int characteristicDiscovered(uint16_t conn, const struct ble_gatt_error *error,
                                      const struct ble_gatt_chr *chr, void *arg);
  static int writeComplete(uint16_t conn, const struct ble_gatt_error *error,
                           struct ble_gatt_attr *attr, void *arg);

  void discover();
  void subscribe();
  void handleNotification(uint16_t attrHandle, const uint8_t *data, size_t length);
  void handleNotificationSource(const uint8_t *data, size_t length);
  void handleDataSource(const uint8_t *data, size_t length);
  bool startRequest();
  bool requestAttributes(uint32_t uid, FetchStage stage);
  bool parseAttributeResponse(const uint8_t *bytes, size_t length);
  void finishRequest(bool success);
  void enqueueEvent(const PendingEvent &event);
  bool dequeueEvent(PendingEvent &event);
  void removeQueuedEvent(uint32_t uid);
  void printHealth(uint32_t now);
  static bool isNavigationApp(const String &appId);

  BLEServer *server_ = nullptr;
  String deviceName_;
  StateCallback stateCallback_ = nullptr;
  NotificationCallback notificationCallback_ = nullptr;
  RemovedCallback removedCallback_ = nullptr;
  portMUX_TYPE stateMux_ = portMUX_INITIALIZER_UNLOCKED;
  volatile bool shouldDiscover_ = false;
  volatile bool responseChanged_ = false;
  bool requestInFlight_ = false;
  bool currentRemoved_ = false;
  bool ready_ = false;
  volatile bool acceptAll_ = false;
  FetchStage fetchStage_ = FetchAppId;
  uint32_t requestStartedMs_ = 0;
  uint32_t lastDataMs_ = 0;
  uint32_t nextRequestMs_ = 0;
  uint32_t lastHealthMs_ = 0;
  uint16_t connHandle_ = 0xffff;
  uint16_t negotiatedMtu_ = 23;
  uint16_t connectionIntervalUnits_ = 0;
  uint16_t serviceStart_ = 0;
  uint16_t serviceEnd_ = 0;
  uint16_t notificationSource_ = 0;
  uint16_t controlPoint_ = 0;
  uint16_t dataSource_ = 0;
  uint16_t notificationCccd_ = 0;
  uint16_t dataCccd_ = 0;
  uint8_t response_[768] = {};
  size_t responseLength_ = 0;
  AncsNotification current_;
  PendingEvent currentEvent_;
  PendingEvent queue_[QUEUE_SIZE] = {};
  uint8_t queueCount_ = 0;
  uint8_t maxQueueDepth_ = 0;
  uint32_t rawEventCount_ = 0;
  uint32_t droppedEventCount_ = 0;
  uint32_t completedRequestCount_ = 0;
  uint32_t timeoutCount_ = 0;
  uint32_t retryCount_ = 0;
  uint32_t parseErrorCount_ = 0;
  uint32_t strayDataCount_ = 0;
  uint32_t dataChunkCount_ = 0;
  uint32_t googleEventCount_ = 0;
  uint32_t filteredEventCount_ = 0;
  uint32_t lastGoogleLatencyMs_ = 0;
};

#endif  // PEDALONE_ANCS_CLIENT_H
