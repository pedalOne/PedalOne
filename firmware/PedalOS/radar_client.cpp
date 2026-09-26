#include "radar_client.h"

#include <host/ble_hs.h>
#include <os/os_mbuf.h>

namespace {
constexpr uint8_t REPORT_HEADER[] = {0xF4, 0xF3, 0xF2, 0xF1};
constexpr uint8_t REPORT_TAIL[] = {0xF8, 0xF7, 0xF6, 0xF5};
constexpr uint32_t RETRY_DELAY_MS = 750;
constexpr uint32_t CONNECT_FRAME_DELAY_MS = 150;

bool bytesEqual(const uint8_t *left, const uint8_t *right, size_t length) {
  return memcmp(left, right, length) == 0;
}
}

class RadarScanCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  void onResult(BLEAdvertisedDevice device) override {
    if (RadarClient::instance_) RadarClient::instance_->onAdvertisement(device);
  }
};

RadarClient *RadarClient::instance_ = nullptr;
static RadarScanCallbacks radarScanCallbacks;

void RadarClient::begin(const char *deviceName) {
  instance_ = this;
  deviceName_ = deviceName;
  scan_ = BLEDevice::getScan();
  connHandle_ = BLE_HS_CONN_HANDLE_NONE;
  scanning_ = false;
  connectPending_ = false;
  connecting_ = false;
  haveTargetAddress_ = false;
  configState_ = CONFIG_IDLE;
  receiveLength_ = 0;
  clearTargets(millis());
  if (scan_) {
    scan_->setActiveScan(true);
    scan_->setInterval(120);
    scan_->setWindow(90);
    scan_->setAdvertisedDeviceCallbacks(&radarScanCallbacks, true);
  }
}

void RadarClient::setState(State state, uint32_t now) {
  if (state_ == state) return;
  state_ = state;
  stateChangedMs_ = now;
}

void RadarClient::setEnabled(bool enabled, uint32_t now) {
  if (!enabled) {
    enabled_ = false;
    connectPending_ = false;
    if (scan_ && scanning_) scan_->stop();
    scanning_ = false;
    if (connecting_) ble_gap_conn_cancel();
    connecting_ = false;
    if (connHandle_ != BLE_HS_CONN_HANDLE_NONE)
      ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);
    connHandle_ = BLE_HS_CONN_HANDLE_NONE;
    haveTargetAddress_ = false;
    configState_ = CONFIG_IDLE;
    clearTargets(now);
    setState(STATE_DISABLED, now);
    return;
  }

  enabled_ = true;
  attemptStartedMs_ = now;
  retryAtMs_ = now;
  receiveLength_ = 0;
  clearTargets(now);
  startScan(now);
}

void RadarClient::startScan(uint32_t now) {
  if (!enabled_ || !scan_ || scanning_ ||
      connecting_ || connHandle_ != BLE_HS_CONN_HANDLE_NONE) return;
  haveTargetAddress_ = false;
  connectPending_ = false;
  scan_->clearResults();
  setState(STATE_SEARCHING, now);
  scanning_ = scan_->start(60, scanComplete, false);
  if (!scanning_) scheduleRetry(now);
}

void RadarClient::scanComplete(BLEScanResults) {
  if (instance_) instance_->onScanComplete();
}

void RadarClient::onScanComplete() {
  scanning_ = false;
  if (enabled_ && state_ == STATE_SEARCHING) retryAtMs_ = millis() + RETRY_DELAY_MS;
}

void RadarClient::onAdvertisement(BLEAdvertisedDevice &device) {
  if (!enabled_ || connectPending_ || !device.haveName() ||
      device.getName() != deviceName_) return;
  targetAddress_.type = device.getAddressType();
  memcpy(targetAddress_.val, device.getAddress().getNative(),
         sizeof(targetAddress_.val));
  haveTargetAddress_ = true;
  connectPending_ = true;
  if (scan_) scan_->stop();
  scanning_ = false;
  setState(STATE_FOUND, millis());
  Serial.printf("Radar found: %s at %s RSSI=%d\n", deviceName_.c_str(),
                device.getAddress().toString().c_str(), device.getRSSI());
}

