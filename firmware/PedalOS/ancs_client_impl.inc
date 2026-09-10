#include "ancs_client.h"

#include <BLEAdvertising.h>
#include <BLESecurity.h>
#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>

namespace {
constexpr uint32_t kAttributeTimeoutMs = 2500;
constexpr uint32_t kMaximumEventAgeMs = 20000;
constexpr uint8_t kMaxRetries = 2;
constexpr uint8_t kFlagPreExisting = 1u << 2;
const ble_uuid128_t kAncsService = BLE_UUID128_INIT(
    0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
    0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);
const ble_uuid128_t kNotificationSource = BLE_UUID128_INIT(
    0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c,
    0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
const ble_uuid128_t kControlPoint = BLE_UUID128_INIT(
    0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98,
    0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
const ble_uuid128_t kDataSource = BLE_UUID128_INIT(
    0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe,
    0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);
constexpr char kBikeServiceUuid[] = "8E400001-F315-4F60-9FB8-838830DAEA50";
constexpr char kLocationUuid[] = "8E400002-F315-4F60-9FB8-838830DAEA50";
constexpr char kStatusUuid[] = "8E400003-F315-4F60-9FB8-838830DAEA50";
constexpr char kDeviceNameUuid[] = "8E400006-F315-4F60-9FB8-838830DAEA50";

uint32_t read32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

class AncsServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *, ble_gap_conn_desc *desc) override {
    if (AncsClient::instance_) AncsClient::instance_->onConnected(desc->conn_handle);
  }
  void onDisconnect(BLEServer *, ble_gap_conn_desc *) override {
    if (AncsClient::instance_) AncsClient::instance_->onDisconnected();
  }
};

class AncsSecurityCallbacks : public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t) override { return true; }
  bool onAuthorizationRequest(uint16_t, uint16_t, bool) override { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    if (AncsClient::instance_)
      AncsClient::instance_->onAuthenticated(desc && desc->sec_state.encrypted);
  }
};
}  // namespace

AncsClient *AncsClient::instance_ = nullptr;

