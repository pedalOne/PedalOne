#include "rider_network.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace {
constexpr uint16_t RIDER_PACKET_MAGIC = 0x4452;  // "RD" on the wire.
constexpr uint8_t RIDER_PACKET_VERSION = 1;
constexpr uint8_t RIDER_FLAG_LOCATION_VALID = 1 << 0;
constexpr uint8_t RIDER_CHANNEL = 6;
// Avoid locking every rider to exactly the same one-second transmit cadence.
constexpr uint32_t RIDER_SEND_INTERVAL_MS = 1073;
constexpr uint8_t BROADCAST_ADDRESS[6] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
}

RiderNetwork *RiderNetwork::instance_ = nullptr;

bool RiderNetwork::start() {
  if (active_) return true;
  if (!receiveQueue_)
    receiveQueue_ = xQueueCreate(24, sizeof(ReceivedPacket));
  if (!receiveQueue_) {
    Serial.println("RiDar receive queue allocation failed");
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // Range testing uses continuous reception so a sleeping receiver cannot be
  // mistaken for a weak RF link. Reintroduce synchronized windows later.
  WiFi.setSleep(false);
  // ESP32-S3 maps the 20 dBm request to its highest supported actual power.
  if (!WiFi.setTxPower(WIFI_POWER_20dBm))
    Serial.println("RiDar could not set maximum TX power");
  // Keep legacy Wi-Fi available for maintenance/OTA while allowing RiDar
  // peers to use Espressif's proprietary long-range PHY.
  constexpr uint8_t protocols = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G |
                                WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR;
  if (esp_wifi_set_protocol(WIFI_IF_STA, protocols) != ESP_OK) {
    Serial.println("RiDar could not enable long-range Wi-Fi protocol");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  if (esp_wifi_set_channel(RIDER_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("RiDar could not select ESP-NOW channel");
    WiFi.mode(WIFI_OFF);
    return false;
  }
  if (esp_now_init() != ESP_OK) {
    Serial.println("RiDar ESP-NOW initialization failed");
    WiFi.mode(WIFI_OFF);
    return false;
  }

  instance_ = this;
  if (esp_now_register_recv_cb(receiveCallback) != ESP_OK) {
    Serial.println("RiDar receive callback registration failed");
    stop();
    return false;
  }

  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, BROADCAST_ADDRESS, sizeof(BROADCAST_ADDRESS));
  peer.channel = RIDER_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  const esp_err_t added = esp_now_add_peer(&peer);
  if (added != ESP_OK && added != ESP_ERR_ESPNOW_EXIST) {
    Serial.printf("RiDar broadcast peer failed: %s\n", esp_err_to_name(added));
    stop();
    return false;
  }
  esp_now_rate_config_t rate{};
  rate.phymode = WIFI_PHY_MODE_LR;
  rate.rate = WIFI_PHY_RATE_LORA_250K;
  const esp_err_t rateResult =
      esp_now_set_peer_rate_config(BROADCAST_ADDRESS, &rate);
  if (rateResult != ESP_OK) {
    Serial.printf("RiDar LR-250K configuration failed: %s\n",
                  esp_err_to_name(rateResult));
    stop();
    return false;
  }

  const uint64_t efuse = ESP.getEfuseMac();
  riderId_ = uint32_t(efuse) ^ uint32_t(efuse >> 32);
  if (!riderId_) riderId_ = 1;
  nextSequence_ = 0;
  lastSendMs_ = 0;
  active_ = true;
  Serial.printf("RiDar enabled: id=%08lx channel=%u LR=250K RX=continuous TX=20dBm\n",
                static_cast<unsigned long>(riderId_), RIDER_CHANNEL);
  return true;
}

void RiderNetwork::stop(bool preserveWifi) {
  if (active_) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
  }
  active_ = false;
  if (instance_ == this) instance_ = nullptr;
  if (receiveQueue_) xQueueReset(receiveQueue_);
  if (!preserveWifi) WiFi.mode(WIFI_OFF);
}

void RiderNetwork::pause() {
  stop(false);
}

void RiderNetwork::receiveCallback(const esp_now_recv_info_t *info,
                                   const uint8_t *data, int length) {
  RiderNetwork *self = instance_;
  if (!self || !self->receiveQueue_ || !info || !data ||
      length != int(sizeof(Packet))) return;
  ReceivedPacket received{};
  memcpy(&received.packet, data, sizeof(received.packet));
  received.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
  xQueueSend(self->receiveQueue_, &received, 0);
}

void RiderNetwork::makeCallSign(const String &nickname, char output[4]) const {
  size_t used = 0;
  for (size_t i = 0; i < nickname.length() && used < 4; ++i) {
    const uint8_t c = uint8_t(nickname[i]);
    if ((c >= 'a' && c <= 'z')) output[used++] = char(c - 'a' + 'A');
    else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')
      output[used++] = char(c);
  }
  if (!used) {
    static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
    output[used++] = 'P';
    output[used++] = HEX_DIGITS[(riderId_ >> 8) & 0x0f];
    output[used++] = HEX_DIGITS[(riderId_ >> 4) & 0x0f];
    output[used++] = HEX_DIGITS[riderId_ & 0x0f];
  }
  while (used < 4) output[used++] = ' ';
}

bool RiderNetwork::accept(const ReceivedPacket &received, uint32_t now) {
  const Packet &packet = received.packet;
  if (packet.magic != RIDER_PACKET_MAGIC ||
      packet.version != RIDER_PACKET_VERSION ||
      !(packet.flags & RIDER_FLAG_LOCATION_VALID) ||
      !packet.riderId || packet.riderId == riderId_ ||
      packet.latitudeE7 < -850000000 || packet.latitudeE7 > 850000000 ||
      packet.longitudeE7 < -1800000000 || packet.longitudeE7 > 1800000000)
    return false;

  Rider *slot = nullptr;
  Rider *oldest = &riders_[0];
  for (Rider &rider : riders_) {
    if (rider.id == packet.riderId) { slot = &rider; break; }
    if (!rider.id && !slot) slot = &rider;
    if (rider.lastSeenMs < oldest->lastSeenMs) oldest = &rider;
  }
  if (!slot) slot = oldest;
  slot->id = packet.riderId;
  memcpy(slot->callSign, packet.callSign, 4);
  slot->callSign[4] = 0;
  for (uint8_t i = 0; i < 4; ++i)
    if (slot->callSign[i] < 0x20 || slot->callSign[i] > 0x7e)
      slot->callSign[i] = '?';
  slot->latitudeE7 = packet.latitudeE7;
  slot->longitudeE7 = packet.longitudeE7;
  slot->lastSeenMs = now;
  slot->rssi = received.rssi;
  return true;
}

bool RiderNetwork::service(bool wifiClaimed, bool locationValid,
                           int32_t latitudeE7, int32_t longitudeE7,
                           const String &nickname, uint32_t now) {
  bool changed = false;
  if (!enabled_) {
    if (active_) stop(wifiClaimed);
    for (Rider &rider : riders_) {
      if (rider.id) changed = true;
      rider = {};
    }
    return changed;
  }
  if (wifiClaimed) {
    if (active_) stop(true);
    return false;
  }
  if (!active_ && !start()) return false;

  ReceivedPacket received{};
  while (xQueueReceive(receiveQueue_, &received, 0) == pdTRUE)
    changed = accept(received, now) || changed;

  for (Rider &rider : riders_) {
    if (rider.id && uint32_t(now - rider.lastSeenMs) > RIDER_TIMEOUT_MS) {
      rider = {};
      changed = true;
    }
  }

  if (locationValid && uint32_t(now - lastSendMs_) >= RIDER_SEND_INTERVAL_MS) {
    Packet packet{};
    packet.magic = RIDER_PACKET_MAGIC;
    packet.version = RIDER_PACKET_VERSION;
    packet.flags = RIDER_FLAG_LOCATION_VALID;
    packet.riderId = riderId_;
    makeCallSign(nickname, packet.callSign);
    packet.sequence = ++nextSequence_;
    packet.latitudeE7 = latitudeE7;
    packet.longitudeE7 = longitudeE7;
    const esp_err_t sent = esp_now_send(BROADCAST_ADDRESS,
                                        reinterpret_cast<uint8_t *>(&packet),
                                        sizeof(packet));
    if (sent == ESP_OK) lastSendMs_ = now;
  }
  return changed;
}

size_t RiderNetwork::snapshot(Rider *output, size_t capacity,
                              uint32_t now) const {
  if (!output || !capacity || !enabled_) return 0;
  size_t count = 0;
  for (const Rider &rider : riders_) {
    if (!rider.id || uint32_t(now - rider.lastSeenMs) > RIDER_TIMEOUT_MS)
      continue;
    output[count++] = rider;
    if (count == capacity) break;
  }
  return count;
}