bool RadarClient::startConnection(uint32_t now) {
  if (!haveTargetAddress_) return false;
  setState(STATE_CONNECTING, now);
  uint8_t ownAddressType = BLE_OWN_ADDR_PUBLIC;
  if (ble_hs_id_infer_auto(0, &ownAddressType)) return false;
  const int rc = ble_gap_connect(ownAddressType, &targetAddress_, 8000,
                                 nullptr, gapEvent, this);
  if (rc) {
    Serial.printf("Radar connect start failed: %d\n", rc);
    return false;
  }
  connecting_ = true;
  return true;
}

void RadarClient::scheduleRetry(uint32_t now) {
  connectPending_ = false;
  connecting_ = false;
  haveTargetAddress_ = false;
  retryAtMs_ = now + RETRY_DELAY_MS;
  if (enabled_) setState(STATE_SEARCHING, now);
}

void RadarClient::onDisconnected() {
  connHandle_ = BLE_HS_CONN_HANDLE_NONE;
  serviceStart_ = serviceEnd_ = notifyHandle_ = writeHandle_ = 0;
  clearTargets(millis());
  receiveLength_ = 0;
  configState_ = CONFIG_IDLE;
  if (enabled_) scheduleRetry(millis());
  else setState(STATE_DISABLED, millis());
  Serial.println("Radar disconnected");
}

void RadarClient::service(uint32_t now) {
  if (!enabled_) return;
  if (state_ == STATE_CONNECTED) serviceConfiguration(now);
  if (state_ != STATE_CONNECTED &&
      int32_t(now - attemptStartedMs_) >= int32_t(CONNECT_TIMEOUT_MS)) {
    // Latch timeout and cancel both scan and connection/discovery work.
    // Late asynchronous completions must not leave the timeout screen.
    setEnabled(false, now);
    setState(STATE_TIMED_OUT, now);
    return;
  }
  if (state_ == STATE_FOUND && connectPending_ &&
      int32_t(now - stateChangedMs_) >= int32_t(CONNECT_FRAME_DELAY_MS)) {
    connectPending_ = false;
    if (!startConnection(now)) scheduleRetry(millis());
    return;
  }
  if (state_ == STATE_SEARCHING && !scanning_ &&
      int32_t(now - retryAtMs_) >= 0) startScan(now);
}

void RadarClient::stopForSleep() {
  if (scan_ && scanning_) scan_->stop();
  scanning_ = false;
  connectPending_ = false;
  if (connecting_) ble_gap_conn_cancel();
  connecting_ = false;
  if (connHandle_ != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);
  connHandle_ = BLE_HS_CONN_HANDLE_NONE;
  scan_ = nullptr;
  haveTargetAddress_ = false;
  configState_ = CONFIG_IDLE;
  clearTargets(millis());
}

void RadarClient::failConnection(const char *reason, uint32_t now) {
  Serial.printf("Radar BLE setup failed: %s\n", reason);
  if (connHandle_ != BLE_HS_CONN_HANDLE_NONE)
    ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);
  connHandle_ = BLE_HS_CONN_HANDLE_NONE;
  scheduleRetry(now);
}