void AncsClient::begin(const char *deviceName) {
  running_=true;
  instance_ = this;
  deviceName_ = deviceName;
  BLEDevice::init(deviceName);
  BLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_DEFAULT);
  BLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_ADV);
  BLEDevice::setPower(ESP_PWR_LVL_P3, ESP_BLE_PWR_TYPE_SCAN);
  BLEDevice::setMTU(247);
  BLEDevice::setSecurityCallbacks(new AncsSecurityCallbacks());
  auto *security = new BLESecurity();
  security->setCapability(ESP_IO_CAP_NONE);
  security->setAuthenticationMode(true, false, true);
  server_ = BLEDevice::createServer();
  server_->setCallbacks(new AncsServerCallbacks());
  BLEService *bridgeService = server_->createService(kBikeServiceUuid);
  // Bike Relay prefers acknowledged GPS writes for reliability while ANCS is
  // active, with write-without-response retained for older app builds.
  bridgeService->createCharacteristic(
      kLocationUuid,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  BLECharacteristic *status = bridgeService->createCharacteristic(
      kStatusUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  status->setValue("ancs-priority");
  // The main application installs persistence/validation callbacks after
  // begin() returns. It must be created here because the service is started
  // before control returns to the application.
  bridgeService->createCharacteristic(
      kDeviceNameUuid,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);
  bridgeService->createCharacteristic(
      "8E400007-F315-4F60-9FB8-838830DAEA50",
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  bridgeService->start();
  ble_gap_event_listener_register(new ble_gap_event_listener(), gapEvent, this);
}

void AncsClient::endForLightSleep() {
  running_=false; // Disconnect callbacks must not restart advertising.
  // Stop NimBLE without releasing controller memory so begin() can recreate
  // the GATT server after light sleep. Clear per-connection state because all
  // handles and service pointers become invalid during deinitialization.
  BLEDevice::deinit(false);
  server_ = nullptr;
  connHandle_ = 0xffff;
  negotiatedMtu_ = 23;
  connectionIntervalUnits_ = 0;
  serviceStart_ = serviceEnd_ = 0;
  notificationSource_ = controlPoint_ = dataSource_ = 0;
  notificationCccd_ = dataCccd_ = 0;
  shouldDiscover_ = false;
  responseChanged_ = false;
  requestInFlight_ = false;
  currentRemoved_ = false;
  ready_ = false;
  fetchStage_ = FetchAppId;
  responseLength_ = 0;
  queueCount_ = 0;
  current_ = AncsNotification();
  currentEvent_ = PendingEvent();
  if (stateCallback_) stateCallback_(Disconnected);
}

void AncsClient::setEnabled(bool enabled) {
  if (enabled == running_) {
    if (enabled && connHandle_ == 0xffff) startAdvertising();
    return;
  }

  running_ = enabled;
  if (enabled) {
    startAdvertising();
    return;
  }

  // Keep the controller and GATT database alive while the device is awake.
  // Reinitializing NimBLE from the touch handler can race its host task and
  // reset the ESP32. A disabled client simply stops advertising and drops the
  // active phone link; onDisconnected() will not restart advertising because
  // running_ is already false.
  BLEDevice::getAdvertising()->stop();
  shouldDiscover_ = false;
  ready_ = false;
  requestInFlight_ = false;
  queueCount_ = 0;
  if (connHandle_ != 0xffff && server_) {
    server_->disconnect(connHandle_);
  } else if (stateCallback_) {
    stateCallback_(Disconnected);
  }
}

void AncsClient::startAdvertising() {
  if(!running_)return;
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->stop();
  BLEAdvertisementData data;
  data.setFlags(0x01);
  uint8_t field[18] = {17, 0x15};
  memcpy(field + 2, kAncsService.value, 16);
  data.addData(String(reinterpret_cast<char *>(field), sizeof(field)));
  advertising->setAdvertisementData(data);
  BLEAdvertisementData scanResponse;
  scanResponse.setCompleteServices(BLEUUID(kBikeServiceUuid));
  // A legacy scan response has 31 bytes. The 128-bit service consumes 18,
  // leaving 11 bytes for a local name plus its AD header. The complete name
  // remains available through GAP and the dedicated GATT characteristic.
  String advertisedName = deviceName_;
  if (advertisedName.length() > 11) {
    size_t safeLength = 0;
    while (safeLength < advertisedName.length() && safeLength < 11) {
      const uint8_t first = uint8_t(advertisedName[safeLength]);
      const size_t width = first < 0x80 ? 1 :
          ((first & 0xe0) == 0xc0 ? 2 : ((first & 0xf0) == 0xe0 ? 3 : 4));
      if (safeLength + width > 11) break;
      safeLength += width;
    }
    advertisedName = advertisedName.substring(0, safeLength);
  }
  scanResponse.setName(advertisedName);
  advertising->setScanResponseData(scanResponse);
  advertising->setScanResponse(true);
  advertising->start();
  Serial.printf("BLE advertising as %s\n", deviceName_.c_str());
}

void AncsClient::setDeviceName(const String &deviceName) {
  deviceName_ = deviceName;
  // BLEDevice has no public post-init GAP-name setter in Arduino-ESP32 3.x.
  // NimBLE's GAP service owns the authoritative name once the server exists.
  ble_svc_gap_device_name_set(deviceName_.c_str());
}

void AncsClient::setOtaMode(bool enabled) {
  if (otaMode_ == enabled) return;
  otaMode_ = enabled;
  if (connHandle_ == 0xffff) return;
  ble_gap_upd_params params = {};
  params.itvl_min = params.itvl_max = enabled ? 12 : 24;  // 15 or 30 ms.
  params.latency = 0;
  params.supervision_timeout = 600;
  const int rc = ble_gap_update_params(connHandle_, &params);
  Serial.printf("BLE %s interval request %.0fms rc=%d\n",
                enabled ? "OTA" : "normal", enabled ? 15.0f : 30.0f, rc);
}

void AncsClient::onConnected(uint16_t handle) {
  connHandle_ = handle;
  ready_ = false;
  if (stateCallback_) stateCallback_(Connected);
  const int rc = ble_gap_security_initiate(handle);
  Serial.printf("ANCS link connected handle=%u security=%d\n", handle, rc);
}

void AncsClient::onDisconnected() {
  connHandle_ = 0xffff;
  negotiatedMtu_ = 23;
  connectionIntervalUnits_ = 0;
  serviceStart_ = serviceEnd_ = 0;
  notificationSource_ = controlPoint_ = dataSource_ = 0;
  notificationCccd_ = dataCccd_ = 0;
  ready_ = false;
  shouldDiscover_ = false;
  requestInFlight_ = false;
  responseChanged_ = false;
  currentRemoved_ = false;
  responseLength_ = 0;
  current_ = {};
  portENTER_CRITICAL(&stateMux_);
  queueCount_ = 0;
  portEXIT_CRITICAL(&stateMux_);
  if (stateCallback_) stateCallback_(Disconnected);
  Serial.println("ANCS disconnected; pipeline cleared");
  startAdvertising();
}

void AncsClient::onAuthenticated(bool success) {
  Serial.printf("ANCS authentication %s\n", success ? "complete" : "failed");
  if (!success) return;
  const int rc = ble_gattc_exchange_mtu(connHandle_, mtuComplete, this);
  Serial.printf("ANCS MTU exchange requested local=247 rc=%d\n", rc);
  if (rc) shouldDiscover_ = true;
}

int AncsClient::mtuComplete(uint16_t conn, const ble_gatt_error *error,
                            uint16_t mtu, void *arg) {
  auto *self = static_cast<AncsClient *>(arg);
  self->negotiatedMtu_ = error->status ? ble_att_mtu(conn) : mtu;
  self->shouldDiscover_ = true;
  return 0;
}

void AncsClient::loop() {
  if(!running_)return;
  const uint32_t now = millis();
  if (shouldDiscover_ && connHandle_ != 0xffff) {
    shouldDiscover_ = false;
    Serial.printf("ANCS negotiated MTU=%u payload=%u\n", negotiatedMtu_,
                  negotiatedMtu_ >= 3 ? negotiatedMtu_ - 3 : 0);
    ble_gap_upd_params params = {};
    params.itvl_min = params.itvl_max = otaMode_ ? 12 : 24;
    params.latency = 0;
    params.supervision_timeout = 600;
    const int updateRc = ble_gap_update_params(connHandle_, &params);
    Serial.printf("BLE initial %.0fms connection request rc=%d\n",
                  otaMode_ ? 15.0f : 30.0f, updateRc);
    discover();
  }

  if (responseChanged_ && requestInFlight_) {
    uint8_t snapshot[sizeof(response_)];
    size_t length;
    portENTER_CRITICAL(&stateMux_);
    length = responseLength_;
    memcpy(snapshot, response_, length);
    responseChanged_ = false;
    portEXIT_CRITICAL(&stateMux_);
    parseAttributeResponse(snapshot, length);
  }

  // A response can span several BLE packets.  Time out only when no packet
  // has arrived recently, rather than cancelling a healthy fragmented reply
  // based on the age of the original request.
  if (requestInFlight_ && now - lastDataMs_ >= kAttributeTimeoutMs) {
    ++timeoutCount_;
    Serial.printf("ANCS request timeout uid=%lu stage=%u bytes=%u idle=%lu\n",
                  static_cast<unsigned long>(current_.uid), unsigned(fetchStage_),
                  unsigned(responseLength_),
                  static_cast<unsigned long>(now - lastDataMs_));
    finishRequest(false);
  }

  if (ready_ && !requestInFlight_ &&
      int32_t(now - nextRequestMs_) >= 0) {
    startRequest();
  }
  printHealth(now);
}

void AncsClient::enqueueEvent(const PendingEvent &event) {
  const auto priorityOf = [](const PendingEvent &item) {
    uint8_t priority = item.category == 10 ? 4 : 0;
    if (!(item.eventFlags & kFlagPreExisting)) priority += 2;
    return priority;
  };
  portENTER_CRITICAL(&stateMux_);
  // Coalesce only modifications to the same NotificationUID. Real navigation
  // can briefly publish several Location UIDs; discarding them all here loses
  // the useful Google Maps turn before its app identifier is known.
  for (uint8_t i = 0; i < queueCount_; ++i) {
    if (queue_[i].uid == event.uid) {
      queue_[i] = event;
      portEXIT_CRITICAL(&stateMux_);
      return;
    }
  }
  if (queueCount_ < QUEUE_SIZE) {
    queue_[queueCount_++] = event;
    if (queueCount_ > maxQueueDepth_) maxQueueDepth_ = queueCount_;
  } else {
    uint8_t worst = 0;
    for (uint8_t i = 1; i < queueCount_; ++i) {
      if (priorityOf(queue_[i]) < priorityOf(queue_[worst])) worst = i;
    }
    if (priorityOf(event) > priorityOf(queue_[worst])) queue_[worst] = event;
    ++droppedEventCount_;
  }
  portEXIT_CRITICAL(&stateMux_);
}

bool AncsClient::dequeueEvent(PendingEvent &event) {
  const auto priorityOf = [](const PendingEvent &item) {
    uint8_t priority = item.category == 10 ? 4 : 0;
    if (!(item.eventFlags & kFlagPreExisting)) priority += 2;
    return priority;
  };
  portENTER_CRITICAL(&stateMux_);
  if (!queueCount_) {
    portEXIT_CRITICAL(&stateMux_);
    return false;
  }
  uint8_t selected = 0;
  for (uint8_t i = 1; i < queueCount_; ++i) {
    const uint8_t candidatePriority = priorityOf(queue_[i]);
    const uint8_t selectedPriority = priorityOf(queue_[selected]);
    if (candidatePriority > selectedPriority ||
        (candidatePriority == selectedPriority &&
         int32_t(queue_[i].receivedMs - queue_[selected].receivedMs) > 0))
      selected = i;
  }
  event = queue_[selected];
  for (uint8_t i = selected + 1; i < queueCount_; ++i) queue_[i - 1] = queue_[i];
  --queueCount_;
  portEXIT_CRITICAL(&stateMux_);
  return true;
}

void AncsClient::removeQueuedEvent(uint32_t uid) {
  portENTER_CRITICAL(&stateMux_);
  for (uint8_t i = 0; i < queueCount_;) {
    if (queue_[i].uid != uid) {
      ++i;
      continue;
    }
    for (uint8_t j = i + 1; j < queueCount_; ++j) queue_[j - 1] = queue_[j];
    --queueCount_;
  }
  // Navigation apps commonly remove the current ANCS notification while its
  // attributes are still arriving. Let the in-flight request finish; the UI
  // owns its 20-second/tap/replacement lifetime.
  (void)uid;
  portEXIT_CRITICAL(&stateMux_);
}

void AncsClient::discardOlderLocationEvents(uint32_t receivedMs) {
  portENTER_CRITICAL(&stateMux_);
  for (uint8_t i = 0; i < queueCount_;) {
    if (queue_[i].category != 10 ||
        int32_t(queue_[i].receivedMs - receivedMs) > 0) {
      ++i;
      continue;
    }
    for (uint8_t j = i + 1; j < queueCount_; ++j) queue_[j - 1] = queue_[j];
    --queueCount_;
  }
  portEXIT_CRITICAL(&stateMux_);
}

bool AncsClient::startRequest() {
  if (fetchStage_ == FetchAppId || !current_.uid) {
    PendingEvent next;
    bool foundFresh = false;
    while (dequeueEvent(next)) {
      if (millis() - next.receivedMs < kMaximumEventAgeMs) {
        foundFresh = true;
        break;
      }
      ++filteredEventCount_;
    }
    if (!foundFresh) return false;
    currentEvent_ = next;
    current_ = {};
    current_.uid = next.uid;
    current_.eventFlags = next.eventFlags;
    current_.category = next.category;
    current_.categoryCount = next.categoryCount;
    // Location notifications are short-lived and frequently modified or
    // replaced by map apps. Fetch their full payload in one round trip, then
    // validate the returned app identifier before delivering it. Other
    // categories retain the inexpensive app-ID-first filtering path.
    fetchStage_ = (acceptAll_ || next.category == 10)
        ? FetchDetails : FetchAppId;
    currentRemoved_ = false;
  }
  responseLength_ = 0;
  responseChanged_ = false;
  requestStartedMs_ = millis();
  lastDataMs_ = requestStartedMs_;
  requestInFlight_ = true;
  if (!requestAttributes(current_.uid, fetchStage_)) {
    requestInFlight_ = false;
    nextRequestMs_ = millis() + 100;
    return false;
  }
  return true;
}

bool AncsClient::requestAttributes(uint32_t uid, FetchStage stage) {
  uint8_t cmd[16] = {0, uint8_t(uid), uint8_t(uid >> 8), uint8_t(uid >> 16),
                     uint8_t(uid >> 24)};
  size_t length = 5;
  if (stage == FetchAppId) {
    cmd[length++] = 0;
  } else {
    cmd[length++] = 0;
    cmd[length++] = 1; cmd[length++] = 128; cmd[length++] = 0;
    cmd[length++] = 2; cmd[length++] = 96;  cmd[length++] = 0;
    cmd[length++] = 3; cmd[length++] = 0;   cmd[length++] = 1;
  }
  const int rc = ble_gattc_write_flat(connHandle_, controlPoint_, cmd, length,
                                       writeComplete, this);
  if (rc) Serial.printf("ANCS control-point start failed=%d\n", rc);
  return rc == 0;
}

void AncsClient::finishRequest(bool success) {
  requestInFlight_ = false;
  responseChanged_ = false;
  portENTER_CRITICAL(&stateMux_);
  responseLength_ = 0;
  portEXIT_CRITICAL(&stateMux_);

  if (!success) {
    if (!currentRemoved_ && currentEvent_.retries < kMaxRetries) {
      PendingEvent retry = currentEvent_;
      ++retry.retries;
      ++retryCount_;
      enqueueEvent(retry);
    }
    current_ = {};
    fetchStage_ = FetchAppId;
    nextRequestMs_ = millis() + 100;
  }
}

void AncsClient::discover() {
  serviceStart_ = serviceEnd_ = 0;
  const int rc = ble_gattc_disc_all_svcs(connHandle_, serviceDiscovered, this);
  Serial.printf("ANCS service discovery rc=%d\n", rc);
}

int AncsClient::serviceDiscovered(uint16_t conn, const ble_gatt_error *error,
                                  const ble_gatt_svc *service, void *arg) {
  auto *self = static_cast<AncsClient *>(arg);
  if (error->status == 0 && service) {
    if (ble_uuid_cmp(&service->uuid.u, &kAncsService.u) == 0) {
      self->serviceStart_ = service->start_handle;
      self->serviceEnd_ = service->end_handle;
    }
    return 0;
  }
  if (error->status == BLE_HS_EDONE && self->serviceStart_) {
    ble_gattc_disc_all_chrs(conn, self->serviceStart_, self->serviceEnd_,
                            characteristicDiscovered, self);
  } else if (error->status == BLE_HS_EDONE) {
    Serial.println("ANCS service not published by iPhone");
  } else if (error->status != BLE_HS_EDONE) {
    Serial.printf("ANCS service discovery failed=%d\n", error->status);
  }
  return 0;
}

int AncsClient::characteristicDiscovered(uint16_t, const ble_gatt_error *error,
                                         const ble_gatt_chr *chr, void *arg) {
  auto *self = static_cast<AncsClient *>(arg);
  if (error->status == 0 && chr) {
    if (ble_uuid_cmp(&chr->uuid.u, &kNotificationSource.u) == 0)
      self->notificationSource_ = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &kControlPoint.u) == 0)
      self->controlPoint_ = chr->val_handle;
    else if (ble_uuid_cmp(&chr->uuid.u, &kDataSource.u) == 0)
      self->dataSource_ = chr->val_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE && self->notificationSource_ &&
      self->dataSource_ && self->controlPoint_) {
    // On iOS ANCS both notification characteristics have one CCCD immediately
    // after the value. Descriptor discovery on this reverse-role link can hang.
    self->notificationCccd_ = self->notificationSource_ + 1;
    self->dataCccd_ = self->dataSource_ + 1;
    self->subscribe();
  } else if (error->status == BLE_HS_EDONE) {
    Serial.printf("ANCS characteristics incomplete ns=%u cp=%u ds=%u\n",
                  self->notificationSource_, self->controlPoint_, self->dataSource_);
  }
  return 0;
}

void AncsClient::subscribe() {
  const uint8_t enabled[2] = {1, 0};
  const int rc = ble_gattc_write_flat(connHandle_, dataCccd_, enabled,
                                      sizeof(enabled), writeComplete, this);
  Serial.printf("ANCS subscribe data handle=%u rc=%d\n", dataCccd_, rc);
}

int AncsClient::writeComplete(uint16_t, const ble_gatt_error *error,
                              ble_gatt_attr *attr, void *arg) {
  auto *self = static_cast<AncsClient *>(arg);
  if (error->status) {
    ++self->parseErrorCount_;
    if (attr && (attr->handle == self->dataCccd_ ||
                 attr->handle == self->notificationCccd_)) {
      self->shouldDiscover_ = true;
    }
    return 0;
  }
  if (attr && attr->handle == self->dataCccd_) {
    const uint8_t enabled[2] = {1, 0};
    ble_gattc_write_flat(self->connHandle_, self->notificationCccd_, enabled,
                         sizeof(enabled), writeComplete, self);
  } else if (attr && attr->handle == self->notificationCccd_) {
    self->ready_ = true;
    if (self->stateCallback_) self->stateCallback_(Ready);
  }
  return 0;
}

int AncsClient::gapEvent(ble_gap_event *event, void *arg) {
  auto *self = static_cast<AncsClient *>(arg);
  if (event->type == BLE_GAP_EVENT_NOTIFY_RX &&
      event->notify_rx.conn_handle == self->connHandle_) {
    uint8_t bytes[768];
    size_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (length > sizeof(bytes)) length = sizeof(bytes);
    os_mbuf_copydata(event->notify_rx.om, 0, length, bytes);
    self->handleNotification(event->notify_rx.attr_handle, bytes, length);
  } else if (event->type == BLE_GAP_EVENT_MTU &&
             event->mtu.conn_handle == self->connHandle_) {
    self->negotiatedMtu_ = event->mtu.value;
  } else if (event->type == BLE_GAP_EVENT_CONN_UPDATE &&
             event->conn_update.conn_handle == self->connHandle_) {
    ble_gap_conn_desc desc;
    if (!event->conn_update.status && !ble_gap_conn_find(self->connHandle_, &desc)) {
      Serial.printf("ANCS link interval=%.2fms latency=%u timeout=%ums\n",
                    desc.conn_itvl * 1.25f, desc.conn_latency,
                    desc.supervision_timeout * 10);
      self->connectionIntervalUnits_ = desc.conn_itvl;
    }
  }
  return 0;
}