int RadarClient::gapEvent(ble_gap_event *event, void *arg) {
  auto *self = static_cast<RadarClient *>(arg);
  if (!self || !event) return 0;
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    self->connecting_ = false;
    if (!self->enabled_) {
      if (!event->connect.status)
        ble_gap_terminate(event->connect.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      return 0;
    }
    if (event->connect.status) {
      Serial.printf("Radar BLE connection failed: %d\n", event->connect.status);
      self->scheduleRetry(millis());
      return 0;
    }
    self->connHandle_ = event->connect.conn_handle;
    self->serviceStart_ = self->serviceEnd_ = 0;
    self->notifyHandle_ = self->writeHandle_ = 0;
    const ble_uuid16_t serviceUuid = BLE_UUID16_INIT(0xFFF0);
    const int rc = ble_gattc_disc_svc_by_uuid(
        self->connHandle_, &serviceUuid.u, serviceDiscovered, self);
    if (rc) self->failConnection("FFF0 discovery could not start", millis());
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    if (event->disconnect.conn.conn_handle == self->connHandle_)
      self->onDisconnected();
  } else if (event->type == BLE_GAP_EVENT_NOTIFY_RX &&
             event->notify_rx.conn_handle == self->connHandle_ &&
             event->notify_rx.attr_handle == self->notifyHandle_) {
    uint8_t bytes[160];
    size_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (length > sizeof(bytes)) length = sizeof(bytes);
    os_mbuf_copydata(event->notify_rx.om, 0, length, bytes);
    self->consume(bytes, length, millis());
  }
  return 0;
}

int RadarClient::serviceDiscovered(uint16_t connHandle,
                                   const ble_gatt_error *error,
                                   const ble_gatt_svc *service, void *arg) {
  auto *self = static_cast<RadarClient *>(arg);
  if (!self || connHandle != self->connHandle_) return 0;
  if (error->status == 0 && service) {
    self->serviceStart_ = service->start_handle;
    self->serviceEnd_ = service->end_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE && self->serviceStart_) {
    const int rc = ble_gattc_disc_all_chrs(
        connHandle, self->serviceStart_, self->serviceEnd_,
        characteristicDiscovered, self);
    if (!rc) return 0;
  }
  self->failConnection("FFF0 service missing", millis());
  return 0;
}

int RadarClient::characteristicDiscovered(
    uint16_t connHandle, const ble_gatt_error *error,
    const ble_gatt_chr *characteristic, void *arg) {
  auto *self = static_cast<RadarClient *>(arg);
  if (!self || connHandle != self->connHandle_) return 0;
  if (error->status == 0 && characteristic) {
    const ble_uuid16_t notifyUuid = BLE_UUID16_INIT(0xFFF1);
    const ble_uuid16_t writeUuid = BLE_UUID16_INIT(0xFFF2);
    if (ble_uuid_cmp(&characteristic->uuid.u, &notifyUuid.u) == 0)
      self->notifyHandle_ = characteristic->val_handle;
    else if (ble_uuid_cmp(&characteristic->uuid.u, &writeUuid.u) == 0)
      self->writeHandle_ = characteristic->val_handle;
    return 0;
  }
  if (error->status == BLE_HS_EDONE && self->notifyHandle_ &&
      self->writeHandle_) {
    const uint8_t enabled[2] = {1, 0};
    const int rc = ble_gattc_write_flat(
        connHandle, self->notifyHandle_ + 1, enabled, sizeof(enabled),
        notificationEnabled, self);
    if (!rc) return 0;
  }
  self->failConnection("FFF1/FFF2 characteristics missing", millis());
  return 0;
}

int RadarClient::notificationEnabled(uint16_t connHandle,
                                     const ble_gatt_error *error,
                                     ble_gatt_attr *, void *arg) {
  auto *self = static_cast<RadarClient *>(arg);
  if (!self || connHandle != self->connHandle_) return 0;
  if (!error->status) {
    self->receiveLength_ = 0;
    self->setState(STATE_CONNECTED, millis());
    Serial.printf("Radar connected: %s\n", self->deviceName_.c_str());
    self->applySettings(self->settings_, millis());
  } else {
    self->failConnection("FFF1 notifications rejected", millis());
  }
  return 0;
}

bool RadarClient::applySettings(const Settings &settings, uint32_t now) {
  settings_ = settings;
  settings_.maximumRangeMeters = uint8_t(constrain(
      int(settings_.maximumRangeMeters), 10, 100));
  settings_.direction = uint8_t(constrain(int(settings_.direction), 0, 2));
  settings_.minimumSpeedKmh = uint8_t(constrain(
      int(settings_.minimumSpeedKmh), 0, 120));
  settings_.triggerCount = uint8_t(constrain(
      int(settings_.triggerCount), 1, 10));
  if (settings_.snrThreshold != 0 &&
      (settings_.snrThreshold < 3 || settings_.snrThreshold > 32))
    settings_.snrThreshold = 0;
  if (state_ != STATE_CONNECTED || connHandle_ == BLE_HS_CONN_HANDLE_NONE ||
      !writeHandle_) {
    configState_ = CONFIG_IDLE;
    return false;
  }
  configStep_ = 0;
  nextConfigWriteMs_ = now;
  configState_ = CONFIG_APPLYING;
  return true;
}

bool RadarClient::sendCommand(uint16_t command, const uint8_t *payload,
                              size_t payloadLength) {
  if (connHandle_ == BLE_HS_CONN_HANDLE_NONE || !writeHandle_ ||
      payloadLength > 16) return false;
  uint8_t frame[28] = {0xFD, 0xFC, 0xFB, 0xFA};
  const uint16_t bodyLength = uint16_t(payloadLength + 2);
  frame[4] = uint8_t(bodyLength);
  frame[5] = uint8_t(bodyLength >> 8);
  frame[6] = uint8_t(command);
  frame[7] = uint8_t(command >> 8);
  if (payloadLength) memcpy(frame + 8, payload, payloadLength);
  const size_t tail = 8 + payloadLength;
  frame[tail] = 0x04;
  frame[tail + 1] = 0x03;
  frame[tail + 2] = 0x02;
  frame[tail + 3] = 0x01;
  return ble_gattc_write_no_rsp_flat(connHandle_, writeHandle_, frame,
                                     tail + 4) == 0;
}

void RadarClient::serviceConfiguration(uint32_t now) {
  if (configState_ != CONFIG_APPLYING ||
      int32_t(now - nextConfigWriteMs_) < 0) return;
  bool sent = false;
  if (configStep_ == 0) {
    const uint8_t enable[] = {1, 0};
    sent = sendCommand(0x00FF, enable, sizeof(enable));
    nextConfigWriteMs_ = now + 250;
  } else if (configStep_ == 1) {
    const uint8_t detection[] = {
        settings_.maximumRangeMeters, settings_.direction,
        settings_.minimumSpeedKmh, settings_.noTargetDelaySeconds};
    sent = sendCommand(0x0002, detection, sizeof(detection));
    nextConfigWriteMs_ = now + 300;
  } else if (configStep_ == 2) {
    const uint8_t sensitivity[] = {
        settings_.triggerCount, settings_.snrThreshold, 0, 0};
    sent = sendCommand(0x0003, sensitivity, sizeof(sensitivity));
    nextConfigWriteMs_ = now + 300;
  } else {
    sent = sendCommand(0x00FE, nullptr, 0);
    configState_ = sent ? CONFIG_APPLIED : CONFIG_FAILED;
    if (sent)
      Serial.printf("Radar settings applied: %um dir=%u min=%ukm/h hold=%us trigger=%u snr=%u\n",
                    settings_.maximumRangeMeters, settings_.direction,
                    settings_.minimumSpeedKmh, settings_.noTargetDelaySeconds,
                    settings_.triggerCount, settings_.snrThreshold);
    return;
  }
  if (!sent) {
    configState_ = CONFIG_FAILED;
    Serial.printf("Radar settings write failed at step %u\n", configStep_);
    return;
  }
  ++configStep_;
}