void AncsClient::handleNotification(uint16_t handle, const uint8_t *data,
                                    size_t length) {
  if (handle == notificationSource_) handleNotificationSource(data, length);
  else if (handle == dataSource_) handleDataSource(data, length);
}

void AncsClient::handleNotificationSource(const uint8_t *data, size_t length) {
  if (length < 8) {
    ++parseErrorCount_;
    return;
  }
  const uint32_t uid = read32(data + 4);
  ++rawEventCount_;
  if (data[0] == 2) {
    removeQueuedEvent(uid);
    if (removedCallback_) removedCallback_(uid);
  } else if (data[0] == 0 || data[0] == 1) {
    PendingEvent event;
    event.uid = uid;
    event.receivedMs = millis();
    event.eventFlags = data[1];
    event.category = data[2];
    event.categoryCount = data[3];
    // Retain pre-existing notifications until their app identifier is known.
    // Google Maps is not guaranteed to classify every navigation alert as
    // Location, especially when PedalOne reconnects during an active route.
    // Queue priority still puts new and Location events ahead of this backlog.
    enqueueEvent(event);
  }
}

void AncsClient::handleDataSource(const uint8_t *data, size_t length) {
  if (!requestInFlight_) {
    ++strayDataCount_;
    return;
  }
  portENTER_CRITICAL(&stateMux_);
  if (!responseLength_ &&
      (length < 5 || data[0] != 0 || read32(data + 1) != current_.uid)) {
    ++strayDataCount_;
    portEXIT_CRITICAL(&stateMux_);
    return;
  }
  if (responseLength_ + length > sizeof(response_)) {
    ++parseErrorCount_;
    portEXIT_CRITICAL(&stateMux_);
    return;
  }
  memcpy(response_ + responseLength_, data, length);
  responseLength_ += length;
  ++dataChunkCount_;
  lastDataMs_ = millis();
  responseChanged_ = true;
  portEXIT_CRITICAL(&stateMux_);
}

bool AncsClient::parseAttributeResponse(const uint8_t *bytes, size_t length) {
  if (length < 5 || bytes[0] != 0 || read32(bytes + 1) != current_.uid) return false;
  size_t pos = 5;
  uint8_t parsedMask = 0;
  String appId, title, subtitle, message;
  while (pos + 3 <= length) {
    const uint8_t id = bytes[pos];
    const uint16_t valueLength = uint16_t(bytes[pos + 1]) |
                                 (uint16_t(bytes[pos + 2]) << 8);
    if (pos + 3 + valueLength > length) return false;
    String value;
    value.reserve(valueLength);
    for (uint16_t i = 0; i < valueLength; ++i) value += char(bytes[pos + 3 + i]);
    if (id == 0) { appId = value; parsedMask |= 1u << 0; }
    else if (id == 1) { title = value; parsedMask |= 1u << 1; }
    else if (id == 2) { subtitle = value; parsedMask |= 1u << 2; }
    else if (id == 3) { message = value; parsedMask |= 1u << 3; }
    pos += 3 + valueLength;
  }

  const uint8_t expected = fetchStage_ == FetchAppId ? 0x01 : 0x0f;
  if ((parsedMask & expected) != expected || pos != length) return false;
  ++completedRequestCount_;

  if (fetchStage_ == FetchAppId) {
    current_.appId = appId;
    requestInFlight_ = false;
    responseLength_ = 0;
    responseChanged_ = false;
    // Only the explicit navigation-app allowlist proceeds to the larger
    // detail request. The same rule applies in ride and ANCS Testing modes.
    if (acceptAll_ || isNavigationApp(appId)) {
      ++googleEventCount_;
      fetchStage_ = FetchDetails;
      nextRequestMs_ = millis();
    } else {
      ++filteredEventCount_;
      current_ = {};
      fetchStage_ = FetchAppId;
      nextRequestMs_ = millis();
    }
    return true;
  }

  const bool directDetails = !current_.appId.length();
  current_.title = title;
  current_.subtitle = subtitle;
  current_.message = message;
  // FetchDetails includes the app identifier too. Preserve it so normal mode
  // can apply the same navigation-app filtering as the ANCS test screen.
  current_.appId = appId;
  const uint32_t latency = millis() - currentEvent_.receivedMs;
  lastGoogleLatencyMs_ = latency;
  Serial.printf("ANCS MAP uid=%lu app=%s latency=%lums chunksTotal=%lu title=%s message=%s\n",
                static_cast<unsigned long>(current_.uid), current_.appId.c_str(),
                static_cast<unsigned long>(latency),
                static_cast<unsigned long>(dataChunkCount_), title.c_str(),
                message.c_str());
  const bool accepted = acceptAll_ || isNavigationApp(appId);
  if (accepted) {
    if (directDetails) ++googleEventCount_;
    // Once a current, allowlisted Location event is complete, older waiting
    // Location events are stale. Preserve only updates that arrived while this
    // request was in flight.
    if (current_.category == 10)
      discardOlderLocationEvents(currentEvent_.receivedMs);
    if (!currentRemoved_ && notificationCallback_) notificationCallback_(current_);
  } else {
    ++filteredEventCount_;
  }
  finishRequest(true);
  current_ = {};
  fetchStage_ = FetchAppId;
  nextRequestMs_ = millis();
  return true;
}