void RadarClient::consume(const uint8_t *data, size_t length, uint32_t now) {
  if (!data || !length) return;
  if (length > sizeof(receiveBuffer_) - receiveLength_) receiveLength_ = 0;
  if (length > sizeof(receiveBuffer_)) {
    data += length - sizeof(receiveBuffer_);
    length = sizeof(receiveBuffer_);
  }
  memcpy(receiveBuffer_ + receiveLength_, data, length);
  receiveLength_ += length;

  while (receiveLength_ >= 6) {
    size_t header = 0;
    while (header + sizeof(REPORT_HEADER) <= receiveLength_ &&
           !bytesEqual(receiveBuffer_ + header, REPORT_HEADER,
                       sizeof(REPORT_HEADER))) ++header;
    if (header) {
      memmove(receiveBuffer_, receiveBuffer_ + header, receiveLength_ - header);
      receiveLength_ -= header;
    }
    if (receiveLength_ < 6 ||
        !bytesEqual(receiveBuffer_, REPORT_HEADER, sizeof(REPORT_HEADER))) return;
    const size_t bodyLength = size_t(receiveBuffer_[4]) |
                              (size_t(receiveBuffer_[5]) << 8);
    const size_t frameLength = bodyLength + 10;
    if (frameLength > sizeof(receiveBuffer_)) {
      memmove(receiveBuffer_, receiveBuffer_ + 1, --receiveLength_);
      continue;
    }
    if (receiveLength_ < frameLength) return;
    if (bytesEqual(receiveBuffer_ + frameLength - 4, REPORT_TAIL,
                   sizeof(REPORT_TAIL)))
      parseFrame(receiveBuffer_, frameLength, now);
    memmove(receiveBuffer_, receiveBuffer_ + frameLength,
            receiveLength_ - frameLength);
    receiveLength_ -= frameLength;
  }
}

void RadarClient::parseFrame(const uint8_t *frame, size_t length, uint32_t now) {
  const size_t bodyLength = size_t(frame[4]) | (size_t(frame[5]) << 8);
  if (length != bodyLength + 10) return;
  // Present each radar report directly. The module's no-target delay controls
  // persistence; a clear heartbeat therefore clears the display immediately.
  if (!bodyLength) {
    clearTargets(now);
    return;
  }
  const uint8_t *body = frame + 6;
  if (bodyLength < 2 || bodyLength != size_t(2 + body[0] * 5)) return;
  Target next[MAX_TARGETS] = {};
  size_t count = 0;
  for (uint8_t index = 0; index < body[0] && count < MAX_TARGETS; ++index) {
    const uint8_t *record = body + 2 + index * 5;
    const bool approaching = record[2] == 0x01;
    const bool movingAway = record[2] == 0x00;
    if (!approaching && !movingAway) continue;
    const int8_t signedSpeed = approaching
        ? int8_t(record[3]) : int8_t(-int(record[3]));
    next[count++] = {int8_t(int(record[0]) - 0x80), record[1],
                     signedSpeed, record[4], approaching};
  }
  portENTER_CRITICAL(&targetMux_);
  memset(targets_, 0, sizeof(targets_));
  if (count) memcpy(targets_, next, count * sizeof(Target));
  targetCount_ = count;
  ++targetRevision_;
  portEXIT_CRITICAL(&targetMux_);
}

void RadarClient::clearTargets(uint32_t now) {
  (void)now;
  portENTER_CRITICAL(&targetMux_);
  memset(targets_, 0, sizeof(targets_));
  targetCount_ = 0;
  ++targetRevision_;
  portEXIT_CRITICAL(&targetMux_);
}

size_t RadarClient::snapshot(Target *output, size_t capacity,
                             uint32_t now) const {
  (void)now;
  if (!output || !capacity) return 0;
  portENTER_CRITICAL(&targetMux_);
  const size_t count = min(capacity, targetCount_);
  if (count) memcpy(output, targets_, count * sizeof(Target));
  portEXIT_CRITICAL(&targetMux_);
  return count;
}

uint32_t RadarClient::targetRevision() const {
  portENTER_CRITICAL(&targetMux_);
  const uint32_t revision = targetRevision_;
  portEXIT_CRITICAL(&targetMux_);
  return revision;
}