bool AncsClient::isNavigationApp(const String &appId) {
  String lower = appId;
  lower.toLowerCase();
  return lower.indexOf("com.google.maps") >= 0 ||
         lower.indexOf("komoot") >= 0 ||
         lower.indexOf("ridewithgps") >= 0 ||
         lower.indexOf("com.byobike.pedalone") >= 0;
}

AncsClient::Diagnostics AncsClient::diagnostics() {
  Diagnostics d;
  portENTER_CRITICAL(&stateMux_);
  d.state = ready_ ? Ready : (connHandle_ == 0xffff ? Disconnected : Connected);
  d.requestActive = requestInFlight_;
  d.mtu = negotiatedMtu_;
  d.intervalUnits = connectionIntervalUnits_;
  d.queueDepth = queueCount_;
  d.maxQueueDepth = maxQueueDepth_;
  d.rawEvents = rawEventCount_;
  d.googleEvents = googleEventCount_;
  d.filteredEvents = filteredEventCount_;
  d.drops = droppedEventCount_;
  d.timeouts = timeoutCount_;
  d.retries = retryCount_;
  d.strayData = strayDataCount_;
  d.parseErrors = parseErrorCount_;
  d.lastGoogleLatencyMs = lastGoogleLatencyMs_;
  portEXIT_CRITICAL(&stateMux_);
  d.freeHeap = ESP.getFreeHeap();
  return d;
}

void AncsClient::printHealth(uint32_t now) {
  if (now - lastHealthMs_ < 5000) return;
  lastHealthMs_ = now;
  uint8_t depth;
  portENTER_CRITICAL(&stateMux_);
  depth = queueCount_;
  portEXIT_CRITICAL(&stateMux_);
  Serial.printf("ANCS health ready=%u mtu=%u queue=%u/%u max=%u raw=%lu google=%lu filtered=%lu "
                "done=%lu drops=%lu timeouts=%lu retries=%lu stray=%lu errors=%lu heap=%u\n",
                ready_, negotiatedMtu_, depth, QUEUE_SIZE, maxQueueDepth_,
                static_cast<unsigned long>(rawEventCount_),
                static_cast<unsigned long>(googleEventCount_),
                static_cast<unsigned long>(filteredEventCount_),
                static_cast<unsigned long>(completedRequestCount_),
                static_cast<unsigned long>(droppedEventCount_),
                static_cast<unsigned long>(timeoutCount_),
                static_cast<unsigned long>(retryCount_),
                static_cast<unsigned long>(strayDataCount_),
                static_cast<unsigned long>(parseErrorCount_), ESP.getFreeHeap());
}
